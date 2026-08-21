/**
 * main.js - Electron main process.
 *
 * Owns three things:
 *   1. The window.
 *   2. The Discord OAuth2 handshake (including a throwaway local HTTP server
 *      that catches the redirect).
 *   3. Launching the game.
 *
 * The access token NEVER leaves this file. The renderer is sent only a display
 * profile - name, avatar, id. If the UI were ever compromised (a bad dependency,
 * an injected script), there would be no credential sitting there to steal.
 */

import { app, BrowserWindow, clipboard, dialog, ipcMain, safeStorage, shell } from 'electron'
import electronUpdater from 'electron-updater'
import { spawn, spawnSync } from 'node:child_process'
import { existsSync, readFileSync, writeFileSync, mkdirSync, readdirSync, rmSync, statSync, copyFileSync, unlinkSync, realpathSync, appendFileSync } from 'node:fs'
import fsp from 'node:fs/promises'
import AdmZip from 'adm-zip'
// 7-Zip's standalone extractor, because Nexus main files are as often .7z or .rar as
// .zip - AdmZip alone made every non-zip mod fail with "No END header found" (live,
// 2026-08-21: Fast Launch). The binary is asar-unpacked so it can actually execute.
import { path7za } from '7zip-bin'
// RAR is the one format 7za genuinely cannot read (exit 2, live within the hour of
// shipping it - Fast Launch is a .rar). Full 7-Zip could, but does not ship as a
// clean binary; unrar-as-WASM does the one legal thing the unRAR license allows -
// extraction - with no binary and no asar gymnastics.
import { createExtractorFromData } from 'node-unrar-js'
const SEVEN_ZIP = path7za.replace('app.asar', 'app.asar.unpacked')
import crypto from 'node:crypto'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import axios from 'axios'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

// Your Discord application's Client ID. This is PUBLIC - it identifies the app,
// it does not authorise anything, and it is fine to commit.
//
// There is deliberately no CLIENT SECRET here. See the PKCE note below.
const DISCORD_CLIENT_ID = '1536256811706089512'

// Must match a redirect URI registered in the Discord Developer Portal
// (OAuth2 -> Redirects). The port is fixed because Discord will only redirect
// to a URI it has seen before - it cannot be chosen at random at runtime.
const CALLBACK_PORT = 53682
const REDIRECT_URI = `http://127.0.0.1:${CALLBACK_PORT}/callback`

// 'identify' gives the user's id, username and avatar. 'guilds.members.read' lets
// the SERVER ask whether this user is in Night City Online - it reads membership
// of one guild for this user only, not their server list, and needs no bot.
//
// Nothing more than this. Users see the list on the consent screen, and a long
// one makes people (rightly) suspicious.
const SCOPES = ['identify', 'guilds.members.read']

// The Discord server players must be in, and where to send them if they are not.
const GUILD_ID = '1536257549832167506'
const DISCORD_INVITE = 'https://discord.gg/M9NSWsndC7'

// Discord ids that see the server controls.
//
// This is presentation, NOT access control. The panel starts a process on the
// user's own machine - anyone could run Server.Loader.exe by hand regardless, so
// there is no privilege here to protect. It exists to keep controls that would
// only confuse players out of their way.
//
// Put your own id here. Sign in once and the launcher shows it under your name.
const ADMIN_DISCORD_IDS = [
  '566025915839283220',  // Cam
  // zeldfep - the other half of the project. Added after the second live "where did
  // my server buttons go": a transient role-lookup failure (expired OAuth token, or
  // fetching roles.json mid-release-upload) silently demoted him to player, while
  // Cam never noticed because this floor already carried him. Both owners belong on
  // the floor; the role map governs everyone else.
  '226974251045879808'
]

const SERVER_EXE = 'Server.Loader.exe'

// Where the server lives.
//
// This CANNOT be derived from __dirname: in a packaged build that points inside the app
// bundle, not the repository, so the relative path resolves to nothing and every server
// button silently greys out. It is a saved setting instead, with the dev-tree path only
// as a starting guess.
function defaultServerDir () {
  if (process.env.MP_SERVER_DIR) return process.env.MP_SERVER_DIR

  const devGuess = path.resolve(__dirname, '..', '..', 'build', 'windows', 'x64', 'release')
  if (existsSync(path.join(devGuess, SERVER_EXE))) return devGuess

  return 'C:\\Users\\Cam\\OneDrive\\Documents\\GitHub\\CyberpunkMP\\build\\windows\\x64\\release'
}

// -skipStartScreen is the game's own documented launch option: boot lands on the main
// menu instead of the "press any key" breach screen. Testers relaunch constantly, and
// every screen between double-click and MULTIPLAYER is dead time.
// ============================= HARDCODED BOOT POLICY =============================
// The game boots STRAIGHT TO THE MENU. Crew decree, 2026-08-21: "skip the opening
// scenes and jump to the menu, hard code it and make note to not lose that for the
// future." Two halves, and BOTH stay:
//   1. -skipStartScreen below - kills the press-any-key screen.
//   2. The Fast Launch mod (Nexus 5186) - kills the intro videos. It cannot be marked
//      required in modlist.json (required blocks Play, and nobody gets locked out of
//      the server over a cosmetic), so ensureFastLaunch() installs it by itself
//      whenever it is missing and a Nexus key makes that possible.
// Removing either half regresses the boot to logo-watching. Do not.
const GAME_ARGS = ['-skipStartScreen']
const FAST_LAUNCH_MOD_ID = 5186

// Install one Nexus mod DIRECTLY into the game folder - the path this launcher owns
// end to end (installModArchive extracts it and records every file it places).
// Nexus permits fully-automated downloads only for PREMIUM API keys - their policy,
// not a gap here, and working around it would breach their API terms. A free key
// gets { needsWebsite: true } and the website's "Mod Manager Download" button, which
// routes straight back through our nxm:// handler and lands in the same
// launcher-owned path. Either road, same destination.
async function installNexusMod (modId) {
  const installed = loadInstalledMods()
  if (installed && (installed[modId] || installed[String(modId)])) return { ok: true, already: true }

  const apiKey = loadNexusKey()
  if (!apiKey) return { needsWebsite: true, reason: 'no Nexus key saved' }

  const headers = { apikey: apiKey, 'User-Agent': 'NightCityOnline-Launcher/1.0' }

  const files = await axios.get(
    `https://api.nexusmods.com/v1/games/cyberpunk2077/mods/${modId}/files.json?category=main`,
    { headers, timeout: 15000 })
  const fileId = files.data?.files?.[0]?.file_id
  if (!fileId) return { needsWebsite: true, reason: 'no main file listed' }

  let url = null
  try {
    const link = await axios.get(
      `https://api.nexusmods.com/v1/games/cyberpunk2077/mods/${modId}/files/${fileId}/download_link.json`,
      { headers, timeout: 15000 })
    url = link.data?.[0]?.URI
  } catch (err) {
    // 403 is Nexus saying "free account" - direct links are premium-only.
    if (err.response?.status === 403) return { needsWebsite: true, reason: 'free Nexus account' }
    throw err
  }
  if (!url) return { needsWebsite: true, reason: 'no download link' }

  const file = await axios.get(url, { responseType: 'arraybuffer', timeout: 300000 })
  const result = await installModArchive(modId, Buffer.from(file.data))
  return { ok: true, ...result }
}

async function ensureFastLaunch () {
  try {
    const result = await installNexusMod(FAST_LAUNCH_MOD_ID)
    if (result.ok && !result.already)
      launcherLog('boot policy: Fast Launch auto-installed - intro videos are gone from the next boot')
    else if (result.needsWebsite)
      launcherLog(`boot policy: Fast Launch auto-install skipped (${result.reason})`)
  } catch (err) {
    // Never a blocker and never a nag; the policy converges when it can.
    launcherLog(`boot policy: Fast Launch auto-install skipped (${err.response?.status || err.message})`)
  }
}

// Settings > "Direct install from Nexus": every mod on the server's list that is
// missing here, in one click - installed straight into the game folder on a premium
// key, or handed one page at a time to the website's Mod Manager Download button
// (which comes right back through our nxm:// handler) on a free one.
ipcMain.handle('mods:installMissing', async () => {
  try {
    const mods = await fetchModList()
    const level = await refreshUserLevel().catch(() => 'player')
    const isStaff = (LEVELS[level] || 0) >= LEVELS.admin || isAdmin()

    const done = []
    const manual = []
    const failed = []

    for (const mod of mods) {
      if (mod.devOnly && !isStaff) continue
      try {
        const result = await installNexusMod(mod.nexusModId)
        if (result.ok && !result.already) done.push(mod.nexusModId)
        else if (result.needsWebsite) manual.push(mod.nexusModId)
      } catch (err) {
        failed.push(`${mod.nexusModId} (${err.response?.status || err.message})`)
      }
    }

    if (manual.length) {
      // One page at a time - a browser volley of every missing mod is chaos. The nxm
      // handler installs each as its button is pressed; running this again picks up
      // where the person left off.
      shell.openExternal(`https://www.nexusmods.com/cyberpunk2077/mods/${manual[0]}?tab=files`)
    }

    const parts = []
    if (done.length) parts.push(`installed ${done.length} directly`)
    if (manual.length) parts.push(`${manual.length} need one click on Nexus (page opened - press "Mod Manager Download", it lands here automatically)`)
    if (failed.length) parts.push(`failed: ${failed.join(', ')}`)

    return { ok: !failed.length, message: parts.length ? parts.join('; ') : 'Everything on the list is already installed.' }
  } catch (err) {
    return { ok: false, message: err.message }
  }
})

// Where the game server is.
//
// Resolved in this order, first one wins:
//
//   1. what the player set in Settings        - for testing, or a different server
//   2. server.json published on the release   - what everyone actually uses
//   3. MP_SERVER in the environment           - kept for whoever was relying on it
//   4. 127.0.0.1                              - only correct if you ARE the host
//
// This used to be (3) or (4) with nothing in between, which meant the address existed on
// exactly one machine. Every other player's launcher pinged their own PC, found nothing,
// reported the server offline and refused to let them play - a hard block on everyone but
// the host, with no error explaining it.
let publishedServer = null

async function fetchPublishedServer () {
  if (publishedServer) return publishedServer

  try {
    // Built HERE, not at module scope. GITHUB_REPO is declared further down this file,
    // and a const cannot be read before its declaration runs - doing so throws
    // "Cannot access before initialization" during module load, which means the launcher
    // does not start at all. A crash at import time shows as a raw Electron error dialog
    // with a stack trace, not as anything a player can act on.
    const response = await axios.get(`https://github.com/${GITHUB_REPO}/releases/latest/download/server.json`, {
      headers: { 'User-Agent': 'NightCityOnline-Launcher' },
      timeout: 10000
    })

    if (response.data?.host) publishedServer = response.data
  } catch {
    // Unreachable is survivable - a saved or environment address may still work.
  }

  return publishedServer
}

async function resolveServer () {
  const settings = loadSettings()

  if (settings.serverHost) {
    return { host: settings.serverHost, port: settings.serverPort || 11778, source: 'settings' }
  }

  const published = await fetchPublishedServer()
  if (published) {
    return { host: published.host, port: published.port || 11778, source: 'published' }
  }

  if (process.env.MP_SERVER) {
    return { host: process.env.MP_SERVER, port: process.env.MP_PORT || 11778, source: 'environment' }
  }

  return { host: '127.0.0.1', port: 11778, source: 'fallback' }
}

/**
 * Is a game server actually listening on this machine?
 *
 * Asked only when address resolution has fallen all the way through to 127.0.0.1. That
 * address is correct for exactly one person - whoever is hosting - and wrong for everybody
 * else, and the two cases are indistinguishable from the launcher's point of view until
 * you look at whether anything is there.
 *
 * UDP, because that is what the game server binds. A TCP probe finds nothing even on a
 * perfectly healthy host and would turn every local session into a false alarm.
 *
 * Treated as "yes" if the check itself fails. Being unable to read the socket table is not
 * evidence that nobody is hosting, and guessing "no" there would block a launch that would
 * have worked.
 */
function isServerListeningLocally (port) {
  return new Promise((resolve) => {
    try {
      const check = spawn('netstat', ['-ano', '-p', 'UDP'], { windowsHide: true })

      let out = ''
      check.stdout.on('data', (c) => { out += c.toString() })
      check.on('error', () => resolve(true))
      check.on('close', () => {
        // Matches ":11778" at the end of a local-address column, so port 117780 or a
        // remote address that merely contains the digits cannot be mistaken for it.
        resolve(new RegExp(`:${port}\\b`).test(out))
      })
    } catch {
      resolve(true)
    }
  })
}

// ---------------------------------------------------------------------------
// State held only in the main process
// ---------------------------------------------------------------------------

let mainWindow = null
let accessToken = null   // never sent to the renderer
let currentUser = null   // safe subset, mirrored to the renderer
let lastUpdateCheck = null
let lastServerStatus = null

// ---------------------------------------------------------------------------
// Saved settings
//
// The token is a live credential, so it is encrypted with Electron's safeStorage,
// which on Windows uses DPAPI - tied to the logged-in Windows account. Copying the
// file to another machine yields nothing usable. Plain text on disk would mean
// anyone with file access could act as this Discord user.
// ---------------------------------------------------------------------------

function settingsPath () {
  return path.join(app.getPath('userData'), 'settings.json')
}

// The launcher's own action trail. Six identical "not launched from the launcher"
// sessions from one player who swears he presses JACK IN - and nothing anywhere
// recorded what his launcher actually did. This file does: every launch verdict,
// the exact spawn result, the exit code. It ships to the server with the client
// logs, so the next failed attempt is read, not re-argued. Credential VALUES are
// never written - presence only. Lives in userData, which the footprint rule
// already purges on uninstall.
function launcherLog (aLine) {
  try {
    const file = path.join(app.getPath('userData'), 'launcher-trail.log')
    // A trail, not an archive: start over past ~200KB. What mattered has shipped.
    try { if (statSync(file).size > 200_000) rmSync(file, { force: true }) } catch { /* absent - fine */ }
    appendFileSync(file, `[${new Date().toISOString()}] ${aLine}\n`)
  } catch { /* the trail must never break the thing it watches */ }
}

// ================================ THE FOOTPRINT RULE ================================
// HARD RULE (crew decree, 2026-08-21): uninstalling leaves NOTHING. No files, no
// folders, no registry entries - not in AppData Roaming or Local, not on the Desktop,
// not anywhere. A machine after uninstall takes a fresh build as if the launcher had
// never been there.
//
// The enforcement is this manifest: every location the launcher can write OUTSIDE its
// own install folder is listed here, and the uninstall purge and the Deep clean scan
// walk this list - only this list. Writing to a new location without adding it here
// is a bug by definition: if it is not in the manifest, uninstall cannot remove it,
// and the next "fresh install fails" screenshot traces back to that omission.
//
// Three data-folder names exist because the app has answered to three names over its
// life: Electron's runtime name ("Night City Online", from package.json productName),
// the installer's product name ("Night City Online Launcher", from the build block),
// and the package name ("nightcity-launcher"). Old installs left data under each, so
// each is hunted.
function launcherFootprint () {
  const roaming = app.getPath('appData')
  const local = process.env.LOCALAPPDATA || path.join(app.getPath('home'), 'AppData', 'Local')
  const desktop = app.getPath('desktop')
  const startMenu = path.join(roaming, 'Microsoft', 'Windows', 'Start Menu', 'Programs')

  const names = ['Night City Online', 'Night City Online Launcher', 'nightcity-launcher']

  return {
    // Settings, sign-in tokens, the Nexus key, window caches - one per historical name.
    dataDirs: names.map((n) => path.join(roaming, n)),
    // electron-updater's download cache, same name variants.
    updaterDirs: names.map((n) => path.join(local, `${n}-updater`)),
    // Where the Setup installs, per name. The RUNNING copy's folder is excluded by
    // the callers - the NSIS uninstaller removes that one itself.
    installDirs: names.map((n) => path.join(local, 'Programs', n)),
    // Made by the installer AND by the Settings "Desktop shortcut" button.
    shortcuts: [
      path.join(desktop, 'Night City Online Launcher.lnk'),
      path.join(startMenu, 'Night City Online Launcher.lnk')
    ],
    // Crash-log copies handleGameCrash puts on the Desktop for handing over.
    desktopDir: desktop,
    desktopLogPattern: /^NightCityOnline-CRASH-.*\.log$/i
  }
}

// The launcher's uninstall registry entries live under HKCU (perMachine is false in
// the build config, so nothing of ours is ever under HKLM). Returns every entry whose
// display name is ours; orphaned means the uninstaller it points at no longer exists -
// the ghost row in Windows "Apps" that survives a hand-deleted folder.
function findUninstallRegistryEntries () {
  const entries = []
  try {
    const { execSync } = require('node:child_process')
    const out = execSync(
      'reg query "HKCU\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall" /s',
      { windowsHide: true, maxBuffer: 8 * 1024 * 1024 }).toString()

    let key = null
    let name = null
    let cmd = null
    const flush = () => {
      if (key && name && /night city online/i.test(name)) {
        // Orphaned ONLY when the UninstallString names an ABSOLUTE exe that is
        // provably gone. Everything else - unquoted paths with spaces we cannot
        // parse, MsiExec-style relative commands, unexpanded %vars%, no string at
        // all - is assumed healthy: deleting a live product's Apps row is the ghost
        // problem in the other direction, and "not sure" must never delete.
        let exe = null
        if (cmd) {
          const m = cmd.match(/"([^"]+\.exe)"/i) || cmd.match(/^(\S+\.exe)/i)
          if (m && path.isAbsolute(m[1])) exe = m[1]
        }
        entries.push({ key, displayName: name, orphaned: Boolean(exe) && !existsSync(exe) })
      }
    }
    for (const line of out.split(/\r?\n/)) {
      if (/^HKEY_/i.test(line)) { flush(); key = line.trim(); name = null; cmd = null; continue }
      let m = line.match(/^\s+DisplayName\s+REG_SZ\s+(.+)$/i)
      if (m) { name = m[1].trim(); continue }
      m = line.match(/^\s+UninstallString\s+REG_(?:EXPAND_)?SZ\s+(.+)$/i)
      if (m) cmd = m[1].trim()
    }
    flush()
  } catch { /* reg unavailable - nothing to report */ }
  return entries
}

function isGameRunning () {
  return new Promise((resolve) => {
    const check = spawn('tasklist', ['/FI', 'IMAGENAME eq Cyberpunk2077.exe', '/NH'], { windowsHide: true })
    let out = ''
    check.stdout.on('data', (c) => { out += c.toString() })
    check.on('close', () => resolve(out.includes('Cyberpunk2077.exe')))
    check.on('error', () => resolve(false))
  })
}

// Everything of ours found on the machine right now, EXCEPT what the running copy
// needs to keep running. aIncludeLive widens the sweep for the uninstall purge: the
// live data folder and working shortcuts go too (the NSIS uninstaller re-sweeps after
// this process exits, catching whatever Electron recreates on the way out).
// Windows path identity: case-insensitive, and 8.3 aliases / junctions / whatever
// casing the exe was launched with all name the same folder. A string compare that
// misses ANY of those classes the running install as residue and shreds it mid-run -
// canonicalize first, compare folded.
function samePath (a, b) {
  const canon = (p) => {
    try { return realpathSync.native(p).toLowerCase() } catch { return path.resolve(p).toLowerCase() }
  }
  return canon(a) === canon(b)
}

function collectResidue (aIncludeLive) {
  const fp = launcherFootprint()
  const ownDir = path.dirname(process.execPath)
  const liveData = app.getPath('userData')

  const dirs = []
  for (const d of [...fp.dataDirs, ...fp.updaterDirs]) {
    if (!existsSync(d)) continue
    if (!aIncludeLive && samePath(d, liveData)) continue
    dirs.push(d)
  }
  for (const d of fp.installDirs) {
    // NEVER the folder this process runs from - deleting it out from under a live
    // exe fails halfway and manufactures exactly the residue this hunts.
    if (existsSync(d) && !samePath(d, ownDir)) dirs.push(d)
  }

  const files = []
  for (const s of fp.shortcuts) {
    if (!existsSync(s)) continue
    if (aIncludeLive) { files.push(s); continue }
    // For Deep clean, a shortcut is residue only when it points at a dead exe.
    try {
      const { target } = shell.readShortcutLink(s)
      if (target && !existsSync(target)) files.push(s)
    } catch { /* unreadable link - leave it alone */ }
  }
  try {
    for (const f of readdirSync(fp.desktopDir)) {
      if (fp.desktopLogPattern.test(f)) files.push(path.join(fp.desktopDir, f))
    }
  } catch { /* desktop unreadable - skip */ }

  // Orphaned registry rows are residue always; the LIVE entry belongs to the NSIS
  // uninstaller, which removes it itself.
  const regKeys = findUninstallRegistryEntries()
    .filter((e) => e.orphaned)
    .map((e) => e.key)

  return { dirs, files, regKeys }
}

// Removes what collectResidue found. Locked or protected items are reported, not
// fatal - a half-clean with an honest list beats an exception with nothing done.
function purgeResidue (aResidue) {
  const failed = []
  const { execFileSync } = require('node:child_process')

  for (const d of aResidue.dirs) {
    try { rmSync(d, { recursive: true, force: true }) } catch (err) { failed.push(`${d} (${err.code || err.message})`) }
  }
  for (const f of aResidue.files) {
    try { rmSync(f, { force: true }) } catch (err) { failed.push(`${f} (${err.code || err.message})`) }
  }

  // Registry LAST, and re-scanned: purging another copy's folder just deleted ITS
  // uninstaller, so its Apps row became a ghost within this very run - sweep it now
  // rather than leaving it for a second Deep clean. execFileSync, not a cmd string:
  // any user process can create a key whose NAME carries quotes.
  const orphanedNow = findUninstallRegistryEntries().filter((e) => e.orphaned).map((e) => e.key)
  for (const k of new Set([...aResidue.regKeys, ...orphanedNow])) {
    try { execFileSync('reg', ['delete', k, '/f'], { windowsHide: true }) } catch { failed.push(k) }
  }
  return failed
}

// The one folder the uninstall flow deletes OUTSIDE our own footprint is the mod's,
// and it comes from a settings override a person once typed. Validate the shape, not
// just the marker file: a botched manual install can leave CyberpunkMP.dll at the
// GAME ROOT, and a picker pointed there would pass the dll check - then "remove the
// mod" recursively deletes all of Cyberpunk 2077. A real mod folder sits under
// red4ext\plugins and does not contain the game exe.
function isSafeModDir (aModDir) {
  if (!aModDir) return false
  if (existsSync(path.join(aModDir, 'bin', 'x64', 'Cyberpunk2077.exe'))) return false
  return /[\\/]red4ext[\\/]plugins[\\/][^\\/]+$/i.test(path.resolve(aModDir))
}

function loadSettings () {
  try {
    return JSON.parse(readFileSync(settingsPath(), 'utf8'))
  } catch {
    return {}
  }
}

