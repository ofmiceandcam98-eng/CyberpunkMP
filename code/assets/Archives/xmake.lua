rule("archive")
    set_extensions(".archive")
    -- on_build_file(function (target, sourcefile, opt)
    --     os.cp(sourcefile, path.join(target:targetdir(), path.basename(sourcefile) .. ".archive"))
    -- end)
    on_install(function (target)
        print("on_install")
        for _, sourcebatch in pairs(target:sourcebatches()) do
            local sourcekind = sourcebatch.rulename
            if sourcekind == "archive" then
                for _, sourcefile in ipairs(sourcebatch.sourcefiles) do
                    os.cp(sourcefile, path.join(target:installdir("launcher"), "mod", "assets", "Archives", path.basename(sourcefile) .. ".archive"))
                end
            end
        end
    end)

target("Archives")
    if is_mode("debug") then
        add_defines("TP_ARCHIVES_LOCATION=\"../../../../code/assets/Archives/packed/archive/pc/mod/\"", {public = true})
    else 
        add_defines("TP_ARCHIVES_LOCATION=\"assets/Archives\"", {public = true})
    end 
    set_kind("headeronly")
    set_group("Assets")
    add_rules("archive")
    add_files("packed/archive/pc/mod/CyberpunkMP.archive")

    -- The clean multiplayer start.
    --
    -- A SECOND archive rather than folding the gamedef into CyberpunkMP.archive, because
    -- that one is a packed binary in the tree and repacking it to add one 1 KB file risks
    -- losing something already in it for no benefit. ArchiveXL is handed the whole
    -- directory (Main.cpp, RegisterArchives) so every archive here loads.
    --
    -- It overrides ONE shipped file, ep1\quest\ep1_standalone.gamedef - the definition
    -- behind the "Phantom Liberty" button on the New Game screen, which is where the mod's
    -- character creation ends up because preGameScenarios.script:308 always takes the EP1
    -- branch when the expansion is installed. Stock, that gamedef starts three root quests;
    -- ours drops the story one and keeps the two that build the world:
    --
    --   cyberpunk2077_ep1_standalone.quest   base Night City, prologue already skipped
    --   ep1.quest                            EP1 world at the "Base" socket, no story
    --   ep1_preorder.quest                   the Quadra
    --
    -- All three are CDPR's own files; nothing here is authored. Proven in game on
    -- 2026-08-30: no Songbird, no Dog Eat Dog, and an empty quest log on a fresh character.
    -- The spawn tag is the base game's own #q000_spwn_start, which is a holding area rather
    -- than a place - the server moves new arrivals to its start point on connect.
    add_files("packed/archive/pc/mod/zz_NightCityOnline_CleanStart.archive")