if not os.isfile(get_config("game")) then
    print("Please set the path to Cyberpunk2077.exe using the xmake f --game=<path> command")
else
    local function is_game_running(os)
        if not is_plat("windows") then
            return false
        end
        local stdout, stderr = os.iorunv("tasklist", {"/FI", "IMAGENAME eq Cyberpunk2077.exe"})

        return string.find(stdout, "Cyberpunk2077.exe")
    end

    -- Installing the built DLL is shared by both targets below and lives in installdll.lua,
    -- because it used to be two copies of a step that DESTROYED a launcher install: it
    -- deleted zzzCyberpunkMP before linking, and when the link then failed for want of the
    -- symlink privilege the install was simply gone (zeldfep's box, 2026-09-09 - a bare
    -- `xmake -y` left that folder with zero files and the launcher saying "install the mod
    -- first"). One copy, no delete, and a failure that leaves the install intact.
    local function install_dll(target)
        local targetdir = target:targetdir()
        local clientdir = path.join(os.projectdir(), target:dep("Client"):targetdir())

        local basedir = path.join(targetdir, "..\\..\\")
        local red4extPlugins = path.join(basedir, "red4ext\\plugins")

        import("installdll", {rootdir = os.scriptdir()})

        return installdll.install_client_dll(clientdir, red4extPlugins, is_game_running(os))
    end

    target("Cyberpunk2077")
        set_kind("binary")
        set_basename("Cyberpunk2077")
        set_group("Client")
        add_options("game")
        set_runargs("--online", "--skipStartMenu", "--ip=127.0.0.1")
        set_targetdir(path.directory(get_config("game")))
        on_build(function(target) end)
        on_clean(function(target) end)
        on_link(function(target) end)
        on_install(function(target) end)
        after_build(function(target)
            install_dll(target)
        end)

        add_deps("Client")

    target("RpcGenerator")
        set_kind("binary")
        set_basename("Cyberpunk2077")
        add_options("game", "rpcdir")
        set_group("Client")
        set_runargs("--online", "--skipStartMenu", "--ip=127.0.0.1", "--rpc", "--rpcdir=\"" .. get_config("rpcdir") .. "\"")
        set_targetdir(path.directory(get_config("game")))
        on_build(function(target) end)
        on_clean(function(target) end)
        on_link(function(target) end)
        on_install(function(target) end)
        after_build(function(target)
            install_dll(target)
        end)

        add_deps("Client")
end