function saveSettings (patch) {
  try {
    const current = loadSettings()
    const merged = { ...current, ...patch }
    mkdirSync(path.dirname(settingsPath()), { recursive: true })
    writeFileSync(settingsPath(), JSON.stringify(merged, null, 2))
  } catch (err) {
    console.error('Could not save settings:', err.message)
  }
}

function saveToken (token) {
  if (!token) {
    saveSettings({ token: null })
    return
  }

  if (!safeStorage.isEncryptionAvailable()) {
    // Better to forget the session than to leave a bearer token lying in plain text.
    console.warn('Encrypted storage unavailable - not remembering the sign-in.')
    return
  }

  saveSettings({ token: safeStorage.encryptString(token).toString('base64') })
}

function loadToken () {
  const { token } = loadSettings()
  if (!token || !safeStorage.isEncryptionAvailable()) return null

  try {
    return safeStorage.decryptString(Buffer.from(token, 'base64'))
  } catch {
    // Different Windows account, or the file was tampered with.
    return null
  }
}

function getServerDir () {
  return loadSettings().serverDir || defaultServerDir()
}

// ---------------------------------------------------------------------------
// Mod updates
//
// The release on GitHub is the single source of truth - the same one the Discord
// bot announces from, so the launcher and #server-update can never disagree about
// what the current build is.
// ---------------------------------------------------------------------------

const GITHUB_REPO = 'ofmiceandcam98-eng/CyberpunkMP'

// Whatever is newest, rather than a pinned tag.
//
// This used to point at one hardcoded release, which meant every version bump needed a
// matching code change in a shipped binary - a launcher already in someone's hands could
// never see a release published after it. Releases are now versioned (v0.1.4, v0.1.5...)
// and this always resolves to the current one.
const RELEASE_API = `https://api.github.com/repos/${GITHUB_REPO}/releases/latest`

/**
 * Finds Cyberpunk, wherever it is.
 *
 * A hardcoded Steam path is wrong for anyone with a second drive, a GOG copy, or
 * Epic - which is most people. Asks Steam via the registry first, since Steam records
 * every library folder it uses including ones on other drives.
 */
function findGameDir () {
  const saved = loadSettings().gameDir
  if (saved && existsSync(path.join(saved, 'bin', 'x64', 'Cyberpunk2077.exe'))) return saved

  const candidates = []

  // GOG Galaxy records the exact install path in the registry, same idea as Steam's
  // library file - authoritative, drive letters and custom folders included. 1423049311
  // is Cyberpunk 2077's GOG product id. Checked FIRST because a GOG player's install
  // is the one the folder-guessing below most often misses.
  try {
    const { execSync } = require('node:child_process')
    for (const key of ['HKLM\\SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1423049311',
                       'HKLM\\SOFTWARE\\GOG.com\\Games\\1423049311']) {
      try {
        const out = execSync(`reg query "${key}" /v path`, { windowsHide: true }).toString()
        const match = out.match(/path\s+REG_SZ\s+(.+)/)
        if (match) candidates.push(match[1].trim())
      } catch { /* key absent - not a GOG install */ }
    }
  } catch { /* reg unavailable - fall through to guessing */ }

  // Steam knows where its own libraries are - far better than guessing.
  for (const root of steamRoots()) {
    candidates.push(path.join(root, 'steamapps', 'common', 'Cyberpunk 2077'))

    for (const vdf of [path.join(root, 'steamapps', 'libraryfolders.vdf'),
                       path.join(root, 'config', 'libraryfolders.vdf')]) {
      try {
        const text = readFileSync(vdf, 'utf8')
        for (const match of text.matchAll(/"path"\s+"([^"]+)"/g)) {
          candidates.push(path.join(match[1].replace(/\\\\/g, '\\'),
                                    'steamapps', 'common', 'Cyberpunk 2077'))
        }
      } catch { /* no such library file */ }
    }
  }

  // Epic records every install in a manifest, one .item file per game, each holding the
  // exact InstallLocation. Asking is authoritative; the drive-letter guessing below only
  // ever finds an Epic copy that happens to sit in the default folder, which is not where
  // anyone with a second drive puts it.
  try {
    const manifestDir = path.join(process.env.PROGRAMDATA || 'C:\\ProgramData',
                                  'Epic', 'EpicGamesLauncher', 'Data', 'Manifests')

    for (const file of readdirSync(manifestDir)) {
      if (!file.toLowerCase().endsWith('.item')) continue

      try {
        const manifest = JSON.parse(readFileSync(path.join(manifestDir, file), 'utf8'))
        const name = `${manifest.DisplayName || ''} ${manifest.MandatoryAppFolderName || ''}`

        // Matched on the name rather than a catalog id: Epic's ids for this game differ
        // between the base game and the expansion bundle, and a name match covers both.
        if (/cyberpunk/i.test(name) && manifest.InstallLocation) {
          candidates.push(manifest.InstallLocation)
        }
      } catch { /* a malformed manifest must not stop the others */ }
    }
  } catch { /* Epic not installed */ }

  // Xbox / Microsoft Store. Installs land under a per-drive XboxGames folder with the
  // playable files one level down in Content, which is the part a plain folder guess gets
  // wrong even when it looks in the right place.
  for (const letter of 'CDEFGHIJKLMNOPQRSTUVWXYZAB') {
    candidates.push(`${letter}:\\XboxGames\\Cyberpunk 2077\\Content`)
    candidates.push(`${letter}:\\Program Files\\WindowsApps\\Cyberpunk 2077`)
  }

  // Common layouts on EVERY drive letter, A through Z - C first because it is the
  // likeliest, A and B last because they are the rarest. But rare is not never: one
  // player's Steam library lives on B:. Nothing here should ever be trimmed to "the
  // usual drives"; a missing letter reads as "game not found" to the one person whose
  // layout it excluded, and existsSync on an absent drive costs nothing.
  for (const letter of 'CDEFGHIJKLMNOPQRSTUVWXYZAB') {
    for (const suffix of ['SteamLibrary\\steamapps\\common\\Cyberpunk 2077',
                          'Games\\Cyberpunk 2077',
                          'Games\\GOG\\Cyberpunk 2077',
                          'GOG Games\\Cyberpunk 2077',
                          'Program Files (x86)\\GOG Galaxy\\Games\\Cyberpunk 2077',
                          'Program Files\\Epic Games\\Cyberpunk2077',
                          'Epic Games\\Cyberpunk2077',
                          'Cyberpunk 2077']) {
      candidates.push(`${letter}:\\${suffix}`)
    }
  }

  for (const dir of candidates) {
    try {
      if (existsSync(path.join(dir, 'bin', 'x64', 'Cyberpunk2077.exe'))) return dir
    } catch { /* drive not present - never let this throw */ }
  }

  return null
}

function steamRoots () {
  const roots = ['C:\\Program Files (x86)\\Steam', 'C:\\Program Files\\Steam']

  // Reading the registry without a dependency: `reg query` is always present.
  try {
    const { execSync } = require('node:child_process')
    const out = execSync('reg query "HKCU\\Software\\Valve\\Steam" /v SteamPath', { windowsHide: true })
      .toString()
    const match = out.match(/SteamPath\s+REG_SZ\s+(.+)/)
    if (match) roots.unshift(match[1].trim().replace(/\//g, '\\'))
  } catch { /* Steam not installed, or key missing */ }

  return roots
}

function gameExecutable () {
  if (process.env.GAME_EXE) return process.env.GAME_EXE

  const dir = findGameDir()
  return dir ? path.join(dir, 'bin', 'x64', 'Cyberpunk2077.exe') : null
}

// Finds the installed mod folder. Named zzzCyberpunkMP by convention, but RED4ext
// loads any subfolder, so look for the DLL rather than trusting the name.
function findModDir () {
  // A folder the player pointed at themselves wins over anything found automatically.
  //
  // The search below is good but not infallible - an unusual install, a mod folder
  // renamed, a drive the game was moved off. When it misses, the launcher says "not
  // installed" about a mod sitting right there, and without this there is no way to
  // argue with it.
  const chosen = loadSettings().modDir
  if (chosen && existsSync(path.join(chosen, 'CyberpunkMP.dll'))) return chosen

  const gameDir = findGameDir()
  if (!gameDir) return null

  const pluginRoot = path.join(gameDir, 'red4ext', 'plugins')
  if (!existsSync(pluginRoot)) return null

  // Deliberately NOT filtering on isDirectory().
  //
  // A dev install points red4ext\plugins\zzzCyberpunkMP at the build output with a
  // directory junction, and Node reports junctions as symlinks - isDirectory() is
  // FALSE for them. Filtering on it skipped the one folder that mattered and the
  // launcher concluded the mod was not installed at all.
  //
  // Testing for the DLL directly answers the real question anyway: is the mod here?
  for (const entry of readdirSync(pluginRoot)) {
    const candidate = path.join(pluginRoot, entry, 'CyberpunkMP.dll')
    try {
      if (existsSync(candidate)) return path.join(pluginRoot, entry)
    } catch { /* unreadable entry - keep looking */ }
  }

  return null
}

/**
 * EVERY copy of the mod installed under the game, not just the first one found.
 *
 * findModDir stops at the first match, which is fine for updating but hides the failure
 * that actually happens to developers: RED4ext loads every plugin subdirectory it finds,
 * so a second copy is not ignored - it is loaded alongside, and one of the two sets of
 * scripts wins. The launcher updates the copy it found and reports "up to date" while the
 * game runs the other one.
 *
 * That is exactly what zeldfep hit - a launcher saying v0.3.51 next to a main menu from
 * before v0.3.45 - and nothing in the launcher could see it, because everything it checked
 * was about the copy it already knew.
 *
 * The usual cause is a dev junction at red4ext\plugins\zzzCyberpunkMP pointing at a build
 * output, sitting beside an install the launcher made itself.
 */
function findAllModDirs () {
  const found = []

  const chosen = loadSettings().modDir
  if (chosen && existsSync(path.join(chosen, 'CyberpunkMP.dll'))) found.push(chosen)

  const gameDir = findGameDir()
  if (gameDir) {
    const pluginRoot = path.join(gameDir, 'red4ext', 'plugins')

    if (existsSync(pluginRoot)) {
      for (const entry of readdirSync(pluginRoot)) {
        const dir = path.join(pluginRoot, entry)
        try {
          if (existsSync(path.join(dir, 'CyberpunkMP.dll')) && !found.includes(dir)) {
            found.push(dir)
          }
        } catch { /* unreadable entry - keep looking */ }
      }
    }
  }

  return found
}

/**
 * Moves every copy of the mod except the one we mean to run out of the plugins folder.
 *
 * Reporting duplicates was not enough. Nobody should have to work out which of two folders
 * is the stale one and delete it by hand to make a launcher's own update take effect - that
 * is the launcher's job, and it has every fact needed to do it.
 *
 * MOVED, not renamed and not deleted:
 *   - Renaming does not work. RED4ext scans every subdirectory of plugins for a DLL and
 *     does not care what the folder is called, so a renamed copy is still loaded. Getting
 *     this wrong looks like the fix silently failing.
 *   - Deleting is not ours to do. A second copy is usually a developer's junction pointing
 *     at their own build; removing it would throw away their working setup, and for a
 *     junction the target might not even be theirs to lose. Moving is reversible by drag
 *     and drop.
 *
 * Which copy survives, in order: the folder the player explicitly chose in Settings - a
 * developer pointing the launcher at their own build has said which one they want, and
 * that answer outranks ours - then the one carrying the current release's marker, then
 * whatever came first.
 */
async function resolveDuplicateInstalls (currentVersion) {
  const installs = findAllModDirs()
  if (installs.length < 2) return { moved: [], kept: installs[0] || null }

  const chosen = loadSettings().modDir

  const keep =
    installs.find((dir) => chosen && path.resolve(dir) === path.resolve(chosen)) ||
    installs.find((dir) => currentVersion && installedVersionAt(dir) === currentVersion) ||
    installs[0]

  const gameDir = findGameDir()
  if (!gameDir) return { moved: [], kept: keep }

  const parked = path.join(gameDir, 'red4ext', 'disabled-by-launcher')
  await fsp.mkdir(parked, { recursive: true })

  const moved = []

  for (const dir of installs) {
    if (dir === keep) continue

    // Only touch copies inside the plugins folder. One somewhere else is not being loaded
    // by RED4ext and is therefore not the problem - moving it would be meddling.
    const pluginRoot = path.join(gameDir, 'red4ext', 'plugins')
    if (path.resolve(path.dirname(dir)) !== path.resolve(pluginRoot)) continue

    const stamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)
    const destination = path.join(parked, `${path.basename(dir)}-${stamp}`)

    try {
      await fsp.rename(dir, destination)
      moved.push({ from: dir, to: destination, version: installedVersionAt(destination) })
      console.log(`[mods] moved a duplicate install out of plugins: ${dir} -> ${destination}`)
    } catch (err) {
      // Across volumes, or held open by a running game. Reported rather than thrown - a
      // launch with a duplicate still present is worse informed, not impossible.
      console.warn(`[mods] could not move ${dir}:`, err.message)
    }
  }

  return { moved, kept: keep, parked }
}

/**
 * Which release a copy of the mod came from, read from the marker written at install.
 *
 * Returns null for a copy installed before markers existed, or built by hand - which is
 * itself informative, since the launcher's own installs always have one.
 */
function installedVersionAt (modDir) {
  const marker = path.join(modDir, '.nco-version')
  if (!existsSync(marker)) return null

  try {
    return readFileSync(marker, 'utf8').trim() || null
  } catch {
    return null
  }
}

// ---------------------------------------------------------------------------
// Tailscale
//
// Deliberately detect-and-guide rather than bundling their installer. Shipping
// someone else's signed binary inside ours means owning their update cadence and
// their bugs, and adds ~35MB for people who already have it.
//
// What matters is that the three states are told apart. "Server offline" when the
// real problem is "you are not on the tailnet" is a wrong and unactionable message,
// and it makes your server look broken when it is fine.
// ---------------------------------------------------------------------------

const TAILSCALE_DOWNLOAD = 'https://tailscale.com/download'

function tailscaleExe () {
  const candidates = [
    'C:\\Program Files\\Tailscale\\tailscale.exe',
    'C:\\Program Files (x86)\\Tailscale\\tailscale.exe'
  ]

  return candidates.find((p) => existsSync(p)) || null
}

function getTailscaleStatus () {
  return new Promise((resolve) => {
    const exe = tailscaleExe()

    if (!exe) {
      resolve({ installed: false, connected: false, downloadUrl: TAILSCALE_DOWNLOAD })
      return
    }

    const proc = spawn(exe, ['status'], { windowsHide: true })

    let out = ''
    proc.stdout.on('data', (c) => { out += c.toString() })
    proc.stderr.on('data', (c) => { out += c.toString() })

    proc.on('close', (code) => {
      const text = out.toLowerCase()

      // `tailscale status` exits non-zero and says so when signed out or stopped.
      const connected = code === 0 &&
                        !text.includes('logged out') &&
                        !text.includes('stopped') &&
                        out.trim().length > 0

      // The tailnet IP, useful to show a host who needs to hand it out.
      const match = out.match(/(100\.\d+\.\d+\.\d+)/)

      resolve({
        installed: true,
        connected,
        ip: match ? match[1] : null,
        downloadUrl: TAILSCALE_DOWNLOAD
      })
    })

    proc.on('error', () => {
      resolve({ installed: true, connected: false, downloadUrl: TAILSCALE_DOWNLOAD })
    })
  })
}

/**
 * Is the game server actually up?
 *
 * Asks its public status endpoint, which needs no auth and returns nothing sensitive.
 * Reaching it at all proves the server is running.
 */
async function getGameServerStatus () {
  const server = await resolveServer()
  const host = server.host

  try {
    const response = await axios.get(`http://${host}:${server.port}/api/v1/status/`, { timeout: 4000 })
    return {
      online: true,
      players: response.data?.Players ?? 0,
      // Servers newer than v0.3.65 say whether they are running or deliberately
      // stopped by an admin; older ones say nothing, and reachable means running.
      state: response.data?.State || 'running',
      host,
      source: server.source
    }
  } catch {
    // Any failure - refused, timed out, no route - means players cannot get in.
    return { online: false, players: 0, state: 'unreachable', host }
  }
}

/**
 * Works out whether THIS launcher is the current one.
 *
 * The mod can update itself - it is just files on disk. A running launcher cannot
 * replace its own executable, so the most it can honestly do is notice it is behind
 * and say so clearly. Silently carrying on is the bad outcome: a stale launcher can
 * fail against a changed server or install an update it does not understand, and it
 * looks like the game is broken rather than the launcher being old.
 *
 * The published version is carried by the NAME of a marker asset on the release, so
 * this costs no extra request - the release listing was already fetched. Encoding it
 * in the filename rather than the file contents is the whole trick.
 */
function describeLauncherUpdate (release) {
  const current = app.getVersion()

  const marker = (release.assets || []).find((a) => /^launcher-version-.+\.txt$/.test(a.name))

  // No marker means a release published before this check existed. Treat that as
  // "nothing to say" rather than nagging about an update that does not exist.
  if (!marker) {
    return { current, available: current, upToDate: true }
  }

  const available = marker.name.replace(/^launcher-version-/, '').replace(/\.txt$/, '')

  return { current, available, upToDate: available === current }
}

async function checkForUpdates () {
  const modDir = findModDir()

  // The release is fetched BEFORE the not-installed check, because the launcher
  // version matters whether or not the mod is on disk - someone with a stale launcher
  // and no mod yet needs telling most of all.
  let release
  try {
    const response = await axios.get(RELEASE_API, {
      headers: { 'User-Agent': 'NightCityOnline-Launcher' },
      timeout: 10000
    })
    release = response.data
  } catch {
    // Cannot reach GitHub. Do NOT block play over this - an outage should not stop
    // people getting into a game they already have installed.
    if (!modDir) {
      return { installed: false, upToDate: false, reason: 'The mod is not installed yet.' }
    }
    return { installed: true, upToDate: true, offline: true, version: loadSettings().installedVersion || 'unknown' }
  }

  const launcher = describeLauncherUpdate(release)

  if (!modDir) {
    return { installed: false, upToDate: false, reason: 'The mod is not installed yet.', launcher }
  }

  const asset = (release.assets || []).find((a) => a.name === 'ModPayload.zip')
  if (!asset) {
    return { installed: true, upToDate: true, offline: true, version: release.tag_name, launcher }
  }

  // Compare against what was actually installed, recorded when we applied it. Asset id
  // plus size changes whenever a new payload is uploaded.
  const remoteStamp = `${asset.id}:${asset.size}`
  const localStamp = loadSettings().installedStamp

  return {
    installed: true,
    upToDate: localStamp === remoteStamp,
    version: release.tag_name,
    published: release.published_at,
    notes: release.body || '',
    downloadUrl: asset.browser_download_url,
    size: asset.size,
    remoteStamp,
    launcher
  }
}

/**
 * Checks the install is actually intact, not just that the version stamp matches.
 *
 * A stamp only records what was last downloaded. It says nothing about whether the
 * files are still there - someone deletes a folder, an antivirus quarantines the
 * DLL, a half-finished extract leaves gaps - and in every one of those cases the
 * launcher would happily report "up to date" and let them launch into a broken mod.
 */
async function verifyInstall () {
  const modDir = findModDir()

  if (!modDir) {
    return {
      ok: false,
      installed: false,
      problems: ['The mod is not installed. Install it once from the release, then use Update.']
    }
  }

  const problems = []

  // The pieces the mod cannot run without.
  const required = [
    ['CyberpunkMP.dll', 'the mod itself'],
    ['assets', 'game scripts and assets'],
    ['Rpc', 'network definitions']
  ]

  for (const [name, description] of required) {
    if (!existsSync(path.join(modDir, name))) {
      problems.push(`Missing ${name} (${description})`)
    }
  }

  // Redscript files are what the game compiles at startup; a missing one shows up as
  // a script validation failure long after launch, which is hard to trace back here.
  const redscriptDir = path.join(modDir, 'assets', 'redscript')
  let redscriptCount = 0

  if (existsSync(redscriptDir)) {
    const walk = (dir) => {
      for (const entry of readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) walk(full)
        else if (entry.name.endsWith('.reds')) redscriptCount++
      }
    }
    try { walk(redscriptDir) } catch { /* unreadable - reported below */ }
  }

  if (redscriptCount === 0) {
    problems.push('No script files found - the install looks incomplete')
  }

  // More than one copy of the mod is the failure that looks like nothing at all.
  //
  // RED4ext loads every plugin subdirectory, so a second copy is not dormant - it is
  // running, and its scripts can be the ones the game actually compiles. The launcher
  // updates the copy it finds first and truthfully reports that one as current, which is
  // how somebody ends up staring at an old main menu under a launcher saying it is up to
  // date. Reported with paths and versions, because the answer is always "delete the one
  // you did not mean to keep" and that requires knowing which is which.
  const allInstalls = findAllModDirs()

  if (allInstalls.length > 1) {
    const described = allInstalls
      .map((dir) => `${dir} (${installedVersionAt(dir) || 'version not recorded - built by hand?'})`)
      .join('  |  ')

    problems.push(
      `The mod is installed ${allInstalls.length} times and the game loads all of them: ${described}. ` +
      'Remove the ones you are not using - a copy the launcher does not update will still run, ' +
      'and its scripts can override the current ones.'
    )
  }

  // The mods this one is built on top of.
  //
  // Checked here because their absence does not look like their absence. Without
  // Codeware, every script that imports it fails, redscript abandons the whole
  // compilation, and the game starts with NO scripts at all - so there is no MULTIPLAYER
  // entry, no chat, no other players, and nothing on screen mentioning Codeware. Verify
  // used to pass that install cleanly, because the mod's own files were all present.
  const gameDir = findGameDir()

  if (gameDir) {
    const prerequisites = [
      ['RED4ext', path.join(gameDir, 'bin', 'x64', 'winmm.dll')],
      ['redscript', path.join(gameDir, 'engine', 'tools', 'scc.exe')],
      ['Codeware', path.join(gameDir, 'red4ext', 'plugins', 'Codeware', 'Codeware.dll')],
      ['ArchiveXL', path.join(gameDir, 'red4ext', 'plugins', 'ArchiveXL', 'ArchiveXL.dll')],
      ['TweakXL', path.join(gameDir, 'red4ext', 'plugins', 'TweakXL', 'TweakXL.dll')],
      ['Input Loader', path.join(gameDir, 'red4ext', 'plugins', 'input_loader', 'input_loader.dll')]
    ]

    const missing = prerequisites.filter(([, file]) => !existsSync(file)).map(([name]) => name)

    if (missing.length > 0) {
      problems.push(`Missing required mods: ${missing.join(', ')}. Use "Install everything" in Settings.`)
    }
  }

  // Now the version question, separately from the integrity one.
  const update = await checkForUpdates()
  lastUpdateCheck = update

  const outOfDate = update.installed && !update.upToDate && !update.offline

  if (outOfDate) {
    problems.push(`A newer build is available (${update.version})`)
  }

  return {
    ok: problems.length === 0,
    installed: true,
    modDir,
    redscriptCount,
    outOfDate,
    version: update.version,
    problems
  }
}

