if not os.isfile(get_config("game")) then
    print("Please set the path to Cyberpunk2077.exe using the xmake f --game=<path> command")
else
    -- Installing the built DLL is shared by both targets below and lives in installdll.lua,
    -- because it used to be two copies of a step that DESTROYED a launcher install: it
    -- deleted zzzCyberpunkMP before linking, and when the link then failed for want of the
    -- symlink privilege the install was simply gone (zeldfep's box, 2026-09-09 - a bare
    -- `xmake -y` left that folder with zero files and the launcher saying "install the mod
    -- first"). One copy, no delete, and a failure that leaves the install intact.
    -- THE MODULE IS PASSED IN, NOT IMPORTED HERE, and that is not a style choice.
    --
    -- `import` exists only in xmake's SCRIPT scope - inside an on_*/after_* callback. This
    -- function is a `local function` declared in DESCRIPTION scope, so its environment is
    -- the description sandbox, where `import` is nil. Lua resolves globals against the
    -- environment the function was CREATED in, not the one it is called from, so calling it
    -- from inside after_build did not help: the build linked CyberpunkMP.dll and then died
    -- with "attempt to call a nil value (global 'import')" (2026-10-30, first full
    -- `xmake -y` in a long while - the ship path builds named targets and does not reach
    -- this hook, which is why it stayed hidden).
    local function install_dll(target, installdll, game_running)
        local targetdir = target:targetdir()
        local clientdir = path.join(os.projectdir(), target:dep("Client"):targetdir())

        local basedir = path.join(targetdir, "..\\..\\")
        local red4extPlugins = path.join(basedir, "red4ext\\plugins")

        return installdll.install_client_dll(clientdir, red4extPlugins, game_running)
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
            -- Imported HERE because this is script scope, where `import` exists.
            import("installdll", {rootdir = path.join(os.projectdir(), "code", "loader")})
            -- The answer is computed in the MODULE, and only the boolean travels.
            -- Everything that check needs - `try`, a full `os` - exists in script scope and
            -- the imported module, and in neither case in xmake's description scope. Three
            -- faults in a row here (import, os.iorunv, then try) were all that same lesson.
            install_dll(target, installdll, is_plat("windows") and installdll.is_game_running() or false)
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
            -- Imported HERE because this is script scope, where `import` exists.
            import("installdll", {rootdir = path.join(os.projectdir(), "code", "loader")})
            -- The answer is computed in the MODULE, and only the boolean travels.
            -- Everything that check needs - `try`, a full `os` - exists in script scope and
            -- the imported module, and in neither case in xmake's description scope. Three
            -- faults in a row here (import, os.iorunv, then try) were all that same lesson.
            install_dll(target, installdll, is_plat("windows") and installdll.is_game_running() or false)
        end)

        add_deps("Client")
end
