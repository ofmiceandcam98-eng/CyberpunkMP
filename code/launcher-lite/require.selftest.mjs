/**
 * require.selftest.mjs - nothing in the launcher's ES modules calls require(), and the six
 * places in main.js that did now reach what they were asking for.
 * Run with:  node require.selftest.mjs
 *
 * WHY THIS EXISTS. package.json says "type": "module", so main.js is an ES module, and in an
 * ES module `require` does not exist. Six functions called it anyway, from 2026-08-13 on:
 *
 *   findUninstallRegistryEntries, findNxmRegistration, findGameDir (the GOG lookup),
 *   steamRoots, fitWindowToRole  - each inside a try whose catch was written for "reg is
 *     not available". The ReferenceError landed in that catch, so every one of them quietly
 *     answered "nothing found", on every machine, for three months.
 *   purgeResidue                 - no try. It threw before removing anything, so Uninstall
 *     left every data folder, shortcut and registry key behind, and Deep clean answered
 *     "require is not defined".
 *
 * `node --check` passes all of it, because the file parses. Nothing ran those lines but a
 * button press on a player's machine.
 *
 * So this has two parts. Part 1 reads every ES module the launcher ships and fails on a call
 * to require. Part 2 lifts the six functions out of main.js as text, the way
 * prereq.selftest.mjs does, and runs them with every dependency NAMED in the call below -
 * execSync, execFileSync and screen among them, require not - so each has to return what its
 * registry answer says. A function that reaches for something main.js never imported
 * returns nothing here, and the check that expects an answer fails.
 *
 * Runs no reg command and touches no registry: the answers are canned. Writes only under the
 * OS temp folder, and removes what it wrote. Needs nothing from node_modules.
 */

import { existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, realpathSync, rmSync, writeFileSync } from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const here = path.dirname(fileURLToPath(import.meta.url))
const read = (name) => readFileSync(path.join(here, name), 'utf8').split('\r\n').join('\n')
const source = read('main.js')

let passed = 0
let failed = 0

function check (name, actual, expected) {
  const a = JSON.stringify(actual)
  const e = JSON.stringify(expected)
  if (a === e) {
    passed++
  } else {
    failed++
    console.log(`FAIL: ${name}\n  expected ${e}\n  got      ${a}`)
  }
}

// ---------------------------------------------------------------------------
// Part 1 - no ES module here calls require.
//
// Every .js and .mjs beside package.json is an ES module (that is what "type": "module"
// means); a .cjs file is CommonJS and may call it. The selftests are not shipped and are
// not read. A file that builds its own with createRequire and binds it to the name is doing
// the supported thing, and is left alone. Whole-line comments are skipped. A mention at the
// end of a code line, or inside a string, is still reported: reword it, because this cannot
// tell it from a call.

const modules = readdirSync(here)
  .filter((name) => /\.(js|mjs)$/.test(name) && !name.endsWith('.selftest.mjs'))
  .sort()
const calls = []