/**
 * First-time install: prerequisites AND the mod, in one pass.
 *
 * The six prerequisites (RED4ext, redscript, Codeware, ArchiveXL, TweakXL, Input
 * Loader) are all MIT licensed and are redistributed unmodified with their licence
 * texts. Each is a zip laid out relative to the game root, so extracting straight
 * into the game folder puts every file where it belongs.
 *
 * The mod folder is deliberately named zzzCyberpunkMP - RED4ext loads plugins
 * alphabetically, and the mod must load AFTER the libraries it depends on.
 */
async function installEverything (onProgress = () => {}) {
  const gameDir = findGameDir()
  if (!gameDir) {
    throw new Error('Could not find Cyberpunk 2077. Use Settings to point at it.')
  }

  const running = await isProcessRunning('Cyberpunk2077.exe')
  if (running) throw new Error('Close Cyberpunk 2077 first.')

  onProgress('Downloading...')

  // /releases/latest/download/ is a permanent URL that always resolves to the newest
  // release's asset of that name.
  //
  // This previously interpolated RELEASE_TAG - a constant that was DELETED when the
  // update check moved to /releases/latest. Referencing it threw a ReferenceError before
  // a single byte was fetched, so first-time install was broken outright for anyone on
  // 0.1.4 or later. Nothing pointed at it because the launcher's own update path had
  // stopped using tags, and nobody had done a fresh install since.
  const url = `https://github.com/${GITHUB_REPO}/releases/latest/download/FullInstall.zip`
  const response = await axios.get(url, { responseType: 'arraybuffer', timeout: 180000 })
  const buffer = Buffer.from(response.data)

  if (buffer.length < 1024 * 1024) {
    throw new Error(`That download looks wrong (${buffer.length} bytes) - nothing was changed.`)
  }

  const pkg = new AdmZip(buffer)

  // Zip entry paths use whatever separator the tool that made them wrote.
  //
  // THIS IS THE BUG THAT BROKE EVERY NEW INSTALL. The package stores entries as
  // "prerequisites\Codeware-1.18.0.zip" with a BACKSLASH, and this code filtered on
  // startsWith('prerequisites/') with a forward slash. That matched nothing - both for
  // the prerequisites and for the mod - so "Install everything" extracted precisely
  // zero files and reported success.
  //
  // The result was a player with no Codeware, no Input Loader and no mod, whose game
  // then failed to compile any redscript and showed them nothing at all. It looked like
  // five unrelated faults and was one line.
  const normalise = (name) => name.replace(/\\/g, '/')

  // --- prerequisites ------------------------------------------------------
  // Each is its own zip nested inside the package. Extract them into the game
  // root, where their internal paths (bin\x64\..., red4ext\...) land correctly.
  const prereqs = pkg.getEntries().filter((e) => !e.isDirectory && normalise(e.entryName).startsWith('prerequisites/'))

  // Refuse to continue rather than "succeed" having done nothing. A silent no-op is the
  // failure mode that cost a player their evening; an error naming the problem does not.
  if (prereqs.length === 0) {
    throw new Error('The install package has no prerequisites in it. This is a packaging fault - please report it.')
  }

  const installed = []

  for (const entry of prereqs) {
    const name = path.basename(normalise(entry.entryName))
    onProgress(`Installing ${name.replace(/\.zip$/, '')}...`)

    const inner = new AdmZip(entry.getData())
    inner.extractAllTo(gameDir, true)
    installed.push(name.replace(/\.zip$/, ''))
  }

  // --- the mod ------------------------------------------------------------
  onProgress('Installing the multiplayer mod...')

  const modTarget = path.join(gameDir, 'red4ext', 'plugins', 'zzzCyberpunkMP')
  mkdirSync(modTarget, { recursive: true })

  let modFiles = 0

  for (const entry of pkg.getEntries()) {
    const relative = normalise(entry.entryName)
    if (entry.isDirectory || !relative.startsWith('mod/')) continue

    const dest = path.join(modTarget, relative.slice('mod/'.length).replace(/\//g, path.sep))

    mkdirSync(path.dirname(dest), { recursive: true })
    writeFileSync(dest, entry.getData())
    modFiles++
  }

  if (modFiles === 0) {
    throw new Error('The install package has no mod files in it. This is a packaging fault - please report it.')
  }

  // --- prove it actually landed -------------------------------------------
  //
  // Checked on disk, not inferred from "the loop ran". Every symptom this bug produced
  // came from believing an install had happened when it had not, so the install now ends
  // by looking.
  const mustExist = {
    'the mod': path.join(modTarget, 'CyberpunkMP.dll'),
    'RED4ext': path.join(gameDir, 'bin', 'x64', 'winmm.dll'),
    'Codeware': path.join(gameDir, 'red4ext', 'plugins', 'Codeware', 'Codeware.dll'),
    'ArchiveXL': path.join(gameDir, 'red4ext', 'plugins', 'ArchiveXL', 'ArchiveXL.dll'),
    'TweakXL': path.join(gameDir, 'red4ext', 'plugins', 'TweakXL', 'TweakXL.dll'),
    'Input Loader': path.join(gameDir, 'red4ext', 'plugins', 'input_loader', 'input_loader.dll')
  }

  const absent = Object.entries(mustExist).filter(([, p]) => !existsSync(p)).map(([label]) => label)

  if (absent.length > 0) {
    throw new Error(`Installed, but these are missing afterwards: ${absent.join(', ')}. ` +
                    'Check the game folder is writable and that no anti-virus removed them.')
  }

  // Record what was installed so the update check has something to compare against.
  const info = await checkForUpdates()
  if (info.remoteStamp) {
    saveSettings({ installedStamp: info.remoteStamp, installedVersion: info.version })

  // Stamped into the folder itself, not just into settings.
  //
  // Settings describe "the install the launcher knows about"; this describes THIS copy on
  // disk. When two copies exist that difference is the whole diagnosis - it is what lets
  // verify say which folder is current and which is the stale one still being loaded.
  try {
    writeFileSync(path.join(modDir, '.nco-version'), String(info.version || 'unknown'))
  } catch (err) {
    console.warn('[install] could not record the version marker:', err.message)
  }

  }

  onProgress('Done')

  return { installed: true, gameDir, modDir: modTarget, prerequisites: installed.length, modFiles, components: installed }
}

// ---------------------------------------------------------------------------
// Client log shipping
//
// Every session's mod log is pushed to the server automatically, so debugging never
// depends on a player finding a file and pasting it somewhere. The server files them
// under logs/clients/<player>/ and keeps only the newest ten, so nothing goes stale.
//
// Always aimed at the canonical published server, NOT the dev-selected play server:
// the point is one collection point that whoever is debugging can read in one place,
// regardless of which server the session ran on. The log itself says where it connected.
// ---------------------------------------------------------------------------

let logShipInFlight = false

async function shipClientLogs (reason) {
  // A crash and the game-closed poll can fire together; one shipment is plenty.
  if (logShipInFlight) return
  logShipInFlight = true

  try {
    const modDir = findModDir()
    if (!modDir) return

    const logDir = path.join(modDir, 'logs')
    if (!existsSync(logDir)) return

    const shipped = loadSettings().shippedLogs || {}
    const weekAgo = Date.now() - 7 * 24 * 60 * 60 * 1000

    const candidates = readdirSync(logDir)
      .filter((f) => f.startsWith('CyberpunkMP_') && f.endsWith('.log'))
      .map((f) => {
        const full = path.join(logDir, f)
        const stat = statSync(full)
        return { name: f, full, size: stat.size, mtime: stat.mtimeMs }
      })
      // A week-old log is outdated data, not evidence. Size is part of the shipped
      // record so a log that grew since its last shipment goes again; one that
      // has not is skipped instead of re-sent forever.
      .filter((f) => f.size > 0 && f.mtime > weekAgo && shipped[f.name] !== f.size)
      .sort((a, b) => b.mtime - a.mtime)
      .slice(0, 3)

    // No early return on an empty list any more: the launcher's own trail below must
    // ship even when the game never produced a log - a REFUSED launch is exactly the
    // session where the trail is the only witness.

    const published = await fetchPublishedServer()
    if (!published?.host) return

    const player = currentUser?.handle || os.userInfo().username || 'unknown'
    const base = `http://${published.host}:${published.port || 11778}/api/v1/logs/`
    let sent = 0

    // The launcher's own trail rides along, un-deduped - its whole value is the
    // freshest few lines, and it is why a "JACK IN does nothing" report can be read
    // instead of re-argued.
    try {
      const trail = readFileSync(path.join(app.getPath('userData'), 'launcher-trail.log'), 'utf8')
      if (trail.length && trail.length < 3_500_000) {
        await axios.post(`${base}?player=${encodeURIComponent(player)}&file=launcher-trail.log`, trail,
                         { headers: { 'Content-Type': 'text/plain' }, timeout: 20000 })
      }
    } catch { /* no trail yet - nothing to say */ }

    for (const log of candidates) {
      const body = readFileSync(log.full, 'utf8')

      // The server rejects anything over 4 MB; do not waste the bandwidth finding out.
      if (body.length > 3_500_000) continue

      await axios.post(
        `${base}?player=${encodeURIComponent(player)}&file=${encodeURIComponent(log.name)}`,
        body,
        { headers: { 'Content-Type': 'text/plain' }, timeout: 20000 }
      )

      shipped[log.name] = log.size
      sent++
    }

    // Forget records for files the client-side pruning already deleted, so this
    // bookkeeping cannot grow without bound.
    const stillThere = new Set(readdirSync(logDir))
    for (const key of Object.keys(shipped)) {
      if (!stillThere.has(key)) delete shipped[key]
    }

    saveSettings({ shippedLogs: shipped })
    if (sent) console.log(`[logs] shipped ${sent} log(s) to the server (${reason})`)
  } catch (err) {
    // Never let log delivery become its own problem - next trigger retries anyway.
    console.warn('[logs] could not ship logs:', err.message)
  } finally {
    logShipInFlight = false
  }
}

/**
 * The game died. Put the log somewhere obvious and tell the player.
 *
 * The whole point is that nobody should have to find it. It gets copied to the Desktop
 * with a name that says what it is, the folder opens with the file selected, and the
 * path is on the clipboard. Three ways to hand it over, because the person who just
 * crashed is annoyed and should not also be given a scavenger hunt.
 */
function handleGameCrash (exitCode) {
  shipClientLogs('game crash')

  const modDir = findModDir()
  const hex = '0x' + (exitCode >>> 0).toString(16).toUpperCase()

  // 0xC0000005 is an access violation - worth naming, because it is the signature of
  // the crash this project has been chasing.
  const kind = exitCode === -1073741819 ? `access violation (${hex})` : `exit code ${exitCode} (${hex})`

  let savedTo = null

  try {
    const logDir = path.join(modDir || '', 'logs')

    if (modDir && existsSync(logDir)) {
      const newest = readdirSync(logDir)
        .filter((f) => f.startsWith('CyberpunkMP_') && f.endsWith('.log'))
        .map((f) => ({ name: f, full: path.join(logDir, f), time: statSync(path.join(logDir, f)).mtimeMs }))
        .sort((a, b) => b.time - a.time)[0]

      if (newest) {
        const stamp = new Date().toISOString().replace(/[:T]/g, '-').slice(0, 19)
        savedTo = path.join(app.getPath('desktop'), `NightCityOnline-CRASH-${stamp}.log`)
        writeFileSync(savedTo, readFileSync(newest.full))
      }
    }
  } catch (err) {
    console.error('Could not copy the crash log:', err.message)
  }

  if (savedTo) clipboard.writeText(savedTo)

  if (mainWindow) {
    mainWindow.webContents.send('game-crashed', { kind, savedTo })
    mainWindow.show()
    mainWindow.focus()
  }

  const detail = savedTo
    ? `Your log was sent to the dev server automatically, and a copy is on your Desktop:\n\n${savedTo}\n\n` +
      'The path is on your clipboard in case anyone asks for the file directly.'
    : 'The log could not be found automatically. Look in:\n\n' +
      `${modDir ? path.join(modDir, 'logs') : 'your mod folder'}\n\nand send the newest file.`

  dialog.showMessageBox(mainWindow, {
    type: 'warning',
    buttons: savedTo ? ['Show me the file', 'OK'] : ['OK'],
    defaultId: 0,
    title: 'Cyberpunk crashed',
    message: `The game crashed - ${kind}`,
    detail
  }).then((choice) => {
    if (savedTo && choice.response === 0) shell.showItemInFolder(savedTo)
  })
}

function isProcessRunning (imageName) {
  return new Promise((resolve) => {
    const check = spawn('tasklist', ['/FI', `IMAGENAME eq ${imageName}`, '/NH'], { windowsHide: true })
    let out = ''
    check.stdout.on('data', (c) => { out += c.toString() })
    check.on('close', () => resolve(out.includes(imageName)))
    check.on('error', () => resolve(false))
  })
}

async function applyUpdate () {
  const modDir = findModDir()
  if (!modDir) throw new Error('The mod is not installed - install it once first.')

  // The game holds CyberpunkMP.dll open, so extracting over it fails with a
  // permission error that reads like a broken download. Say what it actually is.
  const running = await new Promise((resolve) => {
    const check = spawn('tasklist', ['/FI', 'IMAGENAME eq Cyberpunk2077.exe', '/NH'], { windowsHide: true })
    let out = ''
    check.stdout.on('data', (c) => { out += c.toString() })
    check.on('close', () => resolve(out.includes('Cyberpunk2077.exe')))
    check.on('error', () => resolve(false))
  })

  if (running) {
    throw new Error('Close Cyberpunk 2077 first - the game is holding the mod files open.')
  }

  // Reuse the check we already did rather than asking GitHub again. Two calls per
  // click doubled the chance of hitting the unauthenticated rate limit, and a failed
  // second call made the button look like it needed pressing repeatedly.
  const info = lastUpdateCheck?.downloadUrl ? lastUpdateCheck : await checkForUpdates()

  if (!info.downloadUrl) {
    throw new Error('Could not reach GitHub to download the update. Check your connection and try again.')
  }

  const response = await axios.get(info.downloadUrl, { responseType: 'arraybuffer', timeout: 120000 })
  const buffer = Buffer.from(response.data)

  // Refuse anything implausible rather than shredding a working install with a
  // truncated download or an error page.
  if (buffer.length < 1024 * 1024) {
    throw new Error(`That download looks wrong (${buffer.length} bytes) - install left alone.`)
  }

  const zip = new AdmZip(buffer)
  zip.extractAllTo(modDir, true)

  saveSettings({ installedStamp: info.remoteStamp, installedVersion: info.version })

  // Stamped into the folder itself, not just into settings.
  //
  // Settings describe "the install the launcher knows about"; this describes THIS copy on
  // disk. When two copies exist that difference is the whole diagnosis - it is what lets
  // verify say which folder is current and which is the stale one still being loaded.
  try {
    writeFileSync(path.join(modDir, '.nco-version'), String(info.version || 'unknown'))
  } catch (err) {
    console.warn('[install] could not record the version marker:', err.message)
  }


  return { version: info.version }
}

// ---------------------------------------------------------------------------
// Uninstall
// ---------------------------------------------------------------------------

/**
 * Removes the mod files, leaving the game and the prerequisite mods alone.
 *
 * Deliberately confirms first and reports what it will delete. Anything that removes
 * files should say exactly what, before doing it - a launcher that quietly wipes a
 * folder is a launcher nobody trusts twice.
 */
async function uninstallMod () {
  const modDir = findModDir()
  if (!modDir) throw new Error('The mod does not appear to be installed.')

  const running = await new Promise((resolve) => {
    const check = spawn('tasklist', ['/FI', 'IMAGENAME eq Cyberpunk2077.exe', '/NH'], { windowsHide: true })
    let out = ''
    check.stdout.on('data', (c) => { out += c.toString() })
    check.on('close', () => resolve(out.includes('Cyberpunk2077.exe')))
    check.on('error', () => resolve(false))
  })

  if (running) throw new Error('Close Cyberpunk 2077 first.')

  const choice = await dialog.showMessageBox(mainWindow, {
    type: 'warning',
    buttons: ['Remove the mod', 'Cancel'],
    defaultId: 1,
    cancelId: 1,
    title: 'Remove CyberpunkMP',
    message: 'Remove the multiplayer mod?',
    detail:
      `This deletes:\n${modDir}\n\n` +
      'Your game, your saves, and the other mods (RED4ext, Codeware, ArchiveXL, ' +
      'TweakXL, redscript, Input Loader) are left alone.\n\n' +
      'You can reinstall any time from the release page.'
  })

  if (choice.response !== 0) return { removed: false }

  // Same shape-check the uninstall flow uses: a settings override pointed at the
  // game root (with a stray dll making it look valid) must never become
  // "recursively delete Cyberpunk 2077".
  if (!isSafeModDir(modDir)) {
    throw new Error(`The mod folder looks wrong (${modDir}) - not deleting it. ` +
                    'Remove red4ext\\plugins\\zzzCyberpunkMP by hand.')
  }

  rmSync(modDir, { recursive: true, force: true })
  saveSettings({ installedStamp: null, installedVersion: null })

  return { removed: true, modDir }
}

/**
 * Forgets everything the launcher stores about this user: the saved Discord session
 * and any remembered folders. Does not touch the game or the mod.
 */
async function resetLauncherData () {
  const choice = await dialog.showMessageBox(mainWindow, {
    type: 'question',
    buttons: ['Sign out and forget', 'Cancel'],
    defaultId: 1,
    cancelId: 1,
    title: 'Reset launcher',
    message: 'Forget your saved sign-in and settings?',
    detail:
      'You will need to sign in with Discord again next time.\n\n' +
      'Your game and the mod are untouched.\n\n' +
      'To revoke the launcher\'s access entirely, also remove it under ' +
      'Discord > Settings > Authorized Apps.'
  })

  if (choice.response !== 0) return { reset: false }

  accessToken = null
  currentUser = null
  lastUpdateCheck = null

  try {
    rmSync(settingsPath(), { force: true })
  } catch { /* nothing saved yet */ }

  return { reset: true }
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

function createWindow () {
  mainWindow = new BrowserWindow({
    // 1180x760 rather than 1280x720.
    //
    // The extra height matters more than matching a video resolution: this is a
    // two-column layout with a tall right-hand stack (status rows, verify, play,
    // and the admin server panel). At 720 the server controls were cut off, which
    // is what "having to stretch it" was.
    //
    // A minimum size stops anyone shrinking it back into that state.
    width: 1180,
    height: 800,
    minWidth: 1024,
    minHeight: 740,
    backgroundColor: '#0a0b0d',
    autoHideMenuBar: true,
    webPreferences: {
      // contextIsolation keeps the page's JavaScript in a separate world from
      // Electron's internals, so the page cannot reach Node APIs directly. The
      // preload script exposes an explicit, minimal bridge instead. Turning
      // either of these off would let any script in the page spawn processes.
      contextIsolation: true,
      nodeIntegration: false,
      // ESM preload scripts do not run in a sandboxed renderer. Leaving sandbox on
      // (the default) means the preload never loads at all, window.launcher is
      // undefined, and every button silently does nothing - no error, no clue.
      sandbox: false,
      // .mjs, not .js - Electron requires that extension for an ESM preload script.
      preload: path.join(__dirname, 'preload.mjs')
    }
  })

  mainWindow.loadFile('index.html')

  // Surface renderer and preload failures instead of letting them vanish.
  mainWindow.webContents.on('preload-error', (_event, preloadPath, error) => {
    console.error('PRELOAD FAILED', preloadPath, error)
  })

  mainWindow.webContents.on('console-message', (_event, level, message) => {
    if (level >= 2) console.error('[renderer]', message)
  })

  if (process.argv.includes('--dev')) {
    mainWindow.webContents.openDevTools({ mode: 'detach' })
  }

  // Anything trying to open a new window (a link, an ad, an injected script)
  // goes to the user's real browser instead of opening an uncontrolled
  // Electron window with our privileges.
  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url)
    return { action: 'deny' }
  })
}

// ---------------------------------------------------------------------------
// PKCE
//
// The classic OAuth "authorization code" flow proves the app's identity with a
// client secret. That works for a web server, where the secret stays on the
// server. It does NOT work for a desktop app: the app is handed to users, and
// anything inside it can be read out with a text editor. A secret shipped to
// users is not a secret.
//
// PKCE ("proof key for code exchange") replaces the secret with a value
// invented fresh for each login:
//
//   1. Generate a random `code_verifier`.
//   2. Send its SHA-256 hash (`code_challenge`) with the authorize request.
//   3. Send the original `code_verifier` when exchanging the code.
//
// Discord checks that the verifier hashes to the challenge it saw earlier. An
// attacker who intercepts the authorization code cannot use it, because they
// never saw the verifier - it never left this process.
// ---------------------------------------------------------------------------

/**
 * Turns a Discord snowflake into a stable, meaningless display number.
 *
 * Same account always produces the same id, so people can recognise each other, but
 * it reveals nothing about the Discord account behind it and cannot be reversed.
 *
 * Display only. Bans and permissions key on the real snowflake server-side, which is
 * why a collision here would be cosmetic rather than a way to inherit someone's rank.
 */
/**
 * What the renderer is allowed to see: everything except the Discord snowflake.
 *
 * One place that decides this, so a new field cannot accidentally leak the id by
 * being added to the wrong object.
 */
function publicProfile () {
  if (!currentUser) return null
  const { id, ...safe } = currentUser
  return safe
}

function derivePlayerId (snowflake) {
  // FNV-1a. Must stay byte-identical to DerivePlayerId in the server's
  // GameServer.cpp, or the number a player reads off their launcher will not match
  // the one in the server log and reports become untraceable.
  //
  // Math.imul is required, not `*`: JavaScript numbers are doubles, so a plain
  // multiply loses the low bits once the value exceeds 2^53 and silently stops
  // agreeing with the C++ side.
  const input = `nightcity:${snowflake}`

  let hash = 2166136261 // FNV offset basis

  for (let i = 0; i < input.length; i++) {
    hash ^= input.charCodeAt(i)
    hash = Math.imul(hash, 16777619) >>> 0
  }

  return (hash % 900000 + 100000).toString()
}

function base64url (buffer) {
  return buffer.toString('base64')
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=/g, '')
}

function createPkcePair () {
  // 32 random bytes -> 43 characters, comfortably inside the 43-128 the spec allows.
  const verifier = base64url(crypto.randomBytes(32))
  const challenge = base64url(crypto.createHash('sha256').update(verifier).digest())
  return { verifier, challenge }
}

