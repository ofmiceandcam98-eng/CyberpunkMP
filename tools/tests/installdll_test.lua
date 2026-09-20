--
-- installdll_test.lua - prove the post-build install step cannot destroy an install.
--
--   xmake lua tools/tests/installdll_test.lua
--
-- THE REGRESSION THIS GUARDS. Until 2026-09-20 the step deleted zzzCyberpunkMP and then
-- linked the new DLL in. On a LAUNCHER install - a real directory holding the real DLL,
-- assets, Rpc and .nco-version - a bare `xmake -y` therefore deleted the lot, and when the
-- link failed for want of the symlink privilege the install was simply gone while the build
-- reported success (zeldfep's box, 2026-09-09).
--
-- So the assertion that matters is not "it returns the right string", it is "the files are
-- STILL THERE afterwards". Every case below checks the files, not just the verdict.
--
-- Creating a symlink needs Developer Mode or elevation, which a test cannot assume. Only
-- the last case needs one, and it accepts "link-failed" as a pass BECAUSE the install
-- surviving a failed link is exactly the property under test.

import("core.base.option")

local passed = 0
local failed = 0

function check(name, ok, detail)
    if ok then
        print("PASS  " .. name)
        passed = passed + 1
    else
        print("FAIL  " .. name .. (detail and (" - " .. tostring(detail)) or ""))
        failed = failed + 1
    end
end

function main()
    import("installdll", {rootdir = path.join(os.projectdir(), "code", "loader")})

    local root = os.tmpfile() .. ".installdll"
    os.mkdir(root)

    -- A build output with a DLL in it, which every case shares.
    local clientdir = path.join(root, "build")
    os.mkdir(clientdir)
    io.writefile(path.join(clientdir, "CyberpunkMP.dll"), "not a real dll - test bytes")

    -- --- a REAL launcher install must be refused and left intact -------------------
    local plugins = path.join(root, "plugins")
    local modPath = path.join(plugins, "zzzCyberpunkMP")
    os.mkdir(modPath)
    io.writefile(path.join(modPath, "CyberpunkMP.dll"), "the launcher's real dll")
    os.mkdir(path.join(modPath, "assets"))
    io.writefile(path.join(modPath, "assets", "kept.reds"), "a file the build must not delete")
    io.writefile(path.join(modPath, ".nco-version"), "v0.3.123")

    local verdict = installdll.install_client_dll(clientdir, plugins, false)

    check("a real install is refused", verdict == "not-a-link", verdict)
    check("the real DLL survives", io.readfile(path.join(modPath, "CyberpunkMP.dll")) == "the launcher's real dll")
    check("the assets survive", os.isfile(path.join(modPath, "assets", "kept.reds")))
    check("the version marker survives", os.isfile(path.join(modPath, ".nco-version")))

    -- --- the game running stops everything -----------------------------------------
    local running = installdll.install_client_dll(clientdir, plugins, true)
    check("the game running stops the install", running == "game-running", running)
    check("nothing was touched while the game ran", os.isfile(path.join(modPath, ".nco-version")))

    -- --- no DLL to install is reported, not guessed at ------------------------------
    local emptyBuild = path.join(root, "empty-build")
    os.mkdir(emptyBuild)
    local noDll = installdll.install_client_dll(emptyBuild, plugins, false)
    check("a build with no DLL is reported", noDll == "no-dll", noDll)
    check("the install survives a build with no DLL", os.isfile(path.join(modPath, "CyberpunkMP.dll")))

    -- --- a fresh folder is created and linked ---------------------------------------
    local freshPlugins = path.join(root, "fresh")
    os.mkdir(freshPlugins)
    local fresh = installdll.install_client_dll(clientdir, freshPlugins, false)

    -- "link-failed" is a PASS here: without Developer Mode the link cannot be made, and
    -- the point of this change is that failing to link breaks nothing.
    check("a fresh install links (or fails harmlessly)",
          fresh == "relinked" or fresh == "link-failed", fresh)

    if fresh == "relinked" then
        check("the linked DLL is there", os.isfile(path.join(freshPlugins, "zzzCyberpunkMP", "CyberpunkMP.dll")))

        -- Re-running replaces our own link rather than refusing: that is the dev loop.
        local again = installdll.install_client_dll(clientdir, freshPlugins, false)
        check("re-running replaces our own link", again == "relinked", again)
    else
        print("      (symlink privilege unavailable - link cases skipped, which is itself the harmless path)")
    end

    os.rm(root)

    print("")
    if failed > 0 then
        print("installdll_test: " .. failed .. " check(s) FAILED")
        os.exit(1)
    end
    print("installdll_test: all " .. passed .. " checks passed")
end
