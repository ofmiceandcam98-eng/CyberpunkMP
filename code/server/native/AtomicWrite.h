#pragma once

/**
 * Replace a file's contents without ever leaving it truncated or half-written.
 *
 * WHY THIS EXISTS
 *
 * Every store here persisted the same way:
 *
 *     std::ofstream file(m_path);
 *     file << nlohmann::json(m_records).dump(2);
 *
 * The first line TRUNCATES the live file. From that instant until the last byte lands, the
 * authoritative copy does not exist - and for players.json that is every character on the
 * server, not one. A crash, a power cut, a full disk or a killed container in that window
 * leaves a file that is empty or half a JSON document, and the next load finds neither.
 *
 * Nothing kept a backup either, so the failure was unrecoverable rather than inconvenient.
 *
 * WHAT THIS DOES INSTEAD
 *
 *     1. serialise fully, in memory        - a serialisation failure touches nothing
 *     2. write it to <target>.tmp          - the live file is still intact and complete
 *     3. flush and fsync the temp          - the bytes are on the disk, not in a cache
 *     4. copy the live file to <target>.bak - the previous good state, saved BEFORE risk
 *     5. atomically replace target with tmp
 *
 * At every point at least one complete, valid file exists. There is no instant at which a
 * crash loses both.
 *
 * ORDER IS THE DESIGN. The backup is taken from the live file BEFORE the replace, because a
 * backup written after would describe the new state and be worthless for recovering from a
 * bad one. And the temp is fully synced BEFORE the replace, because replacing with a file
 * whose bytes are still in the page cache just moves the corruption window rather than
 * closing it.
 *
 * PLATFORM. The production server is GCC in a container on Linux; Windows is the development
 * build. Both are handled explicitly rather than relying on std::filesystem::rename, whose
 * behaviour when the destination exists is worth being explicit about in code that exists
 * specifically to be careful:
 *
 *     POSIX   rename(2) is atomic within a filesystem and replaces the destination.
 *     Windows MoveFileExW with MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH.
 *
 * Both temp and target live in the same directory, which is what makes the POSIX rename
 * atomic - across filesystems it degrades to copy-and-delete and the guarantee is lost.
 */

#include <string>
#include <filesystem>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <atomic>
#include <thread>
#include <sstream>