/**
 * Opens Discord's consent page in a window owned by the launcher, and resolves
 * with the authorization code once Discord redirects.
 *
 * We never serve the redirect - we intercept it. The moment Discord tries to
 * navigate to 127.0.0.1, the code is in the URL, so the navigation is cancelled
 * and the window closed. No local web server is needed at all.
 *
 * The window is deliberately bare: no preload, no node integration, its own
 * session partition. It can reach nothing of ours. The user is typing their
 * password into Discord's real page, served over TLS from Discord - we are only
 * providing the frame around it.
 */
function signInWindow (authorizeUrl, expectedState) {
  return new Promise((resolve, reject) => {
    const authWindow = new BrowserWindow({
      width: 520,
      height: 800,
      parent: mainWindow,
      modal: true,
      show: false,
      autoHideMenuBar: true,
      backgroundColor: '#0f1115',
      title: 'Sign in with Discord',
      webPreferences: {
        contextIsolation: true,
        nodeIntegration: false,
        sandbox: true,
        // A fresh partition each time, so signing out actually signs out and the
        // next person on this PC does not inherit a live Discord session.
        partition: `oauth-${Date.now()}`
      }
    })

    let settled = false

    const finish = (fn, value) => {
      if (settled) return
      settled = true
      fn(value)
      if (!authWindow.isDestroyed()) authWindow.close()
    }

    const handleUrl = (url) => {
      if (!url.startsWith(REDIRECT_URI)) return false

      const parsed = new URL(url)
      const code = parsed.searchParams.get('code')
      const state = parsed.searchParams.get('state')
      const error = parsed.searchParams.get('error')

      if (error) {
        finish(reject, new Error(`Discord returned: ${error}`))
      } else if (state !== expectedState) {
        // See the CSRF note below - a mismatched state means this response is not
        // the one we asked for, and must be thrown away.
        finish(reject, new Error('State mismatch - possible CSRF attempt'))
      } else if (!code) {
        finish(reject, new Error('Discord did not return an authorization code'))
      } else {
        finish(resolve, code)
      }

      return true
    }

    // will-redirect catches the 302 to our redirect URI; will-navigate covers the
    // case where Discord navigates directly instead.
    authWindow.webContents.on('will-redirect', (event, url) => {
      if (handleUrl(url)) event.preventDefault()
    })

    authWindow.webContents.on('will-navigate', (event, url) => {
      if (handleUrl(url)) event.preventDefault()
    })

    authWindow.webContents.on('did-fail-load', (_e, errorCode, errorDescription, validatedURL) => {
      // Navigating to 127.0.0.1 legitimately fails - nothing is listening there.
      // That is expected and already handled above; anything else is real.
      if (validatedURL && validatedURL.startsWith(REDIRECT_URI)) return
      finish(reject, new Error(`Could not load Discord: ${errorDescription}`))
    })

    authWindow.on('closed', () => {
      if (!settled) {
        settled = true
        reject(new Error('Sign-in window was closed'))
      }
    })

    authWindow.once('ready-to-show', () => authWindow.show())
    authWindow.loadURL(authorizeUrl)
  })
}


// ---------------------------------------------------------------------------
// The handshake, end to end
// ---------------------------------------------------------------------------

async function signInWithDiscord () {
  const { verifier, challenge } = createPkcePair()
  const state = base64url(crypto.randomBytes(16))

  const authorizeUrl = new URL('https://discord.com/oauth2/authorize')
  authorizeUrl.searchParams.set('client_id', DISCORD_CLIENT_ID)
  authorizeUrl.searchParams.set('redirect_uri', REDIRECT_URI)
  authorizeUrl.searchParams.set('response_type', 'code')
  authorizeUrl.searchParams.set('scope', SCOPES.join(' '))
  authorizeUrl.searchParams.set('state', state)
  authorizeUrl.searchParams.set('code_challenge', challenge)
  authorizeUrl.searchParams.set('code_challenge_method', 'S256')

  // Sign in inside the launcher rather than kicking the user out to a browser.
  //
  // Worth being clear about the tradeoff: the most cautious approach is the
  // system browser, where the user can see the real URL and padlock themselves.
  // An in-app window asks them to trust that we are showing them the genuine
  // Discord. Every game launcher does it this way because bouncing users to a
  // browser mid-launch is miserable, and it is acceptable HERE because the page
  // is loaded straight from discord.com over TLS, in a window with no preload,
  // no node access and its own session. We never see the password - only the
  // authorization code Discord hands back afterwards.
  const code = await signInWindow(authorizeUrl.toString(), state)

  // Exchange the one-time code for an access token. `code_verifier` is what
  // replaces the client secret here.
  const body = new URLSearchParams({
    client_id: DISCORD_CLIENT_ID,
    grant_type: 'authorization_code',
    code,
    redirect_uri: REDIRECT_URI,
    code_verifier: verifier
  })

  const tokenResponse = await axios.post('https://discord.com/api/v10/oauth2/token', body.toString(), {
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
  })

  accessToken = tokenResponse.data.access_token

  return hydrateUserFromToken()
}

/**
 * Turns whatever is in accessToken into a profile.
 *
 * Shared by a fresh sign-in and by restoring a saved session, so both go through the
 * same identity check. Restoring from disk without re-asking Discord would mean a
 * revoked or expired token still looked signed in.
 */
async function hydrateUserFromToken () {
  // Ask Discord who this token belongs to. We never trust anything the UI says
  // about the user's identity - it comes from here, or not at all.
  const userResponse = await axios.get('https://discord.com/api/v10/users/@me', {
    headers: { Authorization: `Bearer ${accessToken}` }
  })

  const user = userResponse.data

  // Are they actually in Night City Online?
  //
  // This is for the PLAYER's benefit, not for security - it tells them straight
  // away that they need to join, rather than letting them get all the way to a
  // connection that the server refuses. The check that actually matters happens
  // server-side on connect, because anything decided here runs on the player's
  // own machine and could be patched out.
  let isMember = false

  try {
    const membership = await axios.get(
      `https://discord.com/api/v10/users/@me/guilds/${GUILD_ID}/member`,
      { headers: { Authorization: `Bearer ${accessToken}` }, validateStatus: () => true })

    // 404 is Discord's honest "not in that server".
    isMember = membership.status === 200
  } catch (err) {
    // Network trouble - do not claim they are a member, and do not claim they
    // are not. The server will decide either way.
    isMember = false
  }

  // Two records, deliberately.
  //
  // currentUser is INTERNAL and keeps the Discord snowflake, because isAdmin() and
  // anything else server-facing has to key on the real identity. publicProfile() is
  // what the renderer gets, and strips it.
  //
  // Collapsing these into one object is exactly the bug that broke the admin panel:
  // dropping `id` to keep it out of the UI also made every permission check fail.
  currentUser = {
    id: user.id,
    isMember,
    inviteUrl: DISCORD_INVITE,
    isAdmin: ADMIN_DISCORD_IDS.includes(user.id),

    // A display id derived from the Discord snowflake, NOT the snowflake itself.
    //
    // The snowflake is a real, permanent handle on someone's Discord account -
    // anyone who has it can look them up or add them. It ends up in screenshots,
    // stream overlays and pasted bug reports, and once it is out there it cannot
    // be changed. This is a one-way hash: stable for a given account, meaningless
    // to anyone else, and it cannot be turned back into their Discord identity.
    playerId: derivePlayerId(user.id),
    username: user.global_name || user.username,
    handle: user.username,
    avatarUrl: user.avatar
      ? `https://cdn.discordapp.com/avatars/${user.id}/${user.avatar}.png?size=128`
      : `https://cdn.discordapp.com/embed/avatars/${(BigInt(user.id) >> 22n) % 6n}.png`
  }

  // Resolve their Discord roles into a level, so the controls the launcher shows match
  // the commands the game gives them. Deliberately not awaited: it is two network calls
  // and nothing here should wait on Discord before the launcher is usable. The controls
  // appear a moment later, once the answer arrives.
  refreshUserLevel()
    .then((level) => {
      if (currentUser && level && level !== 'player') {
        currentUser.isAdmin = isAdmin()
        console.log(`[roles] ${currentUser.handle} resolved to ${level}`)
        fitWindowToRole()

        // Tell the page, or the controls never actually appear. The profile the
        // renderer got at sign-in was built before this answer arrived, with isAdmin
        // still false for anyone whose access comes from a Discord role rather than
        // the hardcoded list. The comment above promised the controls "a moment
        // later" and nothing delivered them: the only admin so far was on the
        // hardcoded list, resolved synchronously, so the gap was invisible until the
        // first role-based dev signed in and stayed a player.
        if (mainWindow && !mainWindow.isDestroyed()) {
          mainWindow.webContents.send('user-updated', publicProfile())
        }
      }
    })
    .catch(() => {})

  return currentUser
}

// ---------------------------------------------------------------------------
// Launching the game
// ---------------------------------------------------------------------------

// async because the server address may need fetching. Everything it does was already
// effectively asynchronous underneath; only the signature changed.
// ---------------------------------------------------------------------------
// The world template
//
// Nobody should have to own a post-Act-1 save to play, and nobody's own save should be
// touched. So the mod ships one, and every player loads that.
//
// The awkward part: there is NO way to load a save by name. The game exposes exactly one
// load call to scripts - LoadLastCheckpoint - which was checked against the 2.31 type
// hierarchy rather than assumed. So the template cannot be requested; it has to BE the
// last checkpoint.
//
// Hence this. The template is installed as an ordinary save folder and its timestamps are
// stamped to now immediately before the game starts, which makes it the newest save and
// therefore the one LoadLastCheckpoint picks. The launcher runs before every launch, so it
// is re-stamped every time and autosaves written during a session cannot displace it.
//
// It is a trick, and it is honest about being one. The alternative is finding the native
// load-by-name function by offset, which is real reverse-engineering for a result this
// achieves in twenty lines.
// ---------------------------------------------------------------------------

const TEMPLATE_SAVE_NAME = 'MultiplayerStart'

// Two templates, because body type cannot be changed in game.
//
// Ripperdocs change appearance - everything except body gender - and the mirror cannot
// either. So the body a player has is decided entirely by which save the world was built
// from, which means it has to be chosen BEFORE the game starts. There is nowhere else to
// put this: by the time anybody is in the world it is already too late.
//
// Named separately rather than one file with a flag inside, so adding a template is
// dropping a save into publish/ rather than a code change.
const TEMPLATE_URLS = {
  female: `https://github.com/${GITHUB_REPO}/releases/latest/download/character-template.zip`,
  male: `https://github.com/${GITHUB_REPO}/releases/latest/download/character-template-male.zip`
}

// Which body the player asked for. Female is the default only because it is the template
// that exists today, not as a statement about anything.
function chosenBodyType () {
  return loadSettings().bodyType === 'male' ? 'male' : 'female'
}

function savesDir () {
  // Not Documents. Cyberpunk uses the Saved Games known folder, under a CD Projekt Red
  // subdirectory - which is not where the obvious guess puts it, and getting this wrong
  // means silently installing the template somewhere the game never looks.
  return path.join(app.getPath('home'), 'Saved Games', 'CD Projekt Red', 'Cyberpunk 2077')
}

function templateDir () {
  return path.join(savesDir(), TEMPLATE_SAVE_NAME)
}

async function ensureTemplateSave () {
  const target = templateDir()
  const wanted = chosenBodyType()

  // Re-installed when the player changes body type, because the installed save IS the
  // body. Checking only for existence would silently leave somebody who picked male
  // playing the female template forever, with no way to tell why.
  const stampPath = path.join(target, '.bodytype')
  const installed = existsSync(path.join(target, 'sav.dat'))
  const current = installed && existsSync(stampPath)
    ? readFileSync(stampPath, 'utf8').trim()
    : null

  if (installed && current === wanted) return { installed: true, fresh: false, bodyType: wanted }

  const saves = savesDir()
  if (!existsSync(saves)) {
    // No saves folder at all means the game has never been run. Creating it ourselves is
    // fine - the game reads whatever is there.
    await fsp.mkdir(saves, { recursive: true })
  }

  const response = await axios.get(TEMPLATE_URLS[wanted], {
    responseType: 'arraybuffer',
    timeout: 60000,
    validateStatus: (status) => status === 200 || status === 404
  })

  // A body type with no template published yet. Falling back to the one that does exist
  // beats refusing to launch - they play as the wrong body rather than not at all - but it
  // is said out loud, because silently ignoring somebody's choice is worse than the wait.
  if (response.status === 404) {
    if (installed) {
      return { installed: true, fresh: false, bodyType: current, requested: wanted, missing: true }
    }
    return { installed: false, reason: `no ${wanted} template published yet` }
  }

  const zipPath = path.join(app.getPath('temp'), `character-template-${wanted}.zip`)
  await fsp.writeFile(zipPath, Buffer.from(response.data))

  // Cleared first. Extracting over an existing template would leave the previous body's
  // files behind when the two archives ever differ in contents.
  if (installed) {
    await fsp.rm(target, { recursive: true, force: true })
  }

  const zip = new AdmZip(zipPath)
  await fsp.mkdir(target, { recursive: true })
  zip.extractAllTo(target, true)

  // Records WHICH body is installed, so a later change of mind is noticed.
  await fsp.writeFile(stampPath, wanted)

  // And WHEN, which is what tells us later whether the player has built a world of their
  // own since. The folder's own timestamp cannot answer that - stampTemplateNewest
  // rewrites it to the future on every launch - so the real install time is recorded
  // separately and never touched again.
  await fsp.writeFile(path.join(target, '.installed-at'), String(Date.now()))

  return { installed: existsSync(path.join(target, 'sav.dat')), fresh: true, bodyType: wanted }
}

/**
 * When the template was installed, or null if that is not recorded.
 */
function templateInstalledAt () {
  const target = templateDir()
  const marker = path.join(target, '.installed-at')

  if (existsSync(marker)) {
    const value = Number(readFileSync(marker, 'utf8').trim())
    if (Number.isFinite(value)) return value
  }

  if (!existsSync(path.join(target, 'sav.dat'))) return null

  // A template installed before this marker existed, which is everyone playing today.
  //
  // Its real install time is unrecoverable: stampTemplateNewest rewrites every timestamp
  // in the folder to the future on every launch, including the .bodytype file, so nothing
  // in there remembers when it arrived.
  //
  // Backdated a week, which resolves the case that actually matters. Anyone affected by
  // the identity bug has played recently - that is how they noticed - so their own saves
  // land inside the window and they stop being handed the template. Someone genuinely new
  // is not in this branch at all, because their template installs fresh and passes its
  // real timestamp.
  const assumed = Date.now() - 7 * 24 * 60 * 60 * 1000

  try {
    writeFileSync(marker, String(assumed))
  } catch (err) {
    console.warn('[template] could not record an install time:', err.message)
  }

  return assumed
}

/**
 * Has this player made a world of their own since the template was installed?
 *
 * This is the question behind the worst bug the project has had. The template is ONE save,
 * built from one person's character, and it was being forced to the front on every single
 * launch - so MULTIPLAYER loaded it every time and everybody arrived wearing the character
 * it was made from. Two players reported it as "I spawned in as your old character", and
 * hyliangenesis found the workaround that proves the diagnosis exactly: loading their own
 * save from the singleplayer menu and then connecting with '/' produced the right
 * character every time.
 *
 * The template's job is to give somebody with NO character a world past Act 1 to arrive
 * in. Once they have been through NEW CHARACTER, the game has written saves of their own
 * and those are the ones that hold who they are. Continuing to override them is what
 * turned a bootstrap into an identity swap.
 *
 * Saves older than the install are ignored on purpose. Everyone has singleplayer saves
 * from before any of this, and loading a random one of those is the behaviour the template
 * was introduced to stop.
 */
async function hasOwnWorldSince (installedAt) {
  if (!installedAt) return false

  const saves = savesDir()
  if (!existsSync(saves)) return false

  const template = templateDir()

  let entries
  try {
    entries = await fsp.readdir(saves, { withFileTypes: true })
  } catch (err) {
    console.warn('[template] could not read the saves folder:', err.message)
    return false
  }

  for (const entry of entries) {
    if (!entry.isDirectory()) continue

    const full = path.join(saves, entry.name)
    if (full === template) continue

    // A folder is only a save if the game wrote one there.
    if (!existsSync(path.join(full, 'sav.dat'))) continue

    try {
      const stat = await fsp.stat(path.join(full, 'sav.dat'))
      if (stat.mtimeMs > installedAt) return true
    } catch { /* unreadable save - treat as not theirs */ }
  }

  return false
}

/**
 * Makes the template the newest save, so LoadLastCheckpoint chooses it.
 *
 * Both the folder and the files inside it - the game sorts on one of them and which is not
 * documented, so both are set rather than guessing and being subtly wrong.
 */
async function stampTemplateNewest () {
  const target = templateDir()
  if (!existsSync(target)) return false

  // A minute ahead, not now. Some of these writes take a moment and an autosave landing in
  // the same second would otherwise be a coin toss.
  const when = new Date(Date.now() + 60_000)

  try {
    for (const entry of await fsp.readdir(target)) {
      await fsp.utimes(path.join(target, entry), when, when)
    }
    await fsp.utimes(target, when, when)
    return true
  } catch (err) {
    console.warn('[template] could not stamp:', err.message)
    return false
  }
}

// Is a game running, or on its way up?
//
// Two copies of Cyberpunk cannot share one install: they fight over the same save folder
// and the same mod logs, and both connect to the server as the same account, which the
// server sees as one player teleporting between two positions. The launch button is
// disabled in the UI while this is true, and refused here as well - the UI is not a
// security boundary, and the check that matters is the one nothing can route around.
let gameState = { launching: false, running: false }
let gameWatcher = null

function setGameState (next) {
  gameState = { ...gameState, ...next }

  if (mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.webContents.send('game-state', gameState)
  }
}

/**
 * Watches for the game to close, then unlocks the button.
 *
 * Polls the process list rather than trusting the spawned child to represent the game.
 * It often does not: Cyberpunk is started through REDprelauncher, which exits almost
 * immediately after handing off, so the child exiting means nothing about whether anybody
 * is playing. Asking the OS what is running is the only answer that stays true.
 */
function watchForGameExit () {
  if (gameWatcher) clearInterval(gameWatcher)

  let sawItStart = false

  gameWatcher = setInterval(async () => {
    const running = await isProcessRunning('Cyberpunk2077.exe')

    if (running) {
      sawItStart = true
      if (!gameState.running || gameState.launching) {
        setGameState({ launching: false, running: true })
      }
      return
    }

    // Not running. Before it has ever appeared this is just the loading screen taking its
    // time, so the button stays locked - unlocking there would let someone start a second
    // copy during the slowest, most tempting part of the wait.
    if (!sawItStart) return

    clearInterval(gameWatcher)
    gameWatcher = null
    setGameState({ launching: false, running: false })
    console.log('[launch] the game has closed - unlocked')

    // The session just ended, so its log just stopped growing - ship it while the
    // machine is still on. Crashes are shipped by handleGameCrash; the in-flight
    // guard keeps the two from doubling up.
    shipClientLogs('game closed')
  }, 3000)
}

