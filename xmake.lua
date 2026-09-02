set_xmakever("2.8.0")
set_policy("build.ccache", false)
set_policy("package.requires_lock", false)

add_cxflags("-fPIC")

-- c code will use c99,
set_languages("c99", "cxx20")
add_configfiles("BuildInfo.h.in")

-- Every dependency is pinned, deliberately.
--
-- Unpinned entries resolve to whatever is newest on the day, so a clean checkout builds
-- against a different set of libraries every time. CI does not notice because it restores
-- a cached package set keyed on hashFiles('**/xmake.lua') - the cache hides the drift, and
-- only a fresh clone finds it. As of August 2026 a fresh clone of main does not build.
--
-- These are the versions a full build was verified against, not guesses. Loosening any of
-- them is fine; doing it by accident is what this prevents.
add_requires(
    "mimalloc 2.1.7",
    "spdlog v1.17.0",
    "hopscotch-map v2.4.0",
    "cryptopp 8.9.0",
    "gamenetworkingsockets v1.6.0",
    "glm 1.0.3",
    "openssl 1.1.1-w",
    "zlib v1.3.2",
    "nlohmann_json v3.12.0",
    "flecs v4.0.3",
    -- 3.19.4 lacks RecordError and absl::string_view; 35.1 removed
    -- FieldDescriptor::is_optional() and internal symbols code/netpack/cpp/helpers.h
    -- reaches for. 29.3 is the version this code was written against.
    "protobuf-cpp 29.3",
    "entt v3.16.0",
    "microsoft-gsl v4.2.2")

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