// Small, targeted platform headers rather than <windows.h>.
//
// This header is included by every store (PlayerStore, MessageStore, CallStore, BanList,
// VehicleStore, WorldFacts), and dragging <windows.h> into all of them would bring its macro
// pollution - min, max, GetMessage - along with it. <io.h> gives _commit and nothing else.
//
// The replace itself uses std::filesystem::rename, which is the portable spelling of the
// platform-correct operation: the standard requires it to replace an existing non-directory
// destination, and MSVC implements it with MoveFileExW + MOVEFILE_REPLACE_EXISTING - the
// Windows-safe mechanism, reached without the header.
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace AtomicWrite
{
namespace Detail
{
/**
 * Write the bytes and make sure they are actually ON THE DISK before returning.
 *
 * fwrite leaves data in the C library's buffer; fflush pushes it to the OS; only fsync
 * (_commit on Windows) pushes it past the OS cache to the device. Skipping the last step is
 * the classic version of this bug: the replace succeeds, power is lost a moment later, and
 * the "safely written" file turns out to be zeros.
 *
 * FILE* rather than std::ofstream deliberately - there is no portable way to get a file
 * descriptor out of an ofstream, and without one there is nothing to sync.
 */
inline bool WriteAndSync(const std::filesystem::path& acPath, const std::string& acContents,
                         std::string* apError)
{
    const auto fail = [apError](const std::string& acWhy)
    {
        if (apError)
            *apError = acWhy;
        return false;
    };

#ifdef _WIN32
    FILE* pFile = nullptr;
    if (_wfopen_s(&pFile, acPath.wstring().c_str(), L"wb") != 0 || !pFile)
        return fail("could not open the temporary file");
#else
    FILE* pFile = std::fopen(acPath.string().c_str(), "wb");
    if (!pFile)
        return fail(std::string("could not open the temporary file: ") + std::strerror(errno));
#endif

    if (!acContents.empty() &&
        std::fwrite(acContents.data(), 1, acContents.size(), pFile) != acContents.size())
    {
        std::fclose(pFile);
        return fail("the write did not complete - the disk may be full");
    }

    if (std::fflush(pFile) != 0)
    {
        std::fclose(pFile);
        return fail("could not flush the temporary file");
    }

#ifdef _WIN32
    const int synced = _commit(_fileno(pFile));
#else
    const int synced = ::fsync(::fileno(pFile));
#endif

    if (synced != 0)
    {
        std::fclose(pFile);
        return fail("could not sync the temporary file to the disk");
    }

    if (std::fclose(pFile) != 0)
        return fail("could not close the temporary file cleanly");

    return true;
}

/**
 * A temp filename unique to THIS call.
 *
 * The first version used a single "<target>.tmp" for every write, and the concurrency test
 * caught what that means: two writers building the same temp file interleave their bytes
 * into it, and then one of them renames the resulting mixture into place. That is precisely
 * the corruption this unit exists to prevent, reintroduced one step earlier.
 *
 * Thread id plus a counter, so two threads and two calls on one thread are all distinct.
 * Still beside the target, because the rename is only atomic within a filesystem.
 */
inline std::filesystem::path UniqueTemp(const std::filesystem::path& acTarget)
{
    static std::atomic<uint64_t> sCounter{0};

    std::ostringstream suffix;
    suffix << ".tmp." << std::this_thread::get_id() << "." << sCounter.fetch_add(1);

    return std::filesystem::path(acTarget).concat(suffix.str());
}
} // namespace Detail

/**
 * Write `acContents` over `acTarget`, keeping the previous contents as `<target>.bak`.
 *
 * Returns false and leaves the existing file UNTOUCHED on any failure. `apError`, when
 * given, receives a reason suitable for a log line.
 *
 * A failure to write the backup is NOT fatal and does not fail the call: the backup is a
 * convenience for recovery, and refusing to save because the convenience failed would trade
 * a real guarantee for a nicety. It is reported through `apError` either way.
 *
 * Inline and header-only, like every other store in this directory - which also means a unit
 * test can include the header and exercise the real implementation without linking anything.
 */
inline bool Replace(const std::filesystem::path& acTarget, const std::string& acContents,
                    std::string* apError = nullptr)
{
    if (acTarget.empty())
    {
        if (apError)
            *apError = "no path";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(acTarget.parent_path(), ec);

    const auto temp = Detail::UniqueTemp(acTarget);
    const auto backup = std::filesystem::path(acTarget).concat(".bak");

    if (!Detail::WriteAndSync(temp, acContents, apError))
    {
        // The live file was never opened, so it is untouched. Clear the partial temp so a
        // later reader cannot mistake it for a real one.
        std::filesystem::remove(temp, ec);
        return false;
    }

    /*
     * The backup is taken from the LIVE file, BEFORE the replace.
     *
     * Taken afterwards it would be a copy of the new state, which is useless for recovering
     * from a bad one. Taken from the live file it is by definition the last known-good
     * contents.
     *
     * copy_file, not rename: renaming the live file away would leave a window with no target
     * at all, which is the exact failure this unit exists to remove. A copy leaves the
     * original in place the entire time.
     */
    if (std::filesystem::exists(acTarget, ec))
    {
        std::error_code backupError;
        std::filesystem::copy_file(acTarget, backup,
                                   std::filesystem::copy_options::overwrite_existing, backupError);

        if (backupError && apError)
            *apError = "saved, but the backup could not be written: " + backupError.message();
    }

    // The portable spelling of the platform-correct replace - see the note on the includes.
    std::error_code renameError;
    std::filesystem::rename(temp, acTarget, renameError);

    if (renameError)
    {
        if (apError)
            *apError = "could not replace the file: " + renameError.message();

        // The target still holds its previous, complete contents - nothing was truncated.
        std::filesystem::remove(temp, ec);
        return false;
    }

    return true;
}
} // namespace AtomicWrite
