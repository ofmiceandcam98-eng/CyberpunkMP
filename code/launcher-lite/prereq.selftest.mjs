/**
 * prereq.selftest.mjs - runs the REAL framework-update code out of main.js, without Electron.
 * Run with:  node prereq.selftest.mjs
 *
 * manifest.selftest.mjs covers the pure helpers, but the code that calls them lives in main.js,
 * which cannot be imported outside Electron. That gap has a history: installFull shipped a read
 * of a never-bound identifier TWICE, because `node --check` only proves a file parses and the
 * line only ran on a button press. So this lifts the functions out of main.js as text and runs
 * them with every dependency named in deps() below - which makes an undeclared identifier in
 * them a ReferenceError here, instead of one on a player's machine.
 *
 * Writes only under the OS temp folder, and reaches no network: the downloads are stubbed.
 */

import crypto from 'node:crypto'
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import os from 'node:os'
import path from 'node:path'

import * as ManifestKit from './manifest.js'

const AdmZip = createRequire(import.meta.url)('adm-zip')
const source = readFileSync(new URL('./main.js', import.meta.url), 'utf8').split('\r\n').join('\n')

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

async function rejects (name, promise, expectedText) {
  try {
    await promise
    failed++
    console.log(`FAIL: ${name}\n  expected a refusal mentioning "${expectedText}", and it succeeded`)
  } catch (err) {
    if (err instanceof ReferenceError) {
      failed++
      console.log(`FAIL: ${name}\n  undeclared identifier in main.js: ${err.message}`)
    } else {
      check(name, err.message.includes(expectedText) ? expectedText : err.message, expectedText)
    }
  }
}

// One top-level function of main.js, as text. They all close with a brace in column 0.
function grab (name) {
  const start = source.search(new RegExp(`^(async )?function ${name} `, 'm'))
  if (start < 0) throw new Error(`main.js has no function ${name} - update this selftest`)
  const end = source.indexOf('\n}\n', start)
  return source.slice(start, end + 2)
}

const FUNCTIONS = ['missingPrerequisites', 'stalePrerequisites', 'bundledOwnerKey',
  'downloadFullInstallPackage', 'bringPrerequisitesToPin', 'checkForUpdates']

function launcher ({ manifest = null, freshManifest = null, pkg = null, settings = {}, release = null, gameDir = null, modDir = 'mod-dir' }) {
  const state = { manifest, settings, downloads: 0, forced: 0, log: [] }
  const deps = {
    ManifestKit,
    AdmZip,
    path,
    existsSync,
    GITHUB_REPO: 'owner/repo',
    RELEASE_API: 'release-api',
    axios: {
      get: async (url) => {
        if (url === 'release-api') {
          if (!release) throw new Error('offline')
          return { data: release }
        }
        state.downloads++
        return { data: pkg }
      }
    },
    usableManifest: () => state.manifest,
    refreshManifestState: async (force) => {
      if (force) {
        state.forced++
        if (freshManifest) state.manifest = freshManifest
      }
    },
    loadSettings: () => state.settings,
    saveSettings: (patch) => Object.assign(state.settings, patch),
    launcherLog: (line) => state.log.push(line),
    findModDir: () => modDir,
    findGameDir: () => gameDir,
    describeLauncherUpdate: () => null
  }
  const body = FUNCTIONS.map(grab).join('\n\n')
  // eslint-disable-next-line no-new-func
  const fns = new Function(...Object.keys(deps), `'use strict'\n${body}\nreturn { ${FUNCTIONS.join(', ')} }`)(...Object.values(deps))
  return { ...fns, state }
}

function innerZip (files) {
  const zip = new AdmZip()
  for (const [rel, content] of Object.entries(files)) zip.addFile(rel, Buffer.from(content))
  return zip.toBuffer()
}

// A package the size guard accepts: 1.1 MB of noise, because zeros would compress away.
function makePackage (zips) {
  const zip = new AdmZip()
  zip.addFile('padding.bin', crypto.randomBytes(1100 * 1024))
  for (const [name, bytes] of Object.entries(zips)) zip.addFile(`prerequisites/${name}`, bytes)
  return zip.toBuffer()
}

function component (id, name, archiveName, bytes, version = '2.0.0') {
  return { id, name, class: 'bundled', version, archive: { name: archiveName, sha256: ManifestKit.sha256Hex(bytes) } }
}

function place (dir, rel, content) {
  const full = path.join(dir, rel.split('/').join(path.sep))
  mkdirSync(path.dirname(full), { recursive: true })
  writeFileSync(full, content)
}

const read = (dir, rel) => readFileSync(path.join(dir, rel.split('/').join(path.sep)), 'utf8')