async function launchGame () {
  if (!currentUser) {
    // Belt and braces. The button is disabled in the UI, but the UI is not a
    // security boundary - a renderer could send this message anyway.
    throw new Error('Not signed in')
  }

  if (gameState.launching) {
    throw new Error('Already starting the game - give it a moment.')
  }

  // Asked of the OS, not just of our own flag. The flag only knows about launches this
  // launcher made: a game started from Steam, or one still running after the launcher was
  // restarted, is invisible to it and is exactly the case worth catching.
  if (gameState.running || await isProcessRunning('Cyberpunk2077.exe')) {
    setGameState({ launching: false, running: true })
    watchForGameExit()
    throw new Error('Cyberpunk 2077 is already running. Close it before launching again.')
  }

  setGameState({ launching: true })

  if (!currentUser.isMember) {
    throw new Error('You must join the Night City Online Discord to play.')
  }

  // Not installed at all is a different problem from out of date, and saying
  // "press Update" to someone who has never installed the mod just confuses them.
  if (lastUpdateCheck && !lastUpdateCheck.installed) {
    throw new Error('The mod is not installed yet. Use "Download the mod" at the bottom, then restart the launcher.')
  }

  if (!lastUpdateCheck || !(lastUpdateCheck.upToDate || lastUpdateCheck.offline)) {
    // Re-checked here rather than trusting the button state. An out-of-date client
    // against a newer server is the sort of mismatch that produces bug reports nobody
    // can reproduce, so it is refused outright.
    throw new Error('Your game files are out of date. Press Update first.')
  }

  // Do not send players to a server that is not there. They would sit at a black
  // screen, time out with no explanation, and report it as a broken mod.
  //
  // Admins are exempt on purpose: starting the game against a dead server is exactly
  // what you do when testing, and the person who can start the server is the person
  // who should be allowed to launch into nothing.
  if (!lastServerStatus?.online && !isAdmin()) {
    throw new Error('The server is offline right now. Check the Discord for when it is back up.')
  }

  // The world template, installed and made the newest save, immediately before launch.
  //
  // This is what stops multiplayer touching anybody's own saves: MULTIPLAYER loads the
  // last checkpoint, so making the template the last checkpoint means it loads that and
  // leaves every save the player actually cares about alone.
  //
  // Deliberately not fatal. A player who cannot get the template still gets into the game
  // on their own save - which is exactly how it worked before this existed - and the
  // server still owns their character either way. Refusing to launch over it would trade a
  // cosmetic problem for a total one.
  try {
    const template = await ensureTemplateSave()

    if (template.installed) {
      // Every launch now, deliberately - and this is a reversal.
      //
      // It used to be conditional (see hasOwnWorldSince), because forcing the template
      // made everybody spawn as the character it was built from and overrode characters
      // people had just created. That was the right fix at the time and the reason for it
      // has since gone: the server now owns appearance, name, body, position, inventory
      // and cyberware, and overwrites every one of them on arrival. The template cannot
      // leak an identity any more, because nothing it contains about who you are survives
      // contact with the server.
      //
      // What it buys is that everybody boots into the SAME world. Before this, the save
      // used to reach multiplayer was whatever that person last played - so two players
      // stood in one session with different quest states, different open doors, different
      // vehicles, and no way to tell which differences were bugs. The template makes the
      // starting world a constant instead of a variable.
      //
      // Their own saves are untouched - this changes which one is newest, nothing else.
      // Singleplayer is exactly as they left it.
      //
      // Still template-derived, and worth being honest about: quest progress, perk and
      // skill allocation, street cred, owned vehicles and apartments. The server holds
      // level and points and applies those on top, but not where they were spent. That is
      // the next thing to move, not something this pretends to have solved.
      await stampTemplateNewest()
      console.log('[template] multiplayer starts from the shared template - the server owns the character')
    } else {
      console.warn('[template] not available:', template.reason || 'unknown')
    }
  } catch (err) {
    console.warn('[template] could not be prepared:', err.message)
  }

  // Pass the TOKEN, not the id.
  //
  // An id is just a number - the game would be asserting "I am 1234", which any
  // player could edit. The token is a credential only Discord can vouch for, and
  // the game server verifies it independently on connect. This is the difference
  // between the launcher claiming an identity and the server establishing one.
  //
  // Note the '=' form: the game's argument parser silently ignores space-separated
  // values, leaving the setting at its default with no error anywhere.
  const args = [
    ...GAME_ARGS,
    '--online',
    `--discord-token=${accessToken}`,
    // The display name, so chat shows who you are before Discord verification is
    // switched on server-side. Once it is, the SERVER overwrites this with the name
    // Discord returns - a name the client sends is a claim, not an identity.
    // The HANDLE (noremacxxi), not the display name (Noremac).
    //
    // Handles are unique across Discord; display names are not. Two people called
    // "Ghost" in chat is a moderation problem, and "who was that?" needs to have one
    // answer. The display name is still shown in the launcher header, where it is
    // decoration rather than identity.
    `--discord-name=${currentUser.handle}`
  ]

  // Developer overlay, off unless an admin has turned it on. Checked against isAdmin()
  // here rather than trusting the saved setting alone - the settings file is plain JSON
  // on disk, so "debug: true" in it proves nothing about who is running the launcher.
  if (isAdmin() && loadSettings().debugMode) {
    args.push('--debug')
  }

  // ALWAYS pass the address, even the fallback.
  //
  // Previously this was only passed when an environment variable was set, so for everyone
  // else the game silently used its own 127.0.0.1 default - connecting to their own PC
  // and timing out with nothing in the log explaining why.
  const server = await resolveServer()

  // Refuse the launch that was always going to fail.
  //
  // Reaching 'fallback' means every real source was unavailable: nothing saved in
  // Settings, server.json unfetchable, no MP_SERVER. The address left is 127.0.0.1, which
  // is right for whoever is hosting and wrong for everybody else - and the player was
  // never told which of those they are. They launched, waited through a load screen,
  // connected to their own PC, and timed out with nothing anywhere explaining it.
  //
  // The check is what makes this safe to be strict about: if a server really is listening
  // here, this is a host testing locally and the launch proceeds untouched.
  if (server.source === 'fallback' && !(await isServerListeningLocally(server.port))) {
    console.warn('[launch] refused - address resolution fell through to 127.0.0.1 with nothing listening')

    throw new Error(
      'Could not find out where the server is, so there is nothing to connect to.\n\n' +
      'The address normally comes from the latest release, which means this is usually a ' +
      'connection problem at this end - check your internet and try again in a minute.\n\n' +
      'If it keeps happening, ask in the Discord: the address may need republishing. ' +
      '(An admin can set one by hand in Settings to get in meanwhile.)'
    )
  }

  args.push(`--ip=${server.host}`)
  args.push(`--port=${server.port}`)

  console.log(`[launch] server ${server.host}:${server.port} (from ${server.source})`)
  launcherLog(`launching: server ${server.host}:${server.port} (${server.source}), ` +
              `${args.length} args, token ${accessToken ? 'present' : 'MISSING'}, ` +
              `name ${currentUser?.handle || 'MISSING'}`)

  // detached + unref lets the game outlive the launcher. Without it, closing
  // the launcher would take the game down with it, and the launcher would sit
  // in memory for the whole session doing nothing.
  //
  // stdio 'ignore' matters too: with pipes, a game that writes a lot of output
  // eventually fills the OS buffer and BLOCKS, because nobody is reading it.
  const exe = gameExecutable()
  if (!exe || !existsSync(exe)) {
    throw new Error('Could not find Cyberpunk 2077. Use "Locate game" to point at it.')
  }

  // A Steam copy launched while Steam itself is NOT running does not keep our
  // arguments: the game boots Steam and relaunches itself bare, so the mod reads no
  // --ip and dials 127.0.0.1 forever (phonix, five identical sessions, 2026-08-21 -
  // every log says "not launched from the launcher" even when it was). Refusing with
  // the reason beats launching into a guaranteed dead end.
  if (/steamapps/i.test(exe)) {
    const steamUp = await new Promise((resolve) => {
      const check = spawn('tasklist', ['/FI', 'IMAGENAME eq steam.exe', '/NH'], { windowsHide: true })
      let out = ''
      check.stdout.on('data', (c) => { out += c.toString() })
      check.on('close', () => resolve(/steam\.exe/i.test(out)))
      check.on('error', () => resolve(true)) // cannot tell - do not block on a guess
    })

    if (!steamUp) {
      throw new Error('Steam is not running. Start Steam, wait for it to sign in, then JACK IN - ' +
                      'a Steam copy launched without Steam restarts itself and loses the ' +
                      'multiplayer connection settings.')
    }

    launcherLog('steam guard: steam.exe running - proceeding')
  }

  // Boot policy self-heal, deliberately not awaited: an installed Fast Launch is a
  // cheap no-op check, and a missing one downloading in the background must not
  // delay the JACK IN that was just pressed - it lands for the next boot.
  ensureFastLaunch()

  // Last thing before starting: make sure only one copy of the mod will load.
  //
  // Here rather than at install time because a duplicate does not have to arrive through
  // the launcher - a developer's junction, a manual unzip, a copy restored by a backup
  // tool. The only moment it is certainly true is the moment before the game reads them.
  //
  // Non-fatal by design. A launch with a duplicate still present is a launch that might
  // run old scripts; a launch refused over it is definitely no game at all.
  let duplicates = { moved: [] }

  try {
    duplicates = await resolveDuplicateInstalls(loadSettings().installedVersion)

    if (duplicates.moved.length && mainWindow && !mainWindow.isDestroyed()) {
      const names = duplicates.moved.map((m) => path.basename(m.from)).join(', ')
      mainWindow.webContents.send('mods-cleaned', {
        moved: duplicates.moved,
        parked: duplicates.parked,
        message: `Found another copy of the mod (${names}) and moved it aside - the game ` +
                 'loads every copy it finds, so the old one would have overridden this update.'
      })
    }
  } catch (err) {
    console.warn('[mods] duplicate check failed:', err.message)
  }

  const sendLaunchError = (message) => {
    if (mainWindow) {
      mainWindow.webContents.send('launch-error', message)
    }
  }

  // Watch for a crash.
  //
  // The child is detached and unref'd so the game outlives the launcher, but as long as
  // the launcher IS still open we can still see it exit - and that is exactly when
  // someone needs their log. Nobody should have to go hunting through Program Files for
  // a file whose name they do not know.
  const onExit = (code) => {
    launcherLog(`game exited with code ${code}`)

    // 0 is a normal quit. 0xC0000005 (-1073741819) is an access violation - the crash
    // this project has spent days on. Anything else non-zero is also worth capturing.
    if (code === 0 || code === null) return

    handleGameCrash(code)
  }

  const child = spawn(exe, args, {
    detached: true,
    stdio: 'ignore'
  })

  child.on('error', (err) => {
    // EACCES here rarely means the file is unreadable - the launcher just FOUND it.
    // It means CreateProcess was refused: a "Run as administrator" compatibility flag
    // on Cyberpunk2077.exe (a UAC boundary Node cannot cross), restrictive ACLs on an
    // unusual drive (seen live: a Steam library on B:), or an antivirus interposing.
    // The Windows SHELL can cross the first case - it shows the UAC prompt instead of
    // failing - so retry through cmd's `start`, which routes via ShellExecute.
    if (err.code === 'EACCES' || err.code === 'EPERM' || err.code === 'UNKNOWN') {
      console.warn(`[launch] direct start refused (${err.code}) - retrying through the shell, which can raise a UAC prompt`)
      launcherLog(`spawn refused (${err.code}) - retrying through the shell`)

      // One pre-quoted command line under windowsVerbatimArguments, because cmd has
      // its own quoting rules and Node's default escaping garbles them. The empty ""
      // is start's window-title slot - without it, start would treat the quoted exe
      // path as the title and run nothing.
      //
      // The ARGUMENTS are NOT quoted. They contain no spaces, and the game's own
      // command-line parser does not strip quotes - a quoted token never matches
      // "--ip=", so a quoted argument is an IGNORED argument. That was phonix's whole
      // night, 2026-08-21: JACK IN worked, EACCES sent him through this path, and the
      // game launched cleanly with every argument eaten - "not launched from the
      // launcher", dialing 127.0.0.1, six sessions straight. Only the exe path keeps
      // quotes; it is the one token with spaces, and cmd - not the game - consumes it.
      const line = ['/c', 'start', '""', `"${exe}"`, ...args].join(' ')
      const retry = spawn('cmd.exe', [line], {
        detached: true,
        stdio: 'ignore',
        windowsVerbatimArguments: true,
        cwd: path.dirname(exe)
      })

      // The exit we can watch here is cmd's, not the game's - it leaves as soon as
      // ShellExecute hands off, always with 0, so onExit stays quiet and crash capture
      // for this session comes from the shipped logs instead. watchForGameExit() still
      // tracks the real process by name either way.
      retry.on('error', (err2) => {
        sendLaunchError(
          `Windows refused to start the game twice (${err.code}, then ${err2.code}). ` +
          'Usual causes: "Run as administrator" is ticked on Cyberpunk2077.exe ' +
          '(right-click it > Properties > Compatibility - untick it), or your ' +
          'antivirus is blocking the launcher from starting programs. The game files ' +
          'themselves are fine.'
        )
      })
      retry.on('exit', onExit)
      retry.unref()
      return
    }

    sendLaunchError(err.message)
  })

  child.on('exit', onExit)

  child.unref()

  // The button stays locked from here until the game is gone from the process list.
  watchForGameExit()

  return { launched: true, executable: exe, cleaned: duplicates.moved.length }
}

// ---------------------------------------------------------------------------
// Server controls (admin only)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Discord roles, shared with the game
//
// Permissions used to be decided in two unrelated places: Discord roles in game, and a
// hardcoded list of one Discord id here. So somebody with the dev role had staff commands
// in the world and no server controls in the launcher that starts it.
//
// The server now publishes what each role resolves to as roles.json, alongside the release
// - the same route modlist.json and server.json already take. Both halves read the same
// table.
//
// ADMIN_DISCORD_IDS stays as a floor. If the role map is unreachable, or Discord is having
// a bad day, Cam must not be locked out of the controls for his own server.
// ---------------------------------------------------------------------------

const ROLES_URL = `https://github.com/${GITHUB_REPO}/releases/latest/download/roles.json`

const LEVELS = { player: 0, moderator: 1, admin: 2, owner: 3 }

let roleMap = null            // { owner, roles: [{ id, name, level }] }
let roleMapFetchedAt = 0

async function getRoleMap () {
  // Cached for ten minutes. Roles change rarely and this runs on every permission check.
  if (roleMap && Date.now() - roleMapFetchedAt < 10 * 60 * 1000) return roleMap

  try {
    const response = await axios.get(ROLES_URL, {
      timeout: 8000,
      // 404 is an answer: no role map has been published yet.
      validateStatus: (status) => status === 200 || status === 404
    })

    roleMap = response.status === 200 ? response.data : { roles: [] }
  } catch {
    roleMap = roleMap || { roles: [] }
  }

  roleMapFetchedAt = Date.now()
  return roleMap
}

/**
 * The signed-in user's permission level, from their Discord roles.
 *
 * Uses the same guilds.members.read scope the launcher already asks for, and the same
 * member endpoint the game server uses - so the two cannot disagree about what somebody's
 * roles are, only about how fresh the answer is.
 */
async function resolveUserLevel () {
  if (!currentUser || !currentUser.id) return 'player'

  const map = await getRoleMap()

  if (map.owner && map.owner === currentUser.id) return 'owner'

  // "The lookup FAILED" and "Discord answered: no roles" are different verdicts, and
  // only the second may demote anyone. A dead network, an expired OAuth token, or
  // roles.json fetched in the middle of a release upload used to all collapse to
  // 'player' - the launcher's own admin losing his server buttons to a hiccup, twice,
  // live. On failure the last DEFINITIVE answer for this account stands instead.
  const cachedFor = loadSettings().roleLevelCache
  const cached = (cachedFor && cachedFor.id === currentUser.id) ? cachedFor.level : null
  const fallback = (why) => {
    if (cached) {
      launcherLog(`role lookup failed (${why}) - standing on cached level '${cached}'`)
      return cached
    }
    return 'player'
  }

  if (!map.roles || !map.roles.length) return fallback('role map unavailable')

  const token = loadToken()
  if (!token) return fallback('no OAuth token on hand')

  let memberRoles = []
  try {
    const member = await axios.get(
      `https://discord.com/api/v10/users/@me/guilds/${map.guildId || GUILD_ID}/member`,
      { headers: { Authorization: `Bearer ${token}` }, timeout: 8000,
        validateStatus: (status) => status === 200 || status === 404 })

    if (member.status === 404) {
      // Definitive: not a member of the guild. No cache rescue.
      saveSettings({ roleLevelCache: { id: currentUser.id, level: 'player' } })
      return 'player'
    }
    if (Array.isArray(member.data?.roles)) memberRoles = member.data.roles
  } catch (err) {
    return fallback(err.response?.status ? `Discord answered ${err.response.status}` : err.message)
  }

  let best = 'player'
  for (const role of map.roles) {
    if (!memberRoles.includes(role.id)) continue
    if ((LEVELS[role.level] || 0) > (LEVELS[best] || 0)) best = role.level
  }

  // A definitive answer, from Discord's own mouth - remember it for the next hiccup.
  saveSettings({ roleLevelCache: { id: currentUser.id, level: best } })

  return best
}

// The cached answer, refreshed after sign-in. isAdmin is called from synchronous paths -
// including render - so it cannot await, and a permission check that fires a network
// request per call would be its own problem.
let cachedLevel = 'player'

async function refreshUserLevel () {
  cachedLevel = await resolveUserLevel()
  return cachedLevel
}

// The admin stack (server lifecycle row, admin link, credentials, coordination
// controls) is a full card taller than what players see - a window sized for players
// cuts it off, and the owner was hand-stretching every session ("windows rezing still
// happening"). Grow to fit the role once it resolves, clamped to the screen, and
// never fight a size the person chose themselves: only a window still at the height
// WE last set gets adjusted.
let lastAutoHeight = 0

function fitWindowToRole () {
  if (!mainWindow || mainWindow.isDestroyed()) return
  try {
    const { screen } = require('electron')
    const work = screen.getDisplayMatching(mainWindow.getBounds()).workAreaSize
    const wanted = Math.min(isAdmin() ? 940 : 800, work.height - 24)
    const [w, h] = mainWindow.getSize()
    if (wanted > h && (lastAutoHeight === 0 || h === lastAutoHeight)) {
      mainWindow.setSize(w, wanted)
      lastAutoHeight = wanted
    }
  } catch { /* a sizing nicety must never break sign-in */ }
}

function isAdmin () {
  if (!currentUser) return false
  if (ADMIN_DISCORD_IDS.includes(currentUser.id)) return true

  return (LEVELS[cachedLevel] || 0) >= LEVELS.admin
}

function serverExePath () {
  return path.join(getServerDir(), SERVER_EXE)
}

/**
 * Is the server process alive?
 *
 * Asks Windows rather than tracking a child handle, so it reports correctly even
 * for a server started before the launcher was opened - or one started by the
 * .bat, or by hand.
 */
function getServerStatus () {
  return new Promise((resolve) => {
    const check = spawn('tasklist', ['/FI', `IMAGENAME eq ${SERVER_EXE}`, '/NH'], {
      windowsHide: true
    })

    let output = ''
    check.stdout.on('data', (chunk) => { output += chunk.toString() })

    check.on('close', () => {
      resolve({
        running: output.includes(SERVER_EXE),
        exePath: serverExePath(),
        exists: existsSync(serverExePath())
      })
    })

    check.on('error', () => resolve({ running: false, exePath: serverExePath(), exists: false }))
  })
}

function startServer () {
  if (!existsSync(serverExePath())) {
    throw new Error(`Server not found at ${serverExePath()}`)
  }

  // Release builds refuse to start without these. The launcher never holds the
  // password - it inherits whatever the user set with setx, and passes nothing.
  if (!process.env.CYBERPUNKMP_ADMIN_USERNAME || !process.env.CYBERPUNKMP_ADMIN_PASSWORD) {
    throw new Error(
      'Admin credentials are not set. Run this once in a terminal, then reopen the launcher:\n' +
      'setx CYBERPUNKMP_ADMIN_USERNAME "admin"\n' +
      'setx CYBERPUNKMP_ADMIN_PASSWORD "your-password"')
  }

  // The server needs a REAL console window. With none attached, Swan never registers
  // its ConsoleLogger and WebApi throws on startup trying to unregister it - the
  // process starts and dies immediately, which looks exactly like "nothing happened".
  //
  // Node's detached:true does NOT do this on Windows. It maps to DETACHED_PROCESS,
  // which gives the child NO console at all - the opposite of what is needed here.
  // There is no Node option for CREATE_NEW_CONSOLE, so cmd's `start` is genuinely
  // the right tool.
  //
  // The empty "" is the fix for the earlier "Windows cannot find 'Server\'" error:
  // `start` treats a leading quoted argument as the window TITLE, so a quoted path
  // with no title before it gets swallowed as one. Passing an explicit empty title
  // first means the path is unambiguously the program to run.
  const child = spawn(`start "" "${serverExePath()}"`, {
    cwd: getServerDir(),
    detached: true,
    stdio: 'ignore',
    shell: true,
    windowsHide: false
  })

  child.unref()

  return { started: true }
}

function stopServer () {
  return new Promise((resolve, reject) => {
    const kill = spawn('taskkill', ['/IM', SERVER_EXE, '/F'], { windowsHide: true })

    kill.on('close', (code) => {
      // 128 means "no such process" - already stopped, which is the state we wanted.
      if (code === 0 || code === 128) resolve({ stopped: true })
      else reject(new Error(`Could not stop the server (exit ${code})`))
    })

    kill.on('error', reject)
  })
}

/**
 * Stop, wait for the process to actually be gone, then start.
 *
 * The waiting is the whole point. taskkill returns as soon as it has ASKED Windows to
 * end the process, not once it has. Starting immediately means the new server tries to
 * bind port 11778 while the old one still holds it, and you get a confusing bind error
 * that looks like a config problem.
 */
// ---------------------------------------------------------------------------
// The coordination API
//
// The assistants working on the mod talk to each other through a small service on this
// machine (code/coord-api). It was started by hand, which meant it quietly died on every
// reboot - and when it is down, the far end gets a bare connection-refused with no way to
// tell whether the service is off, the machine is off, or their key is wrong.
//
// Started alongside the game server, because the two are up and down together in practice:
// both live on this machine and both only matter while it is on.
//
// Only ever visible to an admin, and only when the source is actually present - a player
// running the launcher on their own PC has no repo and no business starting this.
// ---------------------------------------------------------------------------

const COORD_PORT = 11780

function coordApiPath () {
  if (process.env.NCO_COORD_API) return process.env.NCO_COORD_API

  // The server lives at <repo>/build/windows/x64/release, so the repo root is four
  // levels up. Derived rather than configured: one path to be wrong instead of two.
  const candidates = [
    path.resolve(getServerDir(), '..', '..', '..', '..', 'code', 'coord-api', 'server.js'),
    path.resolve(__dirname, '..', 'coord-api', 'server.js')
  ]

  return candidates.find((candidate) => existsSync(candidate)) || null
}

/**
 * Is the service actually answering?
 *
 * Asks it, rather than looking for a node process. Node runs on this machine for half a
 * dozen reasons and finding one proves nothing about whether THIS service is up - which is
 * exactly the confusion this panel exists to remove.
 */
async function getCoordStatus () {
  const script = coordApiPath()

  try {
    const response = await axios.get(`http://127.0.0.1:${COORD_PORT}/health`, { timeout: 2000 })
    return {
      available: Boolean(script),
      running: true,
      participants: response.data?.participants ?? null,
      max: response.data?.max ?? null,
      baseUrl: response.data?.baseUrl ?? null
    }
  } catch {
    return { available: Boolean(script), running: false }
  }
}

function startCoordApi () {
  const script = coordApiPath()
  if (!script) throw new Error('The coordination API source is not on this machine.')

  // A real window, titled, for the same reason the game server gets one: when it exits
  // immediately there has to be somewhere the reason is written down.
  const child = spawn(`start "Coordination API" node "${script}"`, {
    cwd: path.dirname(script),
    detached: true,
    stdio: 'ignore',
    shell: true,
    windowsHide: false
  })

  child.unref()

  return { started: true, script }
}

async function stopCoordApi () {
  // Found by port rather than by name. Killing every node process on the machine to stop
  // one service would take out whatever else the user happens to be running.
  return new Promise((resolve) => {
    const check = spawn('netstat', ['-ano', '-p', 'TCP'], { windowsHide: true })

    let output = ''
    check.stdout.on('data', (chunk) => { output += chunk.toString() })

    check.on('close', () => {
      const pids = new Set()

      for (const line of output.split('\n')) {
        if (!line.includes(`:${COORD_PORT}`) || !line.includes('LISTENING')) continue
        const pid = line.trim().split(/\s+/).pop()
        if (pid && pid !== '0') pids.add(pid)
      }

      if (!pids.size) return resolve({ stopped: false, reason: 'not running' })

      for (const pid of pids) {
        spawn('taskkill', ['/PID', pid, '/T', '/F'], { windowsHide: true })
      }

      resolve({ stopped: true, pids: [...pids] })
    })

    check.on('error', () => resolve({ stopped: false, reason: 'could not look up the port' }))
  })
}

/**
 * Best effort, and deliberately never fatal.
 *
 * Whether the assistants can talk to each other has nothing to do with whether players can
 * join, so a failure here must not turn into a failure to start the game server.
 */
async function startCoordApiQuietly () {
  try {
    if (!coordApiPath()) return

    const status = await getCoordStatus()
    if (status.running) return

    startCoordApi()
    console.log('[coord] started alongside the game server')
  } catch (err) {
    console.warn('[coord] could not start:', err.message)
  }
}

/**
 * Does THIS machine host the coordination feed?
 *
 * Having the source is not the same as owning the service. Any checkout has the source,
 * and a second instance is not a spare - it has its own participant keys and its own
 * append-only history, and the two diverge silently from the moment both are running.
 *
 * Holding the participant file is what makes a machine the host, so that is the question
 * asked. A fresh clone does not have one and stays out of the way; the machine that has
 * been running the feed all along keeps doing so.
 */
function hostsCoordApi () {
  const script = coordApiPath()
  if (!script) return false

  const dataDir = process.env.NCO_COORD_DATA || path.join(path.dirname(script), 'data')
  return existsSync(path.join(dataDir, 'participants.json'))
}

async function restartServer () {
  await stopServer()

  const deadline = Date.now() + 10000

  while (Date.now() < deadline) {
    const status = await getServerStatus()
    if (!status.running) {
      // A moment more for the socket to be released - the process disappearing from the
      // task list and the port becoming free are not quite the same instant.
      await new Promise((r) => setTimeout(r, 500))
      return startServer()
    }
    await new Promise((r) => setTimeout(r, 400))
  }

  throw new Error('The old server did not shut down within 10 seconds - stop it manually.')
}

