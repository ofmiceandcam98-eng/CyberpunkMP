#include <shellapi.h>
#include <ArchiveXL/support/red4ext/ArchiveXL.hpp>
#include <TweakXL/support/red4ext/TweakXL.hpp>
#include <App/Settings.h>
#include <RED4ext/LaunchParameters.hpp>
#include <BuildInfo.h>

#include "RED4ext/Api/v1/EMainReason.hpp"

std::filesystem::path GCyberpunkMpLocation;

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

void Initialize()
{
    namespace fs = std::filesystem;

    constexpr auto pathLength = MAX_PATH + 1;
    std::string filename;
    do
    {
        filename.resize(filename.size() + pathLength, '\0');

        const auto length = GetModuleFileName((HINSTANCE)&__ImageBase, filename.data(), static_cast<uint32_t>(filename.size()));
        if (length > 0)
        {
            filename.resize(length);
        }
    } while (GetLastError() == ERROR_INSUFFICIENT_BUFFER);

    fs::path path = filename;
    if (is_symlink(path))
    {
        path = read_symlink(path);
    }

    GCyberpunkMpLocation = path.parent_path();
}

#include "App/Application.h"

// The game version this build has actually been tested against. Bump when the mod
// is verified on a newer patch.
constexpr uint8_t kSupportedGameMajor = 2;
constexpr uint16_t kSupportedGameMinor = 31;

RED4EXT_C_EXPORT bool Main(RED4ext::v1::PluginHandle aHandle, RED4ext::v1::EMainReason aReason, const RED4ext::v1::Sdk* aSdk)
{

    switch (aReason)
    {
    case RED4ext::v1::EMainReason::Load:
    {
        // Report the game version, and say plainly when it isn't one we've tested.
        // Without this, an unsupported patch shows up as a wall of script validation
        // errors ("Missing native function ...", "declared base class ... different
        // than current one ...") that give no hint the game version is the problem.
        // Warn rather than refuse: a later patch may well work, and blocking it would
        // be presumptuous.
        if (aSdk->runtime)
        {
            const auto& v = *aSdk->runtime;
            aSdk->logger->InfoF(aHandle, "Game version %u.%u.%u", v.major, v.minor, v.patch);

            if (v.major != kSupportedGameMajor || v.minor != kSupportedGameMinor)
            {
                aSdk->logger->WarnF(aHandle,
                                    "CyberpunkMP was tested against game %u.%u - you are on %u.%u. "
                                    "If the game refuses to start with script validation errors, "
                                    "this mismatch is the likely cause.",
                                    kSupportedGameMajor, kSupportedGameMinor, v.major, v.minor);
            }
        }

        Initialize();

        App::GApplication = MakeUnique<App::Application>(aHandle, aSdk);
        App::GApplication->Bootstrap();

        const auto scriptPath = GCyberpunkMpLocation / TP_REDSCRIPT_LOCATION;

        // Name the directory BEFORE canonical() throws on it. GCyberpunkMpLocation follows a
        // symlink to its target (see the module-path block above), so a leftover DevInstall
        // symlink points it into the build output tree - which has no assets/redscript (that
        // directory holds a web bundle). canonical() then throws std::filesystem_error, Main
        // returns via the exception, and RED4ext reports only "cannot find <path> ... did not
        // initialize properly, unloading" - the game runs VANILLA and it looks like a
        // regression. One line here turns an evening of diagnosis into a log line.
        // See atlas:a-dev-symlink-silently-beats-every-installed-test-build.
        std::error_code scriptEc;
        if (!std::filesystem::exists(scriptPath, scriptEc))
        {
            aSdk->logger->ErrorF(aHandle,
                                 "CyberpunkMP cannot find its redscript at '%s' - the mod will NOT load. "
                                 "This usually means CyberpunkMP.dll is a DevInstall symlink pointing at a "
                                 "build tree that has no assets/redscript. Remove the dev link, or install "
                                 "the shipped build (launcher: Tools > Test builds > Restore).",
                                 scriptPath.string().c_str());
        }

        // Stored, not .c_str()'d off a temporary: canonical() returns a temporary fs::path, so
        // taking its c_str() here and using it in the next statement read freed memory (UB that
        // happened to work). The path must outlive the Add() call.
        const auto scriptLocation = canonical(scriptPath);
        aSdk->scripts->Add(aHandle, scriptLocation.c_str());

        ArchiveXL::RegisterArchives(canonical(GCyberpunkMpLocation / TP_ARCHIVES_LOCATION));
        TweakXL::RegisterTweaks(canonical(GCyberpunkMpLocation / TP_TWEAKS_LOCATION));

        const auto inputLoaderModule = LoadLibrary("input_loader");
        void(*pInputLoaderAdd)(RED4ext::v1::PluginHandle, const wchar_t*);
        if (inputLoaderModule != nullptr && (pInputLoaderAdd = reinterpret_cast<decltype(pInputLoaderAdd)>(GetProcAddress(inputLoaderModule, "Add"))))
        {
            // Stored, not .c_str()'d off a temporary - same UB as scriptLocation above.
            const auto inputPath = canonical(GCyberpunkMpLocation / TP_INPUTS_LOCATION);
            pInputLoaderAdd(aHandle, inputPath.c_str());
        }
        else
        {
            const auto message =
                L"The following CyperpunkMP requirements were not met:\n\n* Input Loader v0.2.0\nPlease ensure the mods "
                L"above are installed/up-to-date.";
            MessageBoxW(nullptr, message, L"CyperpunkMP requirements could not be found", MB_SYSTEMMODAL | MB_ICONERROR);
            return false;
        }

        for (auto& mod : Settings::Get().mods)
        {
            aSdk->scripts->Add(aHandle, mod.wstring().c_str());
            ArchiveXL::RegisterArchives(mod);
            TweakXL::RegisterTweaks(mod);
            pInputLoaderAdd(aHandle, mod.wstring().c_str());
        }

        break;
    }
    case RED4ext::v1::EMainReason::Unload:
    {
        if (Settings::IsDisabled())
            return false;

        App::GApplication->Shutdown();
        App::GApplication = nullptr;
        break;
    }
    }

    return true;
}

RED4EXT_C_EXPORT void Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"CyberpunkMP";
    aInfo->author = L"Tilted Phoques SRL";

    // Stamped from the release at ship time (BuildInfo.h, via NCO_BUILD_VERSION). This
    // sat frozen at 0.1.0 for the project's whole life, which meant the one version
    // surface RED4ext itself displays never matched anything anyone shipped.
    aInfo->version = RED4EXT_V1_SEMVER(BUILD_VERSION_MAJOR, BUILD_VERSION_MINOR, BUILD_VERSION_PATCH);

    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_INDEPENDENT;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t Supports()
{
    return RED4EXT_API_VERSION_1;
}