const ALPHA_V2 = innerZip({ 'red4ext/plugins/Alpha/Alpha.dll': 'alpha v2' })
const BETA_V2 = innerZip({ 'red4ext/plugins/Beta/Beta.dll': 'beta v2' })
const manifestV2 = {
  components: [
    { id: 'cyberpunk_multiplayer', name: 'CyberpunkMP', class: 'payload' },
    component('alpha', 'Alpha', 'Alpha-2.0.0.zip', ALPHA_V2),
    component('beta', 'Beta', 'Beta-2.0.0.zip', BETA_V2)
  ]
}

async function withGame (fn) {
  const dir = mkdtempSync(path.join(os.tmpdir(), 'nco-prereq-'))
  try {
    await fn(dir)
  } finally {
    rmSync(dir, { recursive: true, force: true })
  }
}

await withGame(async (game) => {
  const l = launcher({ manifest: null })
  check('no manifest: nothing to compare, so nothing is touched or downloaded',
    [await l.bringPrerequisitesToPin(game), l.state.downloads], [[], 0])
})

await withGame(async (game) => {
  // The player the bug was about: an old framework, an old file it no longer ships, no record of pins.
  place(game, 'red4ext/plugins/Alpha/Alpha.dll', 'alpha v1')
  place(game, 'red4ext/plugins/Alpha/Retired.reds', 'old script')
  place(game, 'red4ext/plugins/Alpha/player-notes.txt', 'theirs')
  place(game, 'red4ext/plugins/Beta/Beta.dll', 'beta v2')

  const settings = { bundledFiles: { Alpha: ['red4ext/plugins/Alpha/Alpha.dll', 'red4ext/plugins/Alpha/Retired.reds'] } }
  const steps = []
  const l = launcher({ manifest: manifestV2, pkg: makePackage({ 'Alpha-2.0.0.zip': ALPHA_V2, 'Beta-2.0.0.zip': BETA_V2 }), settings })

  check('an install with no record reads stale', l.stalePrerequisites(game).map((c) => c.id), ['alpha', 'beta'])
  const updated = await l.bringPrerequisitesToPin(game, (s) => steps.push(s))

  check('only the framework that differed is reported as updated', updated, ['Alpha 2.0.0'])
  check('the old framework is replaced on disk', read(game, 'red4ext/plugins/Alpha/Alpha.dll'), 'alpha v2')
  check('the file the new version dropped is gone', existsSync(path.join(game, 'red4ext', 'plugins', 'Alpha', 'Retired.reds')), false)
  check('a file the player added is left alone', read(game, 'red4ext/plugins/Alpha/player-notes.txt'), 'theirs')
  check('both frameworks are recorded at their pins', settings.bundledArchives,
    { alpha: manifestV2.components[1].archive.sha256, beta: manifestV2.components[2].archive.sha256 })
  check('what the framework owns is updated, so a Nexus install still cannot overwrite it',
    settings.bundledFiles.Alpha, ['red4ext/plugins/Alpha/Alpha.dll'])
  check('and it says what it is doing', steps.includes('Checking Alpha 2.0.0...'), true)

  check('once proven it reads current', l.stalePrerequisites(game), [])
  const downloads = l.state.downloads
  check('a second Update changes nothing and downloads nothing',
    [await l.bringPrerequisitesToPin(game), l.state.downloads], [[], downloads])
})

await withGame(async (game) => {
  // missingPrerequisites names the six real frameworks, so prove the stale path with a real one.
  const r4 = innerZip({ 'bin/x64/winmm.dll': 'red4ext' })
  const manifest = { components: [component('red4ext', 'RED4ext', 'RED4ext-1.31.0.zip', r4)] }
  const settings = { bundledArchives: { red4ext: manifest.components[0].archive.sha256 } }
  const l = launcher({ manifest, pkg: makePackage({ 'RED4ext-1.31.0.zip': r4 }), settings })
  check('proven at its pin but gone from disk reads stale', l.stalePrerequisites(game).map((c) => c.id), ['red4ext'])
  check('and Update puts it back', [await l.bringPrerequisitesToPin(game), read(game, 'bin/x64/winmm.dll')], [['RED4ext 2.0.0'], 'red4ext'])
})

await withGame(async (game) => {
  // A memoized manifest older than a republished package must not fail a good download.
  const alphaV3 = innerZip({ 'red4ext/plugins/Alpha/Alpha.dll': 'alpha v3' })
  const fresh = { components: [component('alpha', 'Alpha', 'Alpha-3.0.0.zip', alphaV3, '3.0.0')] }
  const l = launcher({ manifest: manifestV2, freshManifest: fresh, pkg: makePackage({ 'Alpha-3.0.0.zip': alphaV3 }), settings: {} })
  check('a stale memo buys a forced re-fetch, and the fresh manifest decides',
    [await l.bringPrerequisitesToPin(game), l.state.forced, read(game, 'red4ext/plugins/Alpha/Alpha.dll')],
    [['Alpha 3.0.0'], 1, 'alpha v3'])
})

