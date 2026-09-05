// Atomic persistence: the live file must never be truncated, and a failure must never
// destroy what was already there.
//
// This includes the REAL header, not a mirror. AtomicWrite is header-only like every other
// store here, so the test exercises exactly the code the server runs - a copy of the
// algorithm would be free to drift away from the thing it claims to protect, which for a
// data-safety unit is worse than no test at all.
//
// These are real files on a real disk in a temporary directory, not a simulation. The
// property under test is what survives on disk, so simulating the disk would test nothing.

#include "AtomicWrite.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

static std::string Read(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    if (!file)
        return {};

    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

static void Write(const std::filesystem::path& acPath, const std::string& acText)
{
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    file << acText;
}

int main()
{
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) / "nco-atomicwrite-test";

    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    const auto target = dir / "players.json";
    const auto backup = std::filesystem::path(target).concat(".bak");
    const auto temp = std::filesystem::path(target).concat(".tmp");

    { // a first write, where there is nothing to replace
        std::filesystem::remove(target, ec);

        Check(AtomicWrite::Replace(target, "{\"a\":1}"), "the first write succeeds with no existing file");
        Check(Read(target) == "{\"a\":1}", "and the contents are exactly what was asked for");
        Check(!std::filesystem::exists(temp), "the temporary file is not left behind");
    }

    { // replacing an existing file
        Write(target, "OLD");

        Check(AtomicWrite::Replace(target, "NEW"), "replacing an existing file succeeds");
        Check(Read(target) == "NEW", "the live file holds the new contents");
        Check(Read(backup) == "OLD", "and the backup holds the PREVIOUS contents, not the new ones");
    }

    { // repeated saves - the brief's A, B, C
        Write(target, "A");
        AtomicWrite::Replace(target, "B");
        AtomicWrite::Replace(target, "C");

        Check(Read(target) == "C", "after A, B, C the live file is C");
        Check(Read(backup) == "B", "and the backup is B - the last good state before it");
    }

    { // THE POINT: the live file is never truncated at any moment
        Write(target, "IMPORTANT DATA");

        // A write of a large payload; whatever happens, the target must never be observed
        // empty. The strongest check available without racing the filesystem is that the
        // target is complete both before and after, and that the temp is what gets built.
        std::string big(2 * 1024 * 1024, 'x');

        Check(AtomicWrite::Replace(target, big), "a large write succeeds");
        Check(Read(target).size() == big.size(), "the live file is the full size, not partial");
        Check(Read(backup) == "IMPORTANT DATA", "and the previous contents survived in the backup");
    }

    { // an unwritable temp location must leave the original alone
        const auto badDir = dir / "nonexistent-parent-is-created";
        const auto badTarget = badDir / "deep" / "players.json";

        // create_directories inside Replace should make this work rather than fail
        Check(AtomicWrite::Replace(badTarget, "{}"), "a missing parent directory is created");
        Check(Read(badTarget) == "{}", "and the file lands there");
    }

    { // a failure must not destroy the existing file - simulated by an impossible target
        Write(target, "MUST SURVIVE");

        // A path that cannot be written: a directory where the file should be.
        const auto blocked = dir / "blocked.json";
        std::filesystem::remove_all(blocked, ec);
        std::filesystem::create_directories(blocked, ec);   // now a DIRECTORY, not a file

        std::string why;
        const bool ok = AtomicWrite::Replace(blocked, "data", &why);

        Check(!ok, "writing over a directory fails rather than pretending to succeed");
        Check(!why.empty(), "and reports a reason");
        Check(Read(target) == "MUST SURVIVE", "an unrelated file is untouched by the failure");
    }

    { // the temp file is cleaned up after a failed replace
        const auto blocked = dir / "blocked.json";
        const auto blockedTemp = std::filesystem::path(blocked).concat(".tmp");

        std::string why;
        AtomicWrite::Replace(blocked, "data", &why);

        Check(!std::filesystem::exists(blockedTemp), "a failed write leaves no stray .tmp behind");
    }

    { // the backup is never the ONLY copy - after a successful write both exist
        Write(target, "FIRST");
        AtomicWrite::Replace(target, "SECOND");

        Check(std::filesystem::exists(target), "the live file exists after a write");
        Check(std::filesystem::exists(backup), "and so does the backup");
        Check(Read(target) != Read(backup), "they hold different states, so one is always recoverable");
    }

    { // concurrent writers must not interleave into one file
        Write(target, "START");

        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i)
        {
            threads.emplace_back(
                [&target, i]
                {
                    // Each writes a distinct, self-identifying payload of a distinct length.
                    const std::string payload(1000 + i * 100, static_cast<char>('A' + i));
                    for (int n = 0; n < 20; ++n)
                        AtomicWrite::Replace(target, payload);
                });
        }

        for (auto& t : threads)
            t.join();

        // Whatever won, the file must be ONE payload - all the same character, and a length
        // matching exactly one writer. A torn or interleaved write shows up as a mixture.
        const auto finalContents = Read(target);

        bool uniform = !finalContents.empty();
        for (char c : finalContents)
        {
            if (c != finalContents[0])
            {
                uniform = false;
                break;
            }
        }

        const bool plausibleLength =
            finalContents.size() >= 1000 && finalContents.size() <= 1000 + 7 * 100 &&
            ((finalContents.size() - 1000) % 100) == 0;

        Check(uniform, "after 160 concurrent writes the file is ONE writer's payload, not a mixture");
        Check(plausibleLength, "and its length matches exactly one writer, so nothing was torn");
    }

    std::filesystem::remove_all(dir, ec);

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
