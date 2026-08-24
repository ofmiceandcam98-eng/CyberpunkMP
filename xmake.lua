set_xmakever("2.8.0")
set_policy("build.ccache", false)
set_policy("package.requires_lock", false)

add_cxflags("-fPIC")

-- c code will use c99,
set_languages("c99", "cxx20")

-- The release version reaches the binaries through the environment because only the
-- ship pipeline knows it (the version lives in the launcher's package.json, and only a
-- launcher ship moves it). A plain developer build has no release identity and says so.
local nco_version = os.getenv("NCO_BUILD_VERSION") or "0.0.0-dev"
local nco_major, nco_minor, nco_patch = nco_version:match("^(%d+)%.(%d+)%.(%d+)")
set_configvar("NCO_VERSION", nco_version)
set_configvar("NCO_VERSION_MAJOR", tonumber(nco_major) or 0)
set_configvar("NCO_VERSION_MINOR", tonumber(nco_minor) or 0)
set_configvar("NCO_VERSION_PATCH", tonumber(nco_patch) or 0)

add_configfiles("BuildInfo.h.in")

add_requires(
    "mimalloc 2.1.7",
    "spdlog",
    "hopscotch-map",
    "cryptopp",
    "gamenetworkingsockets",
    "glm",
    "openssl",
    "zlib",
    "nlohmann_json",
    "flecs v4.0.3",
    "protobuf-cpp 29.3",
    "entt",
    "microsoft-gsl",

    -- Voice. Opus rather than raw PCM because raw is not affordable: 48kHz mono 16-bit is
    -- ~94KB/s per person talking, which the server would then relay to everybody in range.
    -- Opus carries speech at 24kbps - about a fortieth - and is the codec every other game
    -- uses for the same reason.
    --
    -- Linked into the CLIENT only. The server relays frames without decoding them, so it
    -- needs no codec at all, and keeping it out of the server build means a codec change
    -- never forces a server redeploy.
    --
    -- "libopus", not "opus" - the latter is not a package and xmake reports it as an
    -- unfound name only at configure time. Pinned like every other load-bearing
    -- dependency here: a codec that changes frame layout under us is a silent audio bug,
    -- not a build failure.
    "libopus 1.5.2")

-- The 29.3 pin above only covers OUR requirement. Dependencies resolve their own -
-- gamenetworkingsockets asks for protobuf too, and left free it resolves to 35.x: the
-- version CONTRIBUTING documents as broken. This forces every transitive protobuf to
-- the version the code is written against, so exactly one protobuf gets linked.
--
-- Windows was deliberately excluded for a while: cached gamenetworkingsockets binaries
-- were built against their own newer protobuf, the two versions coexisted in the link
-- by MSVC name-decoration accident, and unifying meant a one-time package rebuild on
-- every Windows machine. That grace ended 2026-08-18, when the package repo moved
-- abseil past the <=20260107.1 cap gamenetworkingsockets declares while free-floating
-- protobuf 35.x demanded the newer one - an unsatisfiable resolve, and every Windows CI
-- run failed at configure. Unified, the whole tree resolves the way Linux (and every
-- machine with the old lockfile) already proved works. First Windows build after this
-- change rebuilds gamenetworkingsockets once; that is the announced cost.
add_requireconfs("**.protobuf-cpp", { version = "29.3", override = true })

-- Same treatment for abseil, protobuf's own dependency. Left floating it resolves to
-- "latest", and gamenetworkingsockets caps it - at <=20260107.1 on one repo snapshot,
-- <=20250127.0 on the CI runner's - so any machine whose snapshot has moved past the
-- cap fails to resolve at all. 20250127.0 is the newest version every observed cap
-- accepts, and the protobuf 29.3 era pairs with it.
add_requireconfs("**.abseil", { version = "20250127.0", override = true })

if is_plat("windows") then
    set_arch("x64")
    add_cxflags("/bigobj")
    add_defines("NOMINMAX")
end

add_defines("_UNICODE", "RED4EXT_STATIC_LIB", "GLM_ENABLE_EXPERIMENTAL")

set_warnings("all")
add_vectorexts("sse", "sse2", "sse3", "ssse3")

-- build configurations
add_rules("mode.debug", "mode.releasedbg", "mode.release")

if is_mode("debug") then
    add_defines("TP_DEBUG")
    set_symbols("debug", "edit")
end

includes('tools/codegen')
includes('tools/csharp')

-- add projects
includes("code/netpack")
includes("code/common")
includes("code/protocol")
includes("code/server")

option("game")
    set_showmenu(true)
    set_default("Cyberpunk2077.exe")
    set_description("Set the path to Cyberpunk2077.exe for easy debugging")

option("rpcdir")
    set_showmenu(true)
    set_default("")
    set_description("Set the path where the RPC files will be generated")

if is_plat("windows") then
    includes("code/assets")
    includes("code/client")
    includes("code/launcher")
    includes("code/loader")
    includes("vendor/")

    includes("@builtin/xpack")

    xpack("Cyberpunk Multiplayer")
        set_formats("zip")
        set_title("Cyberpunk Multiplayer")
        set_basename("Artifacts")
        set_author("Tilted Phoques SRL")
        set_description("Installer for Cyberpunk Multiplayer Launcher")
        --set_homepage("https://your-project-homepage.com")
        --set_licensefile("LICENSE.md")  -- Make sure this file exists in your project
        local function add_files_recursively(dir, root_dir)
            local relative_path = path.relative(dir, root_dir)
            add_installfiles(path.join(dir, "*"), {prefixdir = relative_path})
            for _, subdir in ipairs(os.dirs(path.join(dir, "*"))) do
                add_files_recursively(subdir, root_dir)
            end
        end

        add_files_recursively("distrib/launcher")

        set_version("0.1.0.0")
end