// ---------------------------------------------------------------------------
// IPC - the only surface the UI can reach
// ---------------------------------------------------------------------------

ipcMain.handle('server:status', async () => {
  if (!isAdmin()) return { ok: false, error: 'Not permitted' }

  // A locally built server makes this machine a dev box - the panel controls that
  // process, exactly as before. Everyone else's panel reports the REAL server: the
  // deployment resolveServer() points at, which runs itself (redeploys from GitHub
  // within minutes of a push, restarts automatically if it crashes) and is
  // administered through its own web panel rather than by starting an exe here.
  if (existsSync(serverExePath())) {
    return { ok: true, mode: 'local', ...(await getServerStatus()) }
  }

  const server = await resolveServer()
  const remote = await getGameServerStatus()
  return {
    ok: true,
    mode: 'remote',
    running: remote.online && remote.state === 'running',
    state: remote.state,
    players: remote.players,
    host: server.host,
    port: server.port,
    hasAdminCred: Boolean(getServerAdminCred())
  }
})

// The server's admin login, remembered so lifecycle buttons are one click rather than
// a password prompt every time. Encrypted the same way the Discord token is; if the OS
// cannot encrypt, it is not stored at all.
function getServerAdminCred () {
  const stored = loadSettings().serverAdminCred
  if (!stored || !safeStorage.isEncryptionAvailable()) return null
  try {
    const [username, password] = safeStorage.decryptString(Buffer.from(stored, 'base64')).split('\n')
    return { username, password }
  } catch {
    return null
  }
}

ipcMain.handle('server:setAdminCred', (_event, username, password) => {
  if (!isAdmin()) return { ok: false, error: 'Not permitted' }
  if (!safeStorage.isEncryptionAvailable()) {
    return { ok: false, error: 'This machine cannot store the login securely.' }
  }
  if (!username || !password) return { ok: false, error: 'Both fields are needed.' }

  saveSettings({ serverAdminCred: safeStorage.encryptString(`${username}\n${password}`).toString('base64') })
  return { ok: true }
})

// Start, stop or restart the REAL server, wherever it runs. The server itself enforces
// the admin login; this just carries it. A 401 forgets the stored login, because a
// wrong credential that keeps being resent is a lockout waiting to happen.
ipcMain.handle('server:remote', async (_event, action) => {
  if (!isAdmin()) return { ok: false, error: 'Not permitted' }
  if (!['start', 'stop', 'restart'].includes(action)) return { ok: false, error: 'Unknown action' }

  const cred = getServerAdminCred()
  if (!cred) return { ok: false, needCred: true }

  const server = await resolveServer()
  try {
    await axios.post(`http://${server.host}:${server.port}/api/v1/admin/${action}`, null, {
      auth: cred,
      timeout: 8000
    })
    return { ok: true, action }
  } catch (err) {
    if (err.response && err.response.status === 401) {
      saveSettings({ serverAdminCred: undefined })
      return { ok: false, needCred: true, error: 'Wrong admin login - enter it again.' }
    }
    return { ok: false, error: err.message }
  }
})

// Opens the server's own web admin panel - status, plugins, admin actions - which is
// served by the server itself and guarded by its admin credentials.
ipcMain.handle('server:openAdmin', async () => {
  if (!isAdmin()) return { ok: false, error: 'Not permitted' }
  const server = await resolveServer()
  shell.openExternal(`http://${server.host}:${server.port}/`)
  return { ok: true }
})