await withGame(async (game) => {
  // Tampered: the fresh manifest still disagrees. Nothing may land, including the framework that was fine.
  place(game, 'red4ext/plugins/Alpha/Alpha.dll', 'alpha v1')
  const settings = {}
  const l = launcher({
    manifest: manifestV2,
    pkg: makePackage({ 'Alpha-2.0.0.zip': ALPHA_V2, 'Beta-2.0.0.zip': innerZip({ 'red4ext/plugins/Beta/Beta.dll': 'tampered' }) }),
    settings
  })
  await rejects('a zip the signed manifest did not approve is refused', l.bringPrerequisitesToPin(game), 'Nothing was changed')
  check('and nothing landed, even the framework that was approved', read(game, 'red4ext/plugins/Alpha/Alpha.dll'), 'alpha v1')
  check('and nothing was recorded', settings.bundledArchives, undefined)
})

await withGame(async (game) => {
  const l = launcher({ manifest: manifestV2, pkg: makePackage({ 'Alpha-2.0.0.zip': ALPHA_V2 }) })
  await rejects('a package missing a pinned framework is refused by name', l.bringPrerequisitesToPin(game), 'Beta-2.0.0.zip')
})

await withGame(async (game) => {
  // Part way through: a directory squats where Beta's DLL must go. Alpha stays done and recorded.
  mkdirSync(path.join(game, 'red4ext', 'plugins', 'Beta', 'Beta.dll'), { recursive: true })
  const settings = {}
  const l = launcher({ manifest: manifestV2, pkg: makePackage({ 'Alpha-2.0.0.zip': ALPHA_V2, 'Beta-2.0.0.zip': BETA_V2 }), settings })
  await rejects('a framework that cannot land stops the update by name', l.bringPrerequisitesToPin(game), 'Beta 2.0.0')
  check('what landed stays recorded, so the next Update picks up only the rest',
    Object.keys(settings.bundledArchives), ['alpha'])
})

await withGame(async (game) => {
  // What the UI gates on.
  const release = { tag_name: 'v9', assets: [{ name: 'ModPayload.zip', id: 1, size: 2, browser_download_url: 'payload-url' }] }
  const proven = { alpha: manifestV2.components[1].archive.sha256, beta: manifestV2.components[2].archive.sha256 }
  const ask = (settings, over = {}) => launcher({ manifest: manifestV2, release, settings, gameDir: game, ...over }).checkForUpdates()
  const shape = (r) => [r.upToDate, r.payloadCurrent, r.prerequisitesStale]

  check('mod current, frameworks proven: up to date',
    shape(await ask({ installedStamp: '1:2', bundledArchives: proven })), [true, true, []])
  check('mod current, frameworks not: Update is offered, and says why',
    shape(await ask({ installedStamp: '1:2' })), [false, true, ['Alpha', 'Beta']])
  check('mod behind, frameworks proven: the ordinary update',
    shape(await ask({ installedStamp: '0:0', bundledArchives: proven })), [false, false, []])
  check('a test build stays current: JACK IN is not greyed out and Update would refuse anyway',
    shape(await ask({ installedStamp: '0:0', testBuildTag: 'v9-test.1' })), [true, false, ['Alpha', 'Beta']])
  check('no manifest behaves as it always did',
    shape(await ask({ installedStamp: '1:2' }, { manifest: null })), [true, true, []])
  const offline = await launcher({ manifest: manifestV2, release: null, settings: {}, gameDir: game }).checkForUpdates()
  check('an outage does not block play', [offline.upToDate, offline.offline], [true, true])
})

{
  // Ordering in applyUpdate, which needs too much of Electron to run here.
  const apply = grab('applyUpdate')
  const at = (needle) => apply.indexOf(needle)
  check('applyUpdate: the test-build refusal comes before any framework work',
    at('activeTestTag') > -1 && at('activeTestTag') < at('bringPrerequisitesToPin'), true)
  check('applyUpdate: the game-running refusal comes before any framework work',
    at('isGameRunning') > -1 && at('isGameRunning') < at('bringPrerequisitesToPin'), true)
  check('applyUpdate: frameworks are settled before the payload is touched',
    at('bringPrerequisitesToPin') > -1 && at('bringPrerequisitesToPin') < at('extractPayloadClean'), true)
  // The main screen prints these on the Update button's wire; without them a mod-only update is silent.
  check('applyUpdate: the download says so before it starts',
    at("onProgress('Downloading the update...')") > -1 && at("onProgress('Downloading the update...')") < at('axios.get'), true)
  check('applyUpdate: the install says so before it starts',
    at("onProgress('Installing the update...')") > -1 && at("onProgress('Installing the update...')") < at('extractPayloadClean'), true)
}

console.log(`\n${passed} passed, ${failed} failed`)
process.exitCode = failed === 0 ? 0 : 1
