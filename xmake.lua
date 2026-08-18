set_xmakever("2.8.0")
set_policy("build.ccache", false)
set_policy("package.requires_lock", false)

add_cxflags("-fPIC")

-- c code will use c99,
set_languages("c99", "cxx20")
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
    "microsoft-gsl")

-- The 29.3 pin above only covers OUR requirement. Dependencies resolve their own -
-- gamenetworkingsockets asks for protobuf too, and on the Linux container build (which
-- the lockfile does not cover) it resolved to 35.1: the version CONTRIBUTING documents
-- as broken. This forces every transitive protobuf to the version the code is written
-- against, so the container links exactly one protobuf.
--
-- Deliberately NOT applied on Windows. There, the cached gamenetworkingsockets binary
-- was built against its own newer protobuf and the two versions currently coexist in
-- the link by MSVC name-decoration accident - it works, everyone's caches assume it,
-- and unifying it means a coordinated package-cache rebuild on every Windows machine.
-- That cleanup deserves its own change, announced, not a side effect of fixing Linux.
if not is_plat("windows") then
    add_requireconfs("**.protobuf-cpp", { version = "29.3", override = true })
end

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