ipcMain.handle('server:start', async () => {
  if (!isAdmin()) return { ok: false, error: 'Not permitted' }
  try {
    const result = startServer()

    // Alongside, not before: the game server is what people are waiting on.
    startCoordApiQuietly()

    return { ok: true, ...result }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

ipcMain.handle('coord:status', async () => {
  if (!isAdmin()) return { ok: false, error: 'Not permitted' }
  return { ok: true, ...(await getCoordStatus()) }
})

ipcMain.handle('coord:start', async () => {
  if (!isAdmin()) return { ok: false, error: 'Not permitted' }
  try {
    return { ok: true, ...startCoordApi() }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

ipcMain.handle('coord:stop', async () => {
  if (!isAdmin()) return { ok: false, error: 'Not permitted' }
  try {
    return { ok: true, ...(await stopCoordApi()) }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

ipcMain.handle('server:stop', async () => {
  if (!isAdmin()) return { ok: false, error: 'Not permitted' }
  try {
    return { ok: true, ...(await stopServer()) }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

ipcMain.handle('server:restart', async () => {
  if (!isAdmin()) return { ok: false, error: 'Not permitted' }
  try {
    const result = await restartServer()

    // Deliberately not stopped by server:stop. Rebuilding the game server should not
    // silence the channel the assistants are talking on - but a restart is a good moment
    // to bring it back if it had died.
    startCoordApiQuietly()

    return { ok: true, ...result }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

ipcMain.handle('discord:login', async () => {
  try {
    await signInWithDiscord()
    saveToken(accessToken)
    return { ok: true, user: publicProfile() }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

// Restores a previous session on startup, so signing in is a one-time thing rather
// than something to repeat every launch.
ipcMain.handle('discord:restore', async () => {
  const saved = loadToken()
  if (!saved) return { ok: false }

  try {
    accessToken = saved
    await hydrateUserFromToken()
    return { ok: true, user: publicProfile() }
  } catch {
    // Expired or revoked. Clear it and fall back to the sign-in button rather than
    // showing a session that no longer works.
    accessToken = null
    currentUser = null
    saveToken(null)
    return { ok: false }
  }
})

ipcMain.handle('discord:logout', () => {
  accessToken = null
  currentUser = null
  lastUpdateCheck = null
  saveToken(null)
  return { ok: true }
})

// Developer overlay toggle. Reports admin status too, so the renderer knows whether to
// show the control at all - and refuses to enable it for anyone else, because a renderer
// asking nicely is not authorisation.
ipcMain.handle('debug:get', async () => {
  return { ok: true, admin: isAdmin(), enabled: Boolean(loadSettings().debugMode) }
})

ipcMain.handle('debug:set', async (_event, enabled) => {
  if (!isAdmin()) return { ok: false, error: 'Not authorised.' }

  saveSettings({ debugMode: Boolean(enabled) })
  return { ok: true, enabled: Boolean(enabled) }
})

// ---------------------------------------------------------------------------
// Mod list
//
// A curated list of Nexus mods the server expects everyone to be running. The launcher
// installs and verifies against it, so a session is not half the players on one set of
// mods and half on another.
//
// It does NOT host anything. Those mods belong to their authors and are not ours to
// redistribute - which is exactly why this points at Nexus instead. The six prerequisites
// in FullInstall.zip are different: they are MIT and redistribution is permitted.
//
// The download path is the one Nexus sanctions. Their API refuses download links to
// non-Premium accounts outright, so a launcher cannot fetch files on a free user's
// behalf. Instead the browser hands us an nxm:// link when they press "Mod Manager
// Download" on the mod page, and we take it from there - the same route Vortex uses.
// ---------------------------------------------------------------------------

const MODLIST_URL = `https://github.com/${GITHUB_REPO}/releases/latest/download/modlist.json`

/**
 * The Nexus API key, encrypted exactly like the Discord token.
 *
 * A personal API key, not a password - but it acts as the account for API purposes, so
 * it gets the same DPAPI treatment. Plain text on disk would let anyone with file access
 * use someone's Nexus account, and a leaked key is grounds for Nexus to revoke it.
 */
function loadNexusKey () {
  const stored = loadSettings().nexusKey
  if (!stored) return null

  try {
    return safeStorage.decryptString(Buffer.from(stored, 'base64'))
  } catch {
    return null
  }
}

function saveNexusKey (key) {
  if (!key) {
    saveSettings({ nexusKey: null, nexusName: null })
    return true
  }

  if (!safeStorage.isEncryptionAvailable()) return false

  saveSettings({ nexusKey: safeStorage.encryptString(key).toString('base64') })
  return true
}

/**
 * Sign in through Nexus itself, rather than asking anyone to find an API key.
 *
 * Nexus's SSO works over a WebSocket rather than a redirect, which is why it looks
 * nothing like the Discord flow internally:
 *
 *   1. open a socket to sso.nexusmods.com and send a UUID we generated
 *   2. open the Nexus authorise page in the browser with that same UUID
 *   3. the player presses Authorise there
 *   4. the API key arrives back down the socket we are still holding open
 *
 * The socket must stay open across the whole thing - it is the only channel the key
 * comes back on. There is no callback URL to listen on, so the local HTTP server used
 * for Discord is no help here.
 *
 * The key is never shown to the player and never reaches the page. It is stored the same
 * way as the Discord token: encrypted with DPAPI, tied to this Windows account.
 */
ipcMain.handle('nexus:ssoLogin', async () => {
  const uuid = crypto.randomUUID()

  return new Promise((resolve) => {
    let socket
    let settled = false

    const finish = (result) => {
      if (settled) return
      settled = true
      try { socket?.close() } catch {}
      resolve(result)
    }

    // Nobody should be left staring at a spinner because they closed the browser tab.
    const timeout = setTimeout(() => {
      finish({ ok: false, error: 'Timed out waiting for Nexus. Try again, or paste an API key instead.' })
    }, 180000)

    try {
      socket = new WebSocket('wss://sso.nexusmods.com')
    } catch (err) {
      clearTimeout(timeout)
      return finish({ ok: false, error: `Could not reach Nexus: ${err.message}` })
    }

    socket.addEventListener('open', () => {
      socket.send(JSON.stringify({ id: uuid, token: null, protocol: 2 }))

      // Opened only after the socket is up. Sending someone to the authorise page before
      // we are listening means approving into a void.
      shell.openExternal(`https://www.nexusmods.com/sso?id=${uuid}&application=nightcityonline`)
    })

    socket.addEventListener('message', async (event) => {
      let payload
      try {
        payload = JSON.parse(event.data)
      } catch {
        return
      }

      if (payload.success === false) {
        clearTimeout(timeout)
        return finish({ ok: false, error: payload.error || 'Nexus refused the sign-in.' })
      }

      const apiKey = payload?.data?.api_key
      if (!apiKey) return   // the connection_token message - not the key yet

      clearTimeout(timeout)

      try {
        const who = await axios.get('https://api.nexusmods.com/v1/users/validate.json', {
          headers: { apikey: apiKey, 'User-Agent': 'NightCityOnline-Launcher/1.0' },
          timeout: 15000
        })

        if (!saveNexusKey(apiKey)) {
          return finish({ ok: false, error: 'Encrypted storage is unavailable, so the key was not saved.' })
        }

        saveSettings({ nexusName: who.data?.name || 'Nexus user' })

        finish({ ok: true, name: who.data?.name, premium: Boolean(who.data?.is_premium) })
      } catch (err) {
        finish({ ok: false, error: `Nexus returned a key but rejected it: ${err.message}` })
      }
    })

    socket.addEventListener('error', () => {
      clearTimeout(timeout)
      finish({ ok: false, error: 'Lost the connection to Nexus. Try again, or paste an API key instead.' })
    })
  })
})

// Validates the key by asking Nexus who it belongs to, rather than storing whatever was
// pasted and failing later at download time with something unhelpful.
ipcMain.handle('nexus:signIn', async (_event, key) => {
  const trimmed = (key || '').trim()
  if (!trimmed) return { ok: false, error: 'Paste your Nexus API key first.' }

  try {
    const response = await axios.get('https://api.nexusmods.com/v1/users/validate.json', {
      headers: { apikey: trimmed, 'User-Agent': 'NightCityOnline-Launcher/1.0' },
      timeout: 15000
    })

    if (!saveNexusKey(trimmed)) {
      return { ok: false, error: 'Encrypted storage is unavailable, so the key was not saved.' }
    }

    saveSettings({ nexusName: response.data?.name || 'Nexus user' })

    return {
      ok: true,
      name: response.data?.name,
      premium: Boolean(response.data?.is_premium)
    }
  } catch (err) {
    if (err.response?.status === 401) return { ok: false, error: 'Nexus rejected that key.' }
    return { ok: false, error: `Could not reach Nexus: ${err.message}` }
  }
})

ipcMain.handle('nexus:status', async () => {
  const settings = loadSettings()
  return { ok: true, signedIn: Boolean(settings.nexusKey), name: settings.nexusName || null }
})

function installedModsPath () {
  return path.join(app.getPath('userData'), 'mods-installed.json')
}

function loadInstalledMods () {
  try {
    return JSON.parse(readFileSync(installedModsPath(), 'utf8'))
  } catch {
    return {}
  }
}

function saveInstalledMods (record) {
  try {
    writeFileSync(installedModsPath(), JSON.stringify(record, null, 2))
  } catch (err) {
    console.error('[mods] could not save install record:', err.message)
  }
}

async function fetchModList () {
  const response = await axios.get(MODLIST_URL, {
    headers: { 'User-Agent': 'NightCityOnline-Launcher' },
    timeout: 15000
  })

  const list = response.data
  return Array.isArray(list?.mods) ? list.mods : []
}

/**
 * Fills in a mod's name and summary from Nexus.
 *
 * The curated list carries IDs, not names, and this is why: a name written into the list
 * by hand is a name somebody had to read off a page and retype, which goes stale when the
 * author renames the mod and is a guess if nobody checked. Nexus is the authority on what
 * its own mods are called, so the launcher asks.
 *
 * Cached for the session. Twenty mods is twenty API calls per refresh otherwise, against
 * an API with a daily limit.
 */
const modInfoCache = new Map()

async function fetchModInfo (modId) {
  if (modInfoCache.has(modId)) return modInfoCache.get(modId)

  const apiKey = loadNexusKey()
  if (!apiKey) return null

  try {
    const response = await axios.get(
      `https://api.nexusmods.com/v1/games/cyberpunk2077/mods/${modId}.json`,
      { headers: { apikey: apiKey, 'User-Agent': 'NightCityOnline-Launcher/1.0' }, timeout: 15000 }
    )

    const info = {
      name: response.data?.name || null,
      summary: response.data?.summary || '',
      version: response.data?.version || null,
      author: response.data?.author || null
    }

    modInfoCache.set(modId, info)
    return info
  } catch {
    // Nexus being unreachable must not empty the list. Falling back to the id at least
    // tells someone which mod is meant.
    return null
  }
}

/**
 * Which file to install, when the list has not pinned one.
 *
 * Prefers the file Nexus marks as MAIN. A mod's file list is usually a main download plus
 * optional patches and translations, and grabbing the newest of everything would install
 * a Portuguese translation as if it were the mod.
 */
async function resolveMainFile (modId) {
  const apiKey = loadNexusKey()
  if (!apiKey) return null

  try {
    const response = await axios.get(
      `https://api.nexusmods.com/v1/games/cyberpunk2077/mods/${modId}/files.json?category=main`,
      { headers: { apikey: apiKey, 'User-Agent': 'NightCityOnline-Launcher/1.0' }, timeout: 15000 }
    )

    const files = response.data?.files || []
    const main = files.filter((f) => f.category_name === 'MAIN')
    const pick = (main.length > 0 ? main : files).sort((a, b) => (b.uploaded_timestamp || 0) - (a.uploaded_timestamp || 0))[0]

    return pick ? { fileId: pick.file_id, version: pick.version, name: pick.file_name } : null
  } catch {
    return null
  }
}

/**
 * What is on disk, judged by what we recorded installing.
 *
 * Recording the files we extracted is what makes uninstall possible at all - without it
 * "delete this mod" means guessing which files belonged to it, and guessing wrong means
 * deleting someone else's mod or leaving half of this one behind.
 */
function describeInstalledMod (mod, installed, gameDir) {
  const record = installed[String(mod.nexusModId)]

  if (!record) return { state: 'missing' }

  // Recorded as installed is not the same as still being there. People delete folders.
  const present = (record.files || []).every((relative) => existsSync(path.join(gameDir, relative)))
  if (!present) return { state: 'missing' }

  return {
    state: 'installed',
    fileId: record.fileId,
    version: record.version,
    files: record.files
  }
}

/**
 * Puts the list into install order - requirements before the things that need them.
 *
 * A mod installed before its framework does not fail in any helpful way: the game starts
 * and the mod is quietly absent, or it crashes somewhere unrelated hours later. Ordering
 * the list is half the fix; refusing to install out of order is the other half, because a
 * sorted list someone can click out of order is only a suggestion.
 *
 * A dependency cycle would loop forever, so anything still unplaced after a full pass is
 * appended as-is. A bad list should render awkwardly, not hang the launcher.
 */
function sortByDependencies (mods) {
  const byId = new Map(mods.map((m) => [String(m.nexusModId), m]))
  const placed = new Set()
  const ordered = []

  const place = (mod, seen) => {
    const id = String(mod.nexusModId)
    if (placed.has(id) || seen.has(id)) return

    seen.add(id)

    for (const need of mod.requires || []) {
      const dependency = byId.get(String(need))
      if (dependency) place(dependency, seen)
    }

    if (!placed.has(id)) {
      placed.add(id)
      ordered.push(mod)
    }
  }

  for (const mod of mods) place(mod, new Set())

  // Anything a cycle kept out.
  for (const mod of mods) {
    if (!placed.has(String(mod.nexusModId))) ordered.push(mod)
  }

  return ordered
}

ipcMain.handle('mods:list', async () => {
  try {
    const gameDir = findGameDir()
    if (!gameDir) return { ok: false, error: 'Cyberpunk 2077 not found. Set it in Settings.' }

    const all = sortByDependencies(await fetchModList())

    // devOnly mods are filtered out entirely for players, not shown-and-refused. On this
    // server the world is not synchronised, so a mod that changes it only changes it for
    // whoever installed it - listing those to everyone would create a world only some
    // people can see, which is worse than not offering it.
    const isAdmin = currentUser && ADMIN_DISCORD_IDS.includes(currentUser.id)
    const mods = all.filter((mod) => !mod.devOnly || isAdmin)

    const installed = loadInstalledMods()

    const states = new Map()
    for (const mod of mods) {
      states.set(String(mod.nexusModId), describeInstalledMod(mod, installed, gameDir))
    }

    // Names come from Nexus, in parallel - twenty sequential round trips is a visibly
    // slow list.
    const info = new Map()
    await Promise.all(mods.map(async (mod) => {
      info.set(String(mod.nexusModId), await fetchModInfo(mod.nexusModId))
    }))

    return {
      ok: true,
      signedIn: Boolean(loadNexusKey()),
      mods: mods.map((mod) => {
        // Named, not numbered. "Needs mod 107" tells nobody anything.
        const missing = (mod.requires || [])
          .filter((need) => states.get(String(need))?.state !== 'installed')
          .map((need) => mods.find((m) => String(m.nexusModId) === String(need))?.name || `mod ${need}`)

        const nexus = info.get(String(mod.nexusModId))

        return {
          id: String(mod.nexusModId),
          // Nexus first, then anything the list overrode, then the id. Never a guess.
          name: nexus?.name || mod.name || `Nexus mod ${mod.nexusModId}`,
          summary: mod.note || nexus?.summary || '',
          author: nexus?.author || null,
          latestVersion: nexus?.version || null,
          required: mod.required !== false,
          devOnly: Boolean(mod.devOnly),
          blockedBy: missing,
          nexusUrl: `https://www.nexusmods.com/cyberpunk2077/mods/${mod.nexusModId}`,
          ...states.get(String(mod.nexusModId))
        }
      })
    }
  } catch (err) {
    // An unreachable list must not stop anyone playing - they may already have every
    // mod installed. Report it and let the rest of the launcher carry on.
    return { ok: false, error: `Could not read the mod list: ${err.message}` }
  }
})

/**
 * Checks every mod is actually intact, file by file.
 *
 * Different from the list, which asks "did we record installing this". This asks whether
 * the files are still there and still what we wrote. People delete folders to fix things,
 * a game update can overwrite them, and a download interrupted halfway still leaves a
 * file behind. Any of those leave a mod that looks installed and is not.
 */
ipcMain.handle('mods:verify', async () => {
  const gameDir = findGameDir()
  if (!gameDir) return { ok: false, error: 'Cyberpunk 2077 not found. Set it in Settings.' }

  const installed = loadInstalledMods()
  const problems = []
  let checked = 0
  let intact = 0

  for (const [modId, record] of Object.entries(installed)) {
    checked++

    const missing = (record.files || []).filter((relative) => !existsSync(path.join(gameDir, relative)))

    if (missing.length > 0) {
      problems.push({
        id: modId,
        name: record.name || `mod ${modId}`,
        detail: `${missing.length} of ${record.files.length} file(s) missing`
      })
      continue
    }

    // Zero-length files are the signature of an interrupted write. They exist, so an
    // existence check passes, and the mod is broken anyway.
    const empty = (record.files || []).filter((relative) => {
      try { return statSync(path.join(gameDir, relative)).size === 0 } catch { return true }
    })

    if (empty.length > 0) {
      problems.push({ id: modId, name: record.name || `mod ${modId}`, detail: `${empty.length} empty file(s)` })
      continue
    }

    intact++
  }

  return { ok: true, checked, intact, problems }
})

// Opens the mod's Nexus page. From there "Mod Manager Download" comes back to us as an
// nxm:// link.
ipcMain.handle('mods:open', async (_event, modId) => {
  const mods = await fetchModList().catch(() => [])
  const mod = mods.find((m) => String(m.nexusModId) === String(modId))
  if (!mod) return { ok: false, error: 'That mod is not on the list any more.' }

  // Requirements are checked HERE, not only in the page. Hiding a button is a hint; this
  // is the rule. Nothing stops someone reaching this handler another way, and installing
  // a mod before its framework fails silently rather than loudly.
  const gameDir = findGameDir()
  const installed = loadInstalledMods()

  const missing = (mod.requires || [])
    .filter((need) => {
      const dependency = mods.find((m) => String(m.nexusModId) === String(need))
      return !dependency || !gameDir ||
             describeInstalledMod(dependency, installed, gameDir).state !== 'installed'
    })
    .map((need) => mods.find((m) => String(m.nexusModId) === String(need))?.name || `mod ${need}`)

  if (missing.length > 0) {
    return { ok: false, error: `Install ${missing.join(' and ')} first - ${mod.name} needs it.` }
  }

  // Land on the exact file where one is pinned; otherwise on the Files tab, resolving
  // the main file if Nexus will tell us. Sending someone to a mod's description page
  // leaves them hunting for the download button among optional patches.
  let fileId = mod.nexusFileId
  if (!fileId) {
    const main = await resolveMainFile(mod.nexusModId)
    fileId = main?.fileId
  }

  const url = fileId
    ? `https://www.nexusmods.com/cyberpunk2077/mods/${mod.nexusModId}?tab=files&file_id=${fileId}`
    : `https://www.nexusmods.com/cyberpunk2077/mods/${mod.nexusModId}?tab=files`

  await shell.openExternal(url)
  return { ok: true }
})

ipcMain.handle('mods:delete', async (_event, modId) => {
  const gameDir = findGameDir()
  if (!gameDir) return { ok: false, error: 'Cyberpunk 2077 not found.' }

  const installed = loadInstalledMods()
  const record = installed[String(modId)]
  if (!record) return { ok: false, error: 'That mod is not recorded as installed.' }

  // Warn if anything installed depends on this. Removing a framework from underneath
  // three working mods breaks all three, and the breakage shows up later as those mods
  // misbehaving - never as "you removed the thing they needed".
  const mods = await fetchModList().catch(() => [])
  const dependents = mods
    .filter((m) => (m.requires || []).some((need) => String(need) === String(modId)))
    .filter((m) => installed[String(m.nexusModId)])
    .map((m) => m.name)

  const detail = [`${(record.files || []).length} file(s) will be deleted from your game folder.`]
  if (dependents.length > 0) {
    detail.push('')
    detail.push(`WARNING: ${dependents.join(', ')} need${dependents.length === 1 ? 's' : ''} this and will stop working.`)
  }

  const { response } = await dialog.showMessageBox(mainWindow, {
    type: 'warning',
    title: 'Remove mod',
    message: `Remove ${record.name || 'this mod'}?`,
    detail: detail.join('\n'),
    buttons: ['Remove', 'Cancel'],
    defaultId: 1,
    cancelId: 1,
    noLink: true
  })

  if (response !== 0) return { ok: false }

  // Only the files WE recorded installing. Never a whole directory - mods share folders
  // like red4ext\plugins, and removing one must not take its neighbours with it.
  let removed = 0
  for (const relative of record.files || []) {
    const target = path.join(gameDir, relative)
    try {
      if (existsSync(target)) { rmSync(target, { force: true }); removed++ }
    } catch (err) {
      console.error('[mods] could not delete', target, err.message)
    }
  }

  delete installed[String(modId)]
  saveInstalledMods(installed)

  return { ok: true, removed }
})

ipcMain.handle('mod:pickDir', async () => {
  const result = await dialog.showOpenDialog(mainWindow, {
    title: 'Where is the mod installed?',
    properties: ['openDirectory'],
    message: 'Pick the folder containing CyberpunkMP.dll (usually red4ext\\plugins\\zzzCyberpunkMP)'
  })

  if (result.canceled || result.filePaths.length === 0) return { ok: false }

  const chosen = result.filePaths[0]

  // Checked before it is saved. Accepting a folder without the DLL would replace a
  // working automatic answer with a broken manual one, and the launcher would then
  // insist the mod is missing while pointing at the folder the player just chose.
  if (!existsSync(path.join(chosen, 'CyberpunkMP.dll'))) {
    return { ok: false, error: 'No CyberpunkMP.dll in that folder - pick the one containing it, usually zzzCyberpunkMP.' }
  }

  saveSettings({ modDir: chosen })
  return { ok: true, modDir: chosen }
})

// Opens the folder the launcher is actually installed in. It lives under AppData, which
// nobody should be expected to go digging through.
ipcMain.handle('launcher:openInstallDir', async () => {
  await shell.openPath(path.dirname(process.execPath))
  return { ok: true }
})

/**
 * Removes the launcher itself.
 *
 * NSIS puts an uninstaller next to the exe, but it is buried in AppData where nobody
 * will find it. This is the same uninstaller, reachable from inside the thing being
 * uninstalled.
 *
 * Two things this must get right. It confirms first - it is irreversible and one click
 * from a settings screen. And it QUITS: Windows cannot delete a running executable, so
 * an uninstaller launched from a live launcher would half-finish and leave the install
 * broken rather than gone.
 */
ipcMain.handle('launcher:uninstall', async () => {
  const dir = path.dirname(process.execPath)

  // Found by scanning, not by a hardcoded name. Two productNames exist (package.json
  // top-level says "Night City Online", the build block overrides with "...Launcher"),
  // so a hardcoded filename is one rename away from declaring every install portable.
  // The scan also keeps the answer honest for people RUNNING a portable copy while an
  // installed copy sits in AppData - the live case behind the 2026-08-21 report.
  let uninstaller = null
  try {
    const found = readdirSync(dir).find((f) => /^uninstall.*\.exe$/i.test(f))
    if (found) uninstaller = path.join(dir, found)
  } catch { /* unreadable dir - treat as portable below */ }

  const modDir = findModDir()

  const { response } = await dialog.showMessageBox(mainWindow, {
    type: 'warning',
    title: 'Uninstall Night City Online',
    message: 'Remove Night City Online from this PC - all of it?',
    detail:
      'THE RULE: after this, nothing of ours remains. Removed:\n\n' +
      '- the launcher program and its Windows "Apps" entry\n' +
      '- saved sign-in, settings and keys (all data folders, old versions included)\n' +
      '- updater caches, shortcuts, crash-log copies on the Desktop\n' +
      '- dead registry entries left by older installs\n' +
      (modDir ? `- the multiplayer mod:  ${modDir}\n` : '') +
      '- every Nexus mod this launcher installed (the frameworks stay)\n' +
      '\nCyberpunk 2077 itself, your saves, and the framework mods (RED4ext, ' +
      'redscript, Codeware...) are not touched. A fresh build installs cleanly ' +
      'afterwards.',
    buttons: ['Uninstall everything', 'Cancel'],
    defaultId: 1,
    cancelId: 1,
    noLink: true
  })

  if (response !== 0) return { ok: false }

  // The mod goes first, while the settings that locate it still exist.
  let note = ''
  try {
    if (modDir) {
      if (!isSafeModDir(modDir)) {
        note = `The mod folder looks wrong (${modDir}) - left alone. Delete red4ext\\plugins\\zzzCyberpunkMP by hand. `
      } else if (await isGameRunning()) {
        note = 'Cyberpunk is running, so the mod was left in place - close the game and use Remove mod. '
      } else {
        try { rmSync(modDir, { recursive: true, force: true }) } catch (err) { note = `The mod folder resisted deletion (${err.code || err.message}). ` }
      }
    }

    // Every Nexus mod this launcher installed is on record, file by file - THE RULE
    // covers what we put on the machine wherever we put it, so those files go too.
    // The prerequisite frameworks stay: FullInstall placed them for the game's
    // benefit and other, non-NCO mods may depend on them.
    try {
      const gameDir = findGameDir()
      const record = loadInstalledMods()
      if (gameDir && record) {
        const root = path.resolve(gameDir).toLowerCase()
        for (const entry of Object.values(record)) {
          for (const rel of entry.files || []) {
            const target = path.resolve(path.join(gameDir, rel))
            if (!target.toLowerCase().startsWith(root)) continue
            try { rmSync(target, { force: true }) } catch { /* locked - the purge report below covers honesty */ }
          }
        }
      }
    } catch { /* record unreadable - nothing to sweep */ }

    const failed = purgeResidue(collectResidue(true))
    if (failed.length) note += `Could not remove: ${failed.join(', ')}. `
  } catch (err) {
    // Whatever already got removed stays removed; the person hears what stopped it
    // rather than the IPC rejecting with the work half-done.
    note += `Cleanup stopped early: ${err.message}. `
  }

  if (!uninstaller) {
    // Portable copy: no NSIS uninstaller to hand over to, and everything else is
    // already gone. The exe someone double-clicks is the one thing a running program
    // cannot delete about itself.
    return {
      ok: true,
      message: note + 'All launcher data is wiped. This portable .exe is the last piece - close it and delete the file.'
    }
  }

  spawn(uninstaller, [], { detached: true, stdio: 'ignore' }).unref()
  app.quit()

  return { ok: true }
})

// Settings > Deep clean: the same manifest sweep the uninstaller runs, minus what a
// working install needs (its own data folder, its own registry row, live shortcuts).
// Everything it finds is by definition wreckage from OLD installs - the exact debris
// behind "fresh install fails": stale data folders under previous names, ghost rows
// in Windows Apps pointing at deleted uninstallers, crash logs piling on the Desktop.
ipcMain.handle('repair:run', async () => {
  try {
    const residue = collectResidue(false)
    const count = residue.dirs.length + residue.files.length + residue.regKeys.length

    if (count === 0) {
      return { ok: true, message: 'No residue - this machine is clean.' }
    }

    const listing = [
      ...residue.dirs.map((d) => `folder:  ${d}`),
      ...residue.files.map((f) => `file:  ${f}`),
      ...residue.regKeys.map((k) => `registry:  ${k}`)
    ]

    const { response } = await dialog.showMessageBox(mainWindow, {
      type: 'warning',
      title: 'Deep clean',
      message: `Found ${count} leftover(s) from old installs`,
      detail:
        listing.slice(0, 14).join('\n') +
        (listing.length > 14 ? `\n...and ${listing.length - 14} more` : '') +
        '\n\nNothing here is used by the copy you are running right now.',
      buttons: ['Clean everything', 'Keep'],
      defaultId: 1,
      cancelId: 1,
      noLink: true
    })

    if (response !== 0) return { ok: false, message: 'Left as found.' }

    const failed = purgeResidue(residue)
    return failed.length
      ? { ok: false, message: `Cleaned ${count - failed.length} of ${count} - locked: ${failed.join(', ')}` }
      : { ok: true, message: `Cleaned ${count} leftover(s). This machine takes a fresh install cleanly.` }
  } catch (err) {
    return { ok: false, message: err.message }
  }
})

// Re-runnable from Settings, because the first-run question is asked exactly once and
// "No thanks" is easy to click by reflex.
ipcMain.handle('shortcuts:create', async () => {
  try {
    createShortcut('desktop')
    createShortcut('startmenu')
    return { ok: true }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

ipcMain.handle('update:check', async () => {
  try {
    lastUpdateCheck = await checkForUpdates()
    return { ok: true, ...lastUpdateCheck }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

// ---------------------------------------------------------------------------
// Development updates
//
// The assistants working on the mod post what they changed to a small coordination API
// (code/coord-api), which publishes the newest entries as a release asset. Reading it
// from the same place as modlist.json and server.json means no new hosting and one
// place to look when something does not arrive.
//
// Missing is the normal case, not an error: the asset only exists once somebody has
// posted, and older releases will never have it. The panel simply stays hidden.
// ---------------------------------------------------------------------------

const DEV_UPDATES_URL = `https://github.com/${GITHUB_REPO}/releases/latest/download/assistant-updates.json`

ipcMain.handle('devUpdates:list', async () => {
  try {
    const response = await axios.get(DEV_UPDATES_URL, {
      timeout: 8000,
      // A 404 is an answer, not a failure - it means nothing has been posted yet.
      validateStatus: (status) => status === 200 || status === 404
    })

    if (response.status === 404) return { ok: true, updates: [] }

    const updates = Array.isArray(response.data?.updates) ? response.data.updates : []

    return {
      ok: true,
      generatedAt: response.data?.generatedAt || null,
      updates: updates.slice(0, 25)
    }
  } catch (err) {
    return { ok: false, error: err.message, updates: [] }
  }
})

// Public game-server status, for everyone - this is what drives the online/offline
// badge and the player count.
ipcMain.handle('game:serverStatus', async () => {
  lastServerStatus = await getGameServerStatus()
  return { ok: true, ...lastServerStatus, canBypass: isAdmin() }
})

// Everything the settings screen needs to describe the current state.
ipcMain.handle('paths:get', () => {
  const gameDir = findGameDir()
  const modDir = findModDir()

  // Can we actually WRITE to the game folder?
  //
  // Installing mods means writing into the game directory, which is often under
  // Program Files. Steam usually loosens permissions there, but not always - and a
  // locked-down folder fails mid-extract with a permission error that reads like a
  // corrupt download. Better to say "run as administrator" up front than to let
  // someone chase a phantom download problem.
  let gameWritable = false

  if (gameDir) {
    const probe = path.join(gameDir, '.ncÐ¾-write-test')
    try {
      writeFileSync(probe, '')
      rmSync(probe, { force: true })
      gameWritable = true
    } catch {
      gameWritable = false
    }
  }

  return {
    gameDir,
    gameWritable,
    modDir,
    hasDll: Boolean(modDir) && existsSync(path.join(modDir, 'CyberpunkMP.dll')),
    userData: app.getPath('userData'),
    appVersion: app.getVersion()
  }
})

ipcMain.handle('paths:open', (_event, which) => {
  const target = which === 'mod' ? findModDir() : findGameDir()
  if (!target) return { ok: false, error: 'That folder does not exist yet.' }
  shell.openPath(target)
  return { ok: true }
})

ipcMain.handle('install:everything', async () => {
  try {
    const result = await installEverything((step) => {
      // Progress goes to the renderer as it happens - a silent two-minute install
      // looks identical to a hang.
      if (mainWindow) mainWindow.webContents.send('install-progress', step)
    })
    lastUpdateCheck = await checkForUpdates()
    return { ok: true, ...result }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

ipcMain.handle('update:verify', async () => {
  try {
    return { ok: true, ...(await verifyInstall()) }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

ipcMain.handle('mod:uninstall', async () => {
  try {
    return { ok: true, ...(await uninstallMod()) }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

ipcMain.handle('launcher:reset', async () => {
  try {
    return { ok: true, ...(await resetLauncherData()) }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

ipcMain.handle('update:apply', async () => {
  try {
    const result = await applyUpdate()
    lastUpdateCheck = await checkForUpdates()
    return { ok: true, ...result }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

// Manual fallback when detection fails. Anyone can use this - it points at their own
// game folder, which is not a privileged thing to know.
ipcMain.handle('game:pickDir', async () => {
  const result = await dialog.showOpenDialog(mainWindow, {
    title: 'Where is Cyberpunk 2077 installed?',
    properties: ['openDirectory'],
    defaultPath: findGameDir() || 'C:\\'
  })

  if (result.canceled || !result.filePaths.length) return { ok: false }

  const chosen = result.filePaths[0]
  if (!existsSync(path.join(chosen, 'bin', 'x64', 'Cyberpunk2077.exe'))) {
    return { ok: false, error: 'No Cyberpunk2077.exe under that folder.' }
  }

  saveSettings({ gameDir: chosen })
  return { ok: true, gameDir: chosen }
})

ipcMain.handle('tailscale:status', async () => {
  return { ok: true, ...(await getTailscaleStatus()) }
})

// Every link between this PC and the game server, tested in order, with the first
// broken one named alongside its fix. Exists because every failure in this chain used
// to present identically as "Server offline" - a person on the wrong Tailscale
// network, a person with no Tailscale at all, and a genuinely down server all saw the
// same two words and none of them knew what to do next.
ipcMain.handle('connectivity:test', async () => {
  const steps = []
  let verdict = null

  // 1. Internet at all.
  try {
    await axios.head('https://github.com', { timeout: 6000 })
    steps.push({ name: 'Internet', ok: true, detail: 'online' })
  } catch {
    steps.push({ name: 'Internet', ok: false, detail: 'no connection' })
    verdict = 'No internet connection. Nothing past this can work until that is back.'
  }

  // 2 + 3. Tailscale installed, and connected.
  let ts = null
  if (!verdict) {
    ts = await getTailscaleStatus()
    if (!ts.installed) {
      steps.push({ name: 'Tailscale installed', ok: false, detail: 'not found' })
      verdict = 'Tailscale is not installed. Use the Tailscale link at the bottom of the launcher to get it, then come back.'
    } else {
      steps.push({ name: 'Tailscale installed', ok: true, detail: 'found' })
      if (!ts.connected) {
        steps.push({ name: 'Tailscale connected', ok: false, detail: 'signed out or stopped' })
        verdict = 'Tailscale is installed but not connected. Open Tailscale from the system tray and sign in.'
      } else {
        steps.push({ name: 'Tailscale connected', ok: true, detail: ts.ip || 'connected' })
      }
    }
  }

  // 4 + 5. The server, over that network - reachable, and actually running.
  if (!verdict) {
    const server = await resolveServer()
    const status = await getGameServerStatus()

    if (!status.online) {
      steps.push({ name: `Server reachable (${server.host})`, ok: false, detail: 'no route' })
      verdict = 'You are on a Tailscale network, but the server is not on it. Use "Join the ' +
                "server's network\" above, accept the invite, and make sure Tailscale is " +
                'switched to the network the invite joined (click the Tailscale tray icon to ' +
                'check which network you are on). If you already did all that, the server may ' +
                'genuinely be down - ask in the Discord.'
    } else {
      steps.push({ name: `Server reachable (${server.host})`, ok: true, detail: 'answers' })

      if (status.state === 'stopped') {
        steps.push({ name: 'Server running', ok: false, detail: 'stopped by an admin' })
        verdict = 'Everything on your side works. The server is deliberately stopped right now - an admin can start it from the launcher.'
      } else {
        steps.push({ name: 'Server running', ok: true, detail: `${status.players} player(s) online` })
        verdict = 'Everything works. Press Launch and play.'
      }
    }
  }

  return { ok: true, steps, verdict }
})

ipcMain.handle('tailscale:download', async () => {
  await shell.openExternal(TAILSCALE_DOWNLOAD)
  return { ok: true }
})

/**
 * Opens the invite to Cam's tailnet.
 *
 * Published in server.json rather than compiled in, so revoking or regenerating it is one
 * edit rather than a new launcher for everybody - which matters, because an invite link
 * that cannot be rotated is one you can never take back.
 *
 * The button is only shown to people the launcher has verified are in the Discord. That is
 * a courtesy, not a wall: server.json is a public release asset, so anyone determined to
 * find the link can. Treat this as "saves Cam sending it by hand", not as access control.
 */
ipcMain.handle('tailscale:invite', async () => {
  const published = await fetchPublishedServer()
  const invite = published?.tailscaleInvite

  if (!invite) return { ok: false, error: 'No invite link is published yet.' }

  await shell.openExternal(invite)
  return { ok: true }
})

// ---------------------------------------------------------------------------
// The dev key
//
// Cam's friends are helping write this, and each of them needs the coordination key in
// their own Claude. Relaying it by hand is the sort of chore that stops happening after
// the second person, so the launcher fetches it for anyone the role map says is a dev.
//
// The key never touches the renderer until it has been asked for, and the Discord token
// never leaves the main process except to Discord itself and to Cam's own coordination
// service.
// ---------------------------------------------------------------------------

ipcMain.handle('devKey:fetch', async () => {
  if (!isAdmin()) return { ok: false, error: 'The dev key is for people with the dev role.' }

  const token = loadToken()
  if (!token) return { ok: false, error: 'Sign in with Discord first.' }

  const published = await fetchPublishedServer()
  // coordHost first: the coordination API stays on Cam's machine even when the game
  // server moves, and a dev's server override must not drag this request with it.
  const host = published?.coordHost || loadSettings().serverHost || published?.host
  const port = published?.coordPort || 11780

  if (!host) return { ok: false, error: 'No server address is published yet.' }

  try {
    const response = await axios.post(
      `http://${host}:${port}/v1/dev-key`,
      { discordToken: token },
      { timeout: 8000, validateStatus: () => true })

    if (response.status !== 200) {
      return { ok: false, error: response.data?.error || `The coordination service answered ${response.status}.` }
    }

    return { ok: true, key: response.data.key, baseUrl: response.data.baseUrl, id: response.data.id }
  } catch (err) {
    // Almost always this: the service is only reachable over Tailscale, and only while
    // Cam's machine is on. Saying so beats a raw connection error.
    return {
      ok: false,
      error: 'Could not reach the coordination service. It needs Tailscale connected and Cam\'s PC on.'
    }
  }
})

// ---------------------------------------------------------------------------
// Dev tools: server selection and test builds
//
// The game server no longer lives only on Cam's PC - there is a self-updating test
// server too, and devs need to point their launcher at it without editing JSON by
// hand. resolveServer() has honoured settings.serverHost above everything else since
// the beginning; these are the controls for it. Status checks and the launch-anyway
// rule follow automatically, because both already go through resolveServer().
//
// Every handler re-checks the dev role in the main process - the renderer asking
// nicely is not authorisation.
// ---------------------------------------------------------------------------

ipcMain.handle('devServer:get', () => {
  const settings = loadSettings()
  return {
    ok: true,
    host: settings.serverHost || null,
    port: settings.serverPort || null,
    testBuildTag: settings.testBuildTag || null
  }
})

ipcMain.handle('devServer:set', (_event, host, port) => {
  if (!isAdmin()) return { ok: false, error: 'The server override is for people with the dev role.' }

  if (!host) {
    // undefined survives the merge spread but JSON.stringify drops it - which is
    // exactly "remove the key" without a separate delete path.
    saveSettings({ serverHost: undefined, serverPort: undefined })
    return { ok: true, cleared: true }
  }

  const cleanHost = String(host).trim()
  const cleanPort = Number(port) || 11778

  saveSettings({ serverHost: cleanHost, serverPort: cleanPort })
  return { ok: true, host: cleanHost, port: cleanPort }
})

// Pre-releases are how test builds travel: deliberately invisible to player launchers
// (auto-update reads releases/latest, which skips them), one click for a dev.
ipcMain.handle('prerelease:list', async () => {
  if (!isAdmin()) return { ok: false, error: 'Test builds are for people with the dev role.' }

  try {
    const response = await axios.get(
      `https://api.github.com/repos/${GITHUB_REPO}/releases?per_page=15`,
      { timeout: 8000 })

    const activeTag = loadSettings().testBuildTag || null

    const prereleases = (Array.isArray(response.data) ? response.data : [])
      .filter((r) => r.prerelease)
      .slice(0, 5)
      .map((r) => ({
        tag: r.tag_name,
        name: r.name || r.tag_name,
        publishedAt: r.published_at,
        hasDll: (r.assets || []).some((a) => a.name === 'CyberpunkMP.dll'),
        notesUrl: r.html_url,
        active: r.tag_name === activeTag
      }))

    // A test build can be retired from GitHub while still INSTALLED on this machine -
    // superseded builds get deleted so there is only ever one to pick. Without this
    // row, the installed build simply vanished from the list, taking its Uninstall
    // button with it: the game kept running the old test DLL with no visible way out.
    if (activeTag && !prereleases.some((r) => r.tag === activeTag)) {
      prereleases.unshift({
        tag: activeTag,
        name: `${activeTag} (retired - superseded by a newer test build)`,
        publishedAt: null,
        hasDll: false,
        notesUrl: null,
        active: true
      })
    }

    return { ok: true, prereleases, activeTag }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

ipcMain.handle('prerelease:install', async (_event, tag) => {
  if (!isAdmin()) return { ok: false, error: 'Test builds are for people with the dev role.' }

  try {
    const modDir = findModDir()
    if (!modDir) return { ok: false, error: 'Mod folder not found - install the mod first.' }

    const release = await axios.get(
      `https://api.github.com/repos/${GITHUB_REPO}/releases/tags/${encodeURIComponent(tag)}`,
      { timeout: 8000 })

    const assets = release.data?.assets || []

    // The WHOLE payload when the build has one, not just the DLL.
    //
    // Test builds used to swap CyberpunkMP.dll and nothing else, which works only while
    // the change under test is pure C++ - "remote players MOVE" was, so it was fine. It
    // silently delivers NOTHING when the change lives in redscript, and half this mod
    // does: menus, the seat transition, appearance re-application, the time and pause
    // hooks. The build installs, reports success, and the tester sees no difference.
    //
    // This is the same trap UpdateMod.ps1 already calls out by name: "Updating only the
    // DLL was a trap: fixes that live in redscript silently never reached anyone, so two
    // people could be on the same build and behave differently."
    //
    // ModPayload.zip is the DLL, the redscript and the Rpc definitions together - the
    // same archive a normal install applies - so the two halves cannot disagree.
    const payload = assets.find((a) => a.name === 'ModPayload.zip')
    const dllAsset = assets.find((a) => a.name === 'CyberpunkMP.dll')

    if (!payload && !dllAsset) {
      return { ok: false, error: 'That pre-release has no ModPayload.zip or CyberpunkMP.dll attached.' }
    }

    const asset = payload || dllAsset

    const download = await axios.get(asset.browser_download_url,
      { responseType: 'arraybuffer', timeout: 120000, maxRedirects: 5 })
    const bytes = Buffer.from(download.data)

    // GitHub publishes the digest with the asset; a truncated download must not land.
    const expected = String(asset.digest || '').replace(/^sha256:/, '').toLowerCase()
    if (expected) {
      const actual = crypto.createHash('sha256').update(bytes).digest('hex')
      if (actual !== expected) return { ok: false, error: 'Download failed its checksum - not installed.' }
    }

    const dllPath = path.join(modDir, 'CyberpunkMP.dll')
    const backupPath = path.join(modDir, 'CyberpunkMP.dll.shipped')

    // The FIRST shipped dll is the one kept. Installing test build B over test build A
    // must not make the backup a copy of test build A.
    //
    // Kept for the payload path too, purely as the offline fallback for Restore. Restore
    // prefers to re-download the current release, because that is the only DLL guaranteed
    // to agree with the scripts it ships alongside.
    if (existsSync(dllPath) && !existsSync(backupPath)) copyFileSync(dllPath, backupPath)

    if (payload) {
      // Same refusal as a normal install: an error page or a truncated archive must not
      // be unpacked over a working mod.
      if (bytes.length < 1024 * 1024) {
        return { ok: false, error: `That payload looks wrong (${bytes.length} bytes) - install left alone.` }
      }

      new AdmZip(bytes).extractAllTo(modDir, true)
    } else {
      writeFileSync(dllPath, bytes)
    }

    saveSettings({ testBuildTag: tag })

    return { ok: true, tag, payload: Boolean(payload) }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

ipcMain.handle('prerelease:restore', async () => {
  if (!isAdmin()) return { ok: false, error: 'Test builds are for people with the dev role.' }

  try {
    const modDir = findModDir()
    if (!modDir) return { ok: false, error: 'Mod folder not found.' }

    const dllPath = path.join(modDir, 'CyberpunkMP.dll')
    const backupPath = path.join(modDir, 'CyberpunkMP.dll.shipped')

    // Restore fetches the CURRENT release DLL rather than trusting the backup.
    //
    // The backup is first-write-wins, so after weeks of test builds it holds whatever
    // DLL was current when the FIRST one was installed - while the launcher has kept
    // updating the mod's SCRIPTS to the latest release the whole time. Scripts declare
    // native functions the DLL must define; restore an old DLL under new scripts and
    // RED4ext refuses to start the game at all ("invalid native definitions" - the
    // live failure of 2026-08-20, a test.5 DLL under v0.3.78 scripts). The latest
    // release's DLL and the latest release's scripts are the only pair guaranteed to
    // agree, so that is what restore means now. The backup remains the offline
    // fallback: possibly stale, but strictly better than a test build known-dead.
    // Prefer the release's WHOLE payload over its DLL alone.
    //
    // Test builds can now replace the scripts as well as the DLL, so putting back only
    // the DLL would leave a test build's redscript in place under a release DLL - the
    // exact mismatch the note above is about, just from the other direction. Re-extracting
    // ModPayload.zip restores both halves from the same release, which is the only pair
    // guaranteed to agree.
    try {
      const release = await axios.get(
        `https://api.github.com/repos/${GITHUB_REPO}/releases/latest`, { timeout: 8000 })

      const assets = release.data?.assets || []
      const payload = assets.find((a) => a.name === 'ModPayload.zip')
      const asset = payload || assets.find((a) => a.name === 'CyberpunkMP.dll')

      if (asset) {
        const download = await axios.get(asset.browser_download_url,
          { responseType: 'arraybuffer', timeout: 120000, maxRedirects: 5 })
        const bytes = Buffer.from(download.data)

        const expected = String(asset.digest || '').replace(/^sha256:/, '').toLowerCase()
        const intact = !expected ||
          crypto.createHash('sha256').update(bytes).digest('hex') === expected

        const plausible = !payload || bytes.length >= 1024 * 1024

        if (intact && plausible) {
          if (payload) new AdmZip(bytes).extractAllTo(modDir, true)
          else writeFileSync(dllPath, bytes)

          if (existsSync(backupPath)) unlinkSync(backupPath)
          saveSettings({ testBuildTag: undefined })
          return { ok: true, source: payload ? 'latest release payload' : 'latest release' }
        }
      }
    } catch { /* offline or API down - fall back to the local backup */ }

    if (!existsSync(backupPath))
      return { ok: false, error: 'Could not fetch the shipped mod and no local backup exists - use Reinstall mod files.' }

    copyFileSync(backupPath, dllPath)
    unlinkSync(backupPath)
    saveSettings({ testBuildTag: undefined })

    return { ok: true, source: 'local backup (offline) - Reinstall mod files if the game will not start' }
  } catch (err) {
    return { ok: false, error: err.message }
  }
})

// Opens a download page in the user's real browser. Nothing is fetched by the
// launcher here - these are first-time installs the player performs themselves.
ipcMain.handle('links:open', async (_event, which) => {
  const links = {
    release: 'https://github.com/ofmiceandcam98-eng/CyberpunkMP/releases/latest',
    // Was pinned to an old probe build, which went stale the moment releases became
    // versioned - it offered people a build from days earlier as if it were current.
    // The diagnostics now ship in every build, behind the developer overlay.
    diagnostic: 'https://github.com/ofmiceandcam98-eng/CyberpunkMP/releases/latest',
    discord: DISCORD_INVITE,
    tailscale: TAILSCALE_DOWNLOAD,
    // The upstream project this is a fork of. Their licence asks for clear
    // attribution, and a credit nobody can click through to is a weak one.
    upstream: 'https://github.com/tiltedphoques/CyberpunkMP',
    // Straight to the API key section, not the account page. "Go to settings and find
    // the API tab" is three more chances to end up somewhere else.
    nexusApi: 'https://www.nexusmods.com/users/myaccount?tab=api+access'
  }

  const url = links[which]
  if (!url) return { ok: false, error: 'Unknown link' }

  await shell.openExternal(url)
  return { ok: true }
})

ipcMain.handle('server:pickDir', async () => {
  if (!isAdmin()) return { ok: false, error: 'Not permitted' }

  const result = await dialog.showOpenDialog(mainWindow, {
    title: 'Where is Server.Loader.exe?',
    properties: ['openDirectory'],
    defaultPath: getServerDir()
  })

  if (result.canceled || !result.filePaths.length) return { ok: false }

  const chosen = result.filePaths[0]
  if (!existsSync(path.join(chosen, SERVER_EXE))) {
    return { ok: false, error: `No ${SERVER_EXE} in that folder.` }
  }

  saveSettings({ serverDir: chosen })
  return { ok: true, serverDir: chosen }
})

// Opening the invite goes through the real browser deliberately - it is a normal
// link, not a credential flow, and Discord handles invites better there.
ipcMain.handle('discord:openInvite', () => {
  shell.openExternal(DISCORD_INVITE)
  return { ok: true }
})

ipcMain.handle('game:launch', async () => {
  launcherLog('JACK IN pressed')
  try {
    const result = await launchGame()
    launcherLog(`launch pipeline finished: ${JSON.stringify({ ...result, executable: undefined })} exe=${result.executable || '?'}`)
    return { ok: true, ...result }
  } catch (err) {
    launcherLog(`launch REFUSED: ${err.message}`)
    // A refused launch is exactly when the trail matters - ship it now rather than
    // waiting for a game session that is not going to happen.
    shipClientLogs('launch refused')
    // Unlocked on every failure, without needing to know which check refused.
    //
    // launchGame sets `launching` early - the checks before the spawn include network
    // calls and can take seconds, and the button has to be dead for all of it. Every one
    // of those checks can throw, and any path that threw without clearing the flag would
    // leave the button disabled until the launcher was restarted. One reset here covers
    // all of them, including ones added later.
    //
    // `running` is deliberately not touched: "already running" is one of the errors, and
    // that state is true and still needs to be.
    if (!gameState.running) setGameState({ launching: false })

    return { ok: false, error: err.message }
  }
})

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Updating the launcher itself
//
// A running executable cannot overwrite itself, which is why the old check could only
// nag. electron-updater gets around that the only way Windows allows: download the new
// installer to a temp folder while the launcher runs, then hand over to it after the
// launcher has exited. The swap happens in the gap between the two.
//
// Everything here is deliberately quiet. The download is automatic and silent, and the
// only thing the player is ever asked is whether to restart now or later - because the
// alternative is a launcher that interrupts you to demand permission to do the thing
// you already wanted.
// ---------------------------------------------------------------------------

const { autoUpdater } = electronUpdater

let updateReady = false

function initAutoUpdater () {
  // In development there is no app-update.yml, and electron-updater throws rather than
  // shrugging. Nothing to update when running from source anyway.
  if (!app.isPackaged) return

  autoUpdater.autoDownload = true

  // Install on quit rather than forcing a restart. Someone mid-session should not have
  // their launcher yanked away because a release happened to land.
  autoUpdater.autoInstallOnAppQuit = true

  const send = (channel, payload) => {
    if (mainWindow && !mainWindow.isDestroyed()) mainWindow.webContents.send(channel, payload)
  }

  autoUpdater.on('update-available', (info) => {
    send('launcher-update', { state: 'downloading', version: info.version })
  })

  autoUpdater.on('download-progress', (progress) => {
    send('launcher-update', { state: 'downloading', percent: Math.round(progress.percent) })
  })

  autoUpdater.on('update-downloaded', (info) => {
    updateReady = true
    send('launcher-update', { state: 'ready', version: info.version })
  })

  autoUpdater.on('update-not-available', () => {
    send('launcher-update', { state: 'current' })
  })

  // Never let an update failure become a visible error. Being unable to reach GitHub is
  // not a reason to alarm someone who just wants to play - the launcher still works, it
  // is simply not newer.
  autoUpdater.on('error', (err) => {
    console.error('[updater]', err?.message || err)
    send('launcher-update', { state: 'current' })
  })

  autoUpdater.checkForUpdates().catch(() => {})
}

// Restart into the new version on demand. quitAndInstall closes the app and runs the
// downloaded installer; there is nothing after it.
ipcMain.handle('launcher:restartToUpdate', async () => {
  if (!updateReady) return { ok: false, error: 'No update has finished downloading yet.' }
  setImmediate(() => autoUpdater.quitAndInstall(false, true))
  return { ok: true }
})

/**
 * Puts a shortcut where the player asked for one.
 *
 * Note what is NOT here: pinning to the taskbar. Windows 10 and 11 deliberately removed
 * the ability for a program to pin itself - the old shell verb is blocked, and every
 * workaround that still circulates either fails silently or trips defender heuristics.
 * Claiming to do it and quietly not doing it would be worse than saying so, so the
 * dialog says plainly that the taskbar is a right-click away and does the two things
 * that genuinely work.
 */
function createShortcut (where) {
  const target = process.execPath
  const name = 'Night City Online Launcher.lnk'

  const folder = where === 'desktop'
    ? app.getPath('desktop')
    : path.join(app.getPath('appData'), 'Microsoft', 'Windows', 'Start Menu', 'Programs')

  const link = path.join(folder, name)

  return shell.writeShortcutLink(link, 'create', {
    target,
    // Without this the shortcut inherits whatever directory it was launched from,
    // which breaks relative paths the moment someone moves the .lnk.
    cwd: path.dirname(target),
    icon: target,
    iconIndex: 0,
    description: 'Night City Online - Cyberpunk 2077 multiplayer'
  })
}

/**
 * Asked once, on the first run after installing, and never again unless the answer is
 * lost. A launcher that asks about shortcuts every single time is a launcher people
 * learn to dismiss without reading.
 */
async function offerShortcuts () {
  if (loadSettings().shortcutsAsked) return

  // Record the answer BEFORE acting on it. If shortcut creation throws, the question
  // is still answered - otherwise a failing call would re-prompt on every launch.
  saveSettings({ shortcutsAsked: true })

  const { response } = await dialog.showMessageBox(mainWindow, {
    type: 'question',
    title: 'Night City Online Launcher',
    message: 'Add a shortcut?',
    detail: 'Windows does not let a program pin itself to the taskbar. To get it there, ' +
            'right-click the shortcut and choose "Pin to taskbar".',
    buttons: ['Desktop', 'Start Menu', 'Both', 'No thanks'],
    defaultId: 0,
    cancelId: 3,
    noLink: true
  })

  if (response === 3) return

  try {
    if (response === 0 || response === 2) createShortcut('desktop')
    if (response === 1 || response === 2) createShortcut('startmenu')
  } catch (err) {
    dialog.showMessageBox(mainWindow, {
      type: 'warning',
      title: 'Night City Online Launcher',
      message: 'Could not create the shortcut',
      detail: err.message
    })
  }
}

/**
 * Installs a mod from a downloaded archive.
 *
 * Extracts relative to the game folder, because that is how Cyberpunk mods are packaged -
 * their internal paths (archive\pc\mod\..., red4ext\plugins\...) already say where they
 * belong. Every extracted path is recorded so the mod can be removed later without
 * guessing which files were its.
 */
async function installModArchive (modId, buffer) {
  const gameDir = findGameDir()
  if (!gameDir) throw new Error('Cyberpunk 2077 not found. Set it in Settings.')

  if (await isProcessRunning('Cyberpunk2077.exe')) {
    throw new Error('Close Cyberpunk 2077 first - it holds mod files open.')
  }

  const mods = await fetchModList().catch(() => [])
  const mod = mods.find((m) => String(m.nexusModId) === String(modId))

  // What did Nexus actually send? Mods ship as .zip, .7z or .rar - and a rate-limit
  // or sign-in page arrives as HTML wearing a 200. Naming each case beats AdmZip's
  // "No END header found".
  const head = buffer.subarray(0, 8)
  const isZip = head[0] === 0x50 && head[1] === 0x4b
  const isRar = head[0] === 0x52 && head[1] === 0x61 && head[2] === 0x72 && head[3] === 0x21
  const is7z = head[0] === 0x37 && head[1] === 0x7a && head[2] === 0xbc && head[3] === 0xaf

  if (!isZip && !isRar && !is7z) {
    if (head[0] === 0x3c) {
      throw new Error('Nexus sent a webpage instead of the file - usually a sign-in or ' +
                      'rate-limit page. Wait a minute and try again, or install from the mod page.')
    }
    throw new Error('That download is not an archive this launcher can read.')
  }

  let entries
  let cleanup = null

  if (isZip) {
    const zip = new AdmZip(buffer)
    entries = zip.getEntries().filter((e) => !e.isDirectory)
  } else if (isRar) {
    // In-memory WASM extraction; the results feed the same install loop as zip
    // entries - one guard, one record, whatever the wrapper was.
    const data = buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength)
    const extractor = await createExtractorFromData({ data })
    const { files: rarFiles } = extractor.extract()
    entries = [...rarFiles]
      .filter((f) => !f.fileHeader.flags.directory && f.extraction)
      .map((f) => ({
        entryName: f.fileHeader.name,
        getData: () => Buffer.from(f.extraction)
      }))
  } else {
    // .7z goes through 7za into a scratch folder, then feeds the same install loop.
    const tmpRoot = path.join(os.tmpdir(), `nco-mod-${modId}-${Date.now()}`)
    const outDir = path.join(tmpRoot, 'out')
    mkdirSync(outDir, { recursive: true })
    const archivePath = path.join(tmpRoot, 'archive.bin')
    writeFileSync(archivePath, buffer)

    const run = spawnSync(SEVEN_ZIP, ['x', '-y', `-o${outDir}`, archivePath], { windowsHide: true })
    if (run.status !== 0) {
      rmSync(tmpRoot, { recursive: true, force: true })
      const detail = (run.stderr?.toString() || run.error?.message || '').trim().split(/\r?\n/).pop() || ''
      throw new Error(`Could not extract the archive (7-Zip exit ${run.status ?? '?'}${detail ? ': ' + detail : ''}).`)
    }

    const walk = (dir) => readdirSync(dir, { withFileTypes: true }).flatMap((d) => {
      const full = path.join(dir, d.name)
      return d.isDirectory() ? walk(full) : [full]
    })

    entries = walk(outDir).map((full) => ({
      entryName: path.relative(outDir, full),
      getData: () => readFileSync(full)
    }))
    cleanup = () => rmSync(tmpRoot, { recursive: true, force: true })
  }

  if (entries.length === 0) { if (cleanup) cleanup(); throw new Error('That archive is empty.') }

  const files = []
  for (const entry of entries) {
    // Normalise separators and refuse anything trying to climb out of the game folder.
    // A zip is untrusted input, and "../../windows/system32" is a real archive trick.
    const relative = entry.entryName.replace(/\\/g, '/')
    if (relative.includes('..')) continue

    const target = path.join(gameDir, relative)
    mkdirSync(path.dirname(target), { recursive: true })
    writeFileSync(target, entry.getData())
    files.push(relative)
  }

  const installed = loadInstalledMods()
  installed[String(modId)] = {
    name: mod?.name || `Mod ${modId}`,
    fileId: mod?.nexusFileId,
    version: mod?.version,
    files,
    at: Date.now()
  }
  saveInstalledMods(installed)

  if (cleanup) cleanup()

  return { name: installed[String(modId)].name, count: files.length }
}

/**
 * Receives nxm:// links from the browser.
 *
 * This is the ONLY way a launcher can install a Nexus mod for a free account. Their API
 * returns 403 for download links unless the account is Premium, so no amount of API work
 * gets around it - and trying would breach the API terms. Pressing "Mod Manager Download"
 * on the mod page hands the browser an nxm:// link carrying a short-lived key, Windows
 * routes it to whichever app registered the protocol, and that is us.
 *
 * Format: nxm://cyberpunk2077/mods/<modId>/files/<fileId>?key=...&expires=...
 */
async function handleNxmLink (url) {
  const match = /^nxm:\/\/([^/]+)\/mods\/(\d+)\/files\/(\d+)/i.exec(url)
  if (!match) return

  const [, game, modId, fileId] = match

  if (game.toLowerCase() !== 'cyberpunk2077') {
    dialog.showMessageBox(mainWindow, {
      type: 'warning',
      title: 'Wrong game',
      message: 'That download is not for Cyberpunk 2077.',
      detail: `The link was for "${game}".`
    })
    return
  }

  if (mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.webContents.send('mod-progress', { modId, state: 'downloading' })
  }

  try {
    // The nxm link is a handle, not a file. It must be exchanged for a real download URL
    // through the API, using the key and expiry it carries.
    const query = url.includes('?') ? url.slice(url.indexOf('?')) : ''
    const apiKey = loadNexusKey()

    if (!apiKey) throw new Error('Sign in to Nexus in Settings first.')

    const linkResponse = await axios.get(
      `https://api.nexusmods.com/v1/games/cyberpunk2077/mods/${modId}/files/${fileId}/download_link.json${query}`,
      { headers: { apikey: apiKey, 'User-Agent': 'NightCityOnline-Launcher/1.0' }, timeout: 20000 }
    )

    const downloadUrl = linkResponse.data?.[0]?.URI
    if (!downloadUrl) throw new Error('Nexus did not return a download link.')

    const file = await axios.get(downloadUrl, { responseType: 'arraybuffer', timeout: 300000 })
    const result = await installModArchive(modId, Buffer.from(file.data))

    if (mainWindow && !mainWindow.isDestroyed()) {
      mainWindow.webContents.send('mod-progress', { modId, state: 'installed', ...result })
    }
  } catch (err) {
    if (mainWindow && !mainWindow.isDestroyed()) {
      mainWindow.webContents.send('mod-progress', { modId, state: 'failed', error: err.message })
    }
  }
}

// Only one launcher may hold the nxm:// registration, and a second copy would sit there
// unable to receive anything. Windows delivers the link as an argument to the SECOND
// instance, so the first must be told about it and focused.
const gotLock = app.requestSingleInstanceLock()

if (!gotLock) {
  app.quit()
} else {
  app.on('second-instance', (_event, argv) => {
    const link = argv.find((arg) => arg.startsWith('nxm://'))
    if (link) handleNxmLink(link)

    if (mainWindow) {
      if (mainWindow.isMinimized()) mainWindow.restore()
      mainWindow.focus()
    }
  })
}

app.whenReady().then(() => {
  // Register as the nxm:// handler so "Mod Manager Download" reaches us.
  //
  // Re-registered every start rather than once: installing Vortex or Mod Organizer takes
  // the association away without warning, and the symptom is downloads silently opening
  // the wrong program.
  try {
    app.setAsDefaultProtocolClient('nxm')
  } catch (err) {
    console.error('[mods] could not register nxm:// handler:', err.message)
  }

  createWindow()

  // After the window exists, so the dialog has a parent to attach to rather than
  // appearing as a stray box with no launcher behind it.
  mainWindow.once('ready-to-show', () => {
    offerShortcuts()
    initAutoUpdater()

    // Bring the coordination feed up with the launcher, on the machine that hosts it.
    //
    // It was only ever started by server:start and server:restart, which was right while
    // Cam hosted the game server - the feed came up beside it. After the servers moved to
    // the NAS he stopped starting one, so neither handler fired again and the service that
    // "starts automatically" quietly stopped being started at all.
    //
    // Nothing broke; the trigger simply stopped happening. The feed was then down for two
    // days without anyone noticing, and posts made from the other side during that window
    // were not queued - they were never made. Seventeen releases are missing from the
    // history because of it.
    //
    // Tied to the launcher instead, because that is the thing Cam actually opens. Guarded
    // by hostsCoordApi so a checkout on someone else's machine does not start a second
    // one, and startCoordApiQuietly returns early if it is already up.
    if (hostsCoordApi()) startCoordApiQuietly()
  })

  // Catch-up sweep for logs nobody shipped live: a crash that took the launcher down
  // with it, or a session played while the launcher was closed. Delayed so startup
  // (sign-in restore, update check) is not competing with an upload.
  setTimeout(() => shipClientLogs('startup sweep'), 15000)

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit()
})