for (const name of modules) {
  const text = read(name)
  if (/\b(?:const|let|var)\s+require\s*=/.test(text)) continue

  text.split('\n').forEach((line, index) => {
    const code = line.trim()
    if (code.startsWith('//') || code.startsWith('*') || code.startsWith('/*')) return
    if (/(?<![.\w$])require\s*\(/.test(code)) calls.push(`${name}:${index + 1}  ${code}`)
  })
}

check('main.js is among the modules this reads', modules.includes('main.js'), true)

if (calls.length) {
  failed++
  console.log(`FAIL: ${calls.length} line(s) call require in an ES module`)
  for (const call of calls) console.log(`  ${call}`)
  console.log('  what:  require does not exist in an ES module. The line throws a ReferenceError the')
  console.log('         first time it runs, and if it sits in a try, the catch hides it for good.')
  console.log('  fix:   import the name at the top of the file instead - for example')
  console.log("         import { execSync } from 'node:child_process' - and delete the line.")
} else {
  passed++
}

// ---------------------------------------------------------------------------
// Part 2 - the six functions, run for real.

// One top-level function of main.js, as text. They all close with a brace in column 0.
function grab (name) {
  const start = source.search(new RegExp(`^(async )?function ${name} `, 'm'))
  if (start < 0) throw new Error(`main.js has no function ${name} - update this selftest`)
  const end = source.indexOf('\n}\n', start)
  return source.slice(start, end + 2)
}

// The named functions with exactly these dependencies in scope, and nothing else of main.js.
function lift (names, deps) {
  const body = names.map(grab).join('\n\n')
  // eslint-disable-next-line no-new-func
  return new Function(...Object.keys(deps), `'use strict'\n${body}\nreturn { ${names.join(', ')} }`)(...Object.values(deps))
}

// A stand-in for `reg query`. Each entry of `answers` is [text the command must contain,
// what reg prints]; a command nothing matches fails the way reg does for a missing key.
function fakeReg (answers) {
  const asked = []
  const execSync = (command) => {
    asked.push(command)
    for (const [needle, output] of answers) {
      if (command.includes(needle)) return Buffer.from(output)
    }
    throw new Error('ERROR: The system was unable to find the specified registry key or value.')
  }
  return { execSync, asked }
}

const sandbox = mkdtempSync(path.join(os.tmpdir(), 'nco-require-selftest-'))

try {
  const place = (...parts) => {
    const full = path.join(sandbox, ...parts)
    mkdirSync(path.dirname(full), { recursive: true })
    writeFileSync(full, 'x')
    return full
  }

  const UNINSTALL = 'HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall'
  const liveUninstaller = place('live', 'Uninstall Night City Online Launcher.exe')
  const goneUninstaller = path.join(sandbox, 'gone', 'Uninstall Night City Online.exe')
  const liveLauncher = place('live', 'Night City Online Launcher.exe')
  const goneLauncher = path.join(sandbox, 'gone', 'Night City Online Launcher.exe')

  const uninstallRows = [
    `${UNINSTALL}\\{live-copy}`,
    '    DisplayName    REG_SZ    Night City Online Launcher 0.3.129',
    `    UninstallString    REG_SZ    "${liveUninstaller}" /currentuser`,
    '',
    `${UNINSTALL}\\{ghost-copy}`,
    '    DisplayName    REG_SZ    Night City Online 0.3.80',
    `    UninstallString    REG_SZ    "${goneUninstaller}" /currentuser`,
    '',
    `${UNINSTALL}\\SomebodyElse`,
    '    DisplayName    REG_SZ    Some Other Product',
    `    UninstallString    REG_SZ    "${path.join(sandbox, 'gone', 'other.exe')}"`,
    ''
  ].join('\r\n')

  const nxmRow = (exe) => `\r\nHKEY_CURRENT_USER\\Software\\Classes\\nxm\\shell\\open\\command\r\n    (Default)    REG_SZ    "${exe}" "%1"\r\n`
  const NXM_KEY = 'HKCU\\Software\\Classes\\nxm'

  // What main.js gives these functions, and no more. samePath is main.js's own.
  const residueDeps = (reg, extra = {}) => ({ path, existsSync, realpathSync, rmSync, process, execSync: reg.execSync, ...extra })
  const RESIDUE = ['samePath', 'findUninstallRegistryEntries', 'findNxmRegistration']

  // --- findUninstallRegistryEntries -------------------------------------------------------
  {
    const reg = fakeReg([['CurrentVersion\\Uninstall', uninstallRows]])
    const { findUninstallRegistryEntries } = lift(RESIDUE, residueDeps(reg))

    check('the uninstall rows that are ours are found, and only the one whose uninstaller is gone is orphaned',
      findUninstallRegistryEntries(),
      [
        { key: `${UNINSTALL}\\{live-copy}`, displayName: 'Night City Online Launcher 0.3.129', orphaned: false },
        { key: `${UNINSTALL}\\{ghost-copy}`, displayName: 'Night City Online 0.3.80', orphaned: true }
      ])
    check('it asked reg exactly once', reg.asked.length, 1)

    const none = fakeReg([])
    check('no answer from reg is no rows, not a throw',
      lift(RESIDUE, residueDeps(none)).findUninstallRegistryEntries(), [])
  }

  // --- findNxmRegistration ----------------------------------------------------------------
  {
    const live = lift(RESIDUE, residueDeps(fakeReg([['Classes\\nxm', nxmRow(liveLauncher)]]))).findNxmRegistration()
    check('a handler pointing at a launcher that exists is ours and alive',
      live, { key: NXM_KEY, exe: liveLauncher, ours: true, dead: false })

    const dead = lift(RESIDUE, residueDeps(fakeReg([['Classes\\nxm', nxmRow(goneLauncher)]]))).findNxmRegistration()
    check('a handler pointing at a launcher that is gone is ours and dead',
      dead, { key: NXM_KEY, exe: goneLauncher, ours: true, dead: true })

    const vortex = place('vortex', 'Vortex.exe')
    const theirs = lift(RESIDUE, residueDeps(fakeReg([['Classes\\nxm', nxmRow(vortex)]]))).findNxmRegistration()
    check("another mod manager's handler is not ours",
      theirs, { key: NXM_KEY, exe: vortex, ours: false, dead: false })

    check('no handler registered is null',
      lift(RESIDUE, residueDeps(fakeReg([]))).findNxmRegistration(), null)
  }

  // --- purgeResidue -----------------------------------------------------------------------
  {
    const staleDir = path.join(sandbox, 'stale-data')
    place('stale-data', 'settings.json')
    place('stale-data', 'nested', 'cache.bin')
    const staleFile = place('desktop', 'NightCityOnline-CRASH-1.log')
    const keptFile = place('desktop', 'keep-me.txt')

    const deleted = []
    const refuse = `${UNINSTALL}\\{locked}`
    const execFileSync = (file, args) => {
      deleted.push([file, ...args])
      if (args[1] === refuse) throw new Error('ERROR: Access is denied.')
    }
    // The rescan inside purgeResidue sees the ghost row and a dead handler of ours.
    const reg = fakeReg([['CurrentVersion\\Uninstall', uninstallRows], ['Classes\\nxm', nxmRow(goneLauncher)]])
    const { purgeResidue } = lift([...RESIDUE, 'purgeResidue'], residueDeps(reg, { execFileSync }))

    let outcome
    try {
      outcome = purgeResidue({ dirs: [staleDir], files: [staleFile], regKeys: [`${UNINSTALL}\\{asked-for}`, refuse] })
    } catch (err) {
      outcome = `threw ${err.name}: ${err.message}`
    }

    check('purgeResidue reports only the key that refused', outcome, [refuse])
    check('the folder it was given is gone', existsSync(staleDir), false)
    check('the file it was given is gone', existsSync(staleFile), false)
    check('a file it was not given is untouched', existsSync(keptFile), true)
    check('each key goes through reg delete as its own argument, the rescanned ghost row and dead handler included',
      deleted,
      [
        ['reg', 'delete', `${UNINSTALL}\\{asked-for}`, '/f'],
        ['reg', 'delete', refuse, '/f'],
        ['reg', 'delete', `${UNINSTALL}\\{ghost-copy}`, '/f'],
        ['reg', 'delete', NXM_KEY, '/f']
      ])
  }

  // --- steamRoots -------------------------------------------------------------------------
  {
    const reg = fakeReg([['Valve\\Steam', '\r\nHKEY_CURRENT_USER\\Software\\Valve\\Steam\r\n    SteamPath    REG_SZ    d:/games/steam\r\n']])
    const { steamRoots } = lift(['steamRoots'], { execSync: reg.execSync })

    check('the Steam folder the registry names comes first, before the two guesses',
      steamRoots(), ['d:\\games\\steam', 'C:\\Program Files (x86)\\Steam', 'C:\\Program Files\\Steam'])

    check('no Steam in the registry leaves the two guesses',
      lift(['steamRoots'], { execSync: fakeReg([]).execSync }).steamRoots(),
      ['C:\\Program Files (x86)\\Steam', 'C:\\Program Files\\Steam'])
  }

  // --- findGameDir: the GOG lookup ---------------------------------------------------------
  {
    // Only the GOG path holds a game here: no saved folder, no Steam, no Epic, and no disk
    // but the one this test describes - so the machine running it cannot change the answer.
    const gogDir = path.join(sandbox, 'gog', 'Cyberpunk 2077')
    const gogExe = path.join(gogDir, 'bin', 'x64', 'Cyberpunk2077.exe')
    const reg = fakeReg([['WOW6432Node\\GOG.com\\Games\\1423049311', `\r\n    path    REG_SZ    ${gogDir}\r\n`]])

    const { findGameDir } = lift(['findGameDir'], {
      path,
      process,
      execSync: reg.execSync,
      loadSettings: () => ({}),
      existsSync: (candidate) => candidate === gogExe,
      steamRoots: () => [],
      readFileSync: () => { throw new Error('no such file') },
      readdirSync: () => { throw new Error('no such folder') }
    })

    check('a GOG install is found where the registry says it is', findGameDir(), gogDir)
    check('both GOG keys were asked', reg.asked.length, 2)
  }

  // --- fitWindowToRole --------------------------------------------------------------------
  {
    const fit = (admin, startHeight, workHeight) => {
      const sizes = []
      const mainWindow = {
        isDestroyed: () => false,
        getBounds: () => ({ x: 0, y: 0, width: 1180, height: startHeight }),
        getSize: () => [1180, startHeight],
        setSize: (width, height) => sizes.push([width, height])
      }
      const screen = { getDisplayMatching: () => ({ workAreaSize: { width: 1920, height: workHeight } }) }
      const { fitWindowToRole } = lift(['fitWindowToRole'], { mainWindow, screen, isAdmin: () => admin, lastAutoHeight: 0 })
      fitWindowToRole()
      return sizes
    }

    check('an admin on a tall screen gets the taller window', fit(true, 800, 1040), [[1180, 940]])
    check('an admin on a short screen gets what fits', fit(true, 800, 900), [[1180, 876]])
    check('a player at the default height is left alone', fit(false, 800, 1040), [])
  }
} finally {
  rmSync(sandbox, { recursive: true, force: true })
}

console.log(`${passed} passed, ${failed} failed`)
if (failed) {
  console.log('A function that returns nothing here is usually reaching for a name main.js never')
  console.log('imported: look inside its try block. Run it from code\\launcher-lite: node require.selftest.mjs')
}
process.exit(failed ? 1 : 0)
