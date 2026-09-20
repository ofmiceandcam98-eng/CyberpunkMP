--
-- Put the freshly built CyberpunkMP.dll where the game will load it - WITHOUT ever
-- destroying an install that is already there.
--
-- THE BUG THIS EXISTS FOR (2026-09-09, zeldfep's box). The post-build step used to be:
-- delete zzzCyberpunkMP, recreate it, link the DLL in. Deleting first is fine while that
-- folder is a junction into distrib, which is the dev layout the upstream step assumed.
-- On a LAUNCHER install it is a real directory holding the real DLL, assets, Rpc and
-- .nco-version - and a bare `xmake -y` deleted all of it, then failed to create the link
-- with "A required privilege is not held by the client" because creating a symlink needs
-- either an elevated shell or Developer Mode. The build reported success, the install was
-- gone, and the launcher said "Mod folder not found - install the mod first". Recovery was
-- a full Remove + reinstall.
--
-- The rule here: the step may ADD a link, and may replace a link it made itself. It may
-- never remove somebody's files. Upstream wrote the destructive version (2972a70,
-- a7835b0, 2024); this is our replacement for it.
--
-- Returned status strings, so callers (and the test) can be specific:
--   "linked"        a fresh link was created
--   "relinked"      our own previous link was replaced
--   "game-running"  nothing done, the game holds the DLL open
--   "not-a-link"    a real install is there - refused, nothing touched
--   "junction"      the whole folder is a junction (dev layout) - left alone
--   "link-failed"   the link could not be created; the existing install is INTACT
--   "no-dll"        the build produced no DLL to install
--
function install_client_dll(clientdir, red4ext_plugins, game_is_running)
    local mod_path = path.join(red4ext_plugins, "zzzCyberpunkMP")
    local dll_source = path.join(clientdir, "CyberpunkMP.dll")
    local dll_target = path.join(mod_path, "CyberpunkMP.dll")

    if game_is_running then
        print("CyberpunkMP.dll not installed: game is running.")
        return "game-running"
    end

    if not os.isfile(dll_source) then
        print("CyberpunkMP.dll not installed: the build produced no DLL at " .. dll_source)
        return "no-dll"
    end

    -- A junction into distrib IS the dev layout: whatever is inside is already ours and
    -- already current, so there is nothing to do and everything to lose by touching it.
    if os.exists(mod_path) and os.islink(mod_path) then
        print("zzzCyberpunkMP is a junction - leaving it alone (its contents come from distrib).")
        return "junction"
    end

    if os.exists(mod_path) then
        -- A real directory. Only two shapes are safe to touch: empty, or holding nothing
        -- but a link we made. Anything else is a launcher install, and clearing it is the
        -- bug this file exists to prevent.
        local ours = (not os.exists(dll_target)) or os.islink(dll_target)

        if not ours then
            print("zzzCyberpunkMP holds a real CyberpunkMP.dll - this looks like a launcher install, so it was left untouched.")
            print("  what   the build did NOT install its DLL, on purpose: replacing a launcher install from a build is how one gets destroyed.")
            print("  fix    use the launcher (Tools > Test builds, or Remove + install), or .\\tools\\Verify.ps1 which builds without touching the game folder.")
            return "not-a-link"
        end

        if os.exists(dll_target) then
            os.rm(dll_target)
        end
    else
        os.mkdir(mod_path)
    end

    -- LINKING CAN FAIL, and it must fail HARMLESSLY. Creating a symlink needs Developer
    -- Mode or an elevated shell; without either, os.ln raises. Nothing above has removed
    -- anyone's files by this point, so a failure here leaves the install exactly as it was.
    -- try/catch rather than pcall: xmake runs this in a sandbox where pcall is not a
    -- global, and calling it fails the build with "attempt to call a nil value" - which
    -- would be a new way for this step to break a build, in the file added to stop it
    -- breaking installs.
    local ok = false
    local err = nil

    try
    {
        function()
            os.ln(dll_source, dll_target)
            ok = true
        end,
        catch
        {
            function (errors)
                err = errors
            end
        }
    }

    if not ok then
        print("CyberpunkMP.dll could not be linked into zzzCyberpunkMP - the existing install was NOT touched.")
        print("  what   " .. tostring(err))
        print("  fix    turn on Windows Developer Mode (Settings > System > For developers), or run the build from an elevated shell.")
        return "link-failed"
    end

    return "relinked"
end
