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

    -- The character selection screen's backdrop.
    --
    -- A THIRD archive, for the same reason there is a second one: CyberpunkMP.archive is a
    -- packed binary in the tree and repacking it to add two files risks losing something
    -- already in it for no benefit. ArchiveXL loads every archive in this directory.
    --
    -- Two files, both authored here rather than overriding anything CDPR ships:
    --
    --   nightcityonline\character_select_bg.xbm       the render, at exactly the 1920x1080
    --                                                 the menu lays out in
    --   nightcityonline\character_select_bg.inkatlas  a single-texture atlas with one part
    --                                                 called "whole", which is the only way
    --                                                 an inkImage can reference a texture -
    --                                                 it binds an atlas, never a raw xbm
    --
    -- The atlas was built from a stock LOADING SCREEN atlas rather than authored blind
    -- (base\gameplay\gui\fullscreen\loading\bsc_d_a_4k.inkatlas), because those are already
    -- the exact shape wanted: one full-screen image, one "whole" part, full UV. Its slot 0
    -- and slot 1 are the 4K and 1080p variants the game picks between; both point at the one
    -- texture here, since it is authored at the resolution the menu uses.
    --
    -- Regenerating it, if the render ever changes: replace source\raw\nightcityonline\
    -- character_select_bg.png, then WolvenKit.CLI import -> convert deserialize the atlas
    -- json beside it -> pack. The .inkatlas.json is kept in the tree ON PURPOSE so the atlas
    -- can be rebuilt without extracting a stock one again.
    add_files("packed/archive/pc/mod/zz_NightCityOnline_Selector.archive")