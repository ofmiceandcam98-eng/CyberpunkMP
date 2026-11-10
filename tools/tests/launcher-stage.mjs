// Runs the launcher's real index.html in an ordinary browser, with the Electron bridge stubbed,
// so a UI change can be seen and clicked in any state without building or installing anything.
//
//   node tools/tests/launcher-stage.mjs [port] [minutes]
//   http://127.0.0.1:8772/?scene=ready
//
// Scenes: ready, admin, update, frameworks, testbuild, launcherold, notready, signedout, worst. The
// launcher's own updater is driven from the console: stage.emit('onLauncherUpdate', { state: 'ready' }). Add &motion=full
// or &motion=reduced to force either set of motion rules, whatever this machine's animation setting
// is, and &result=fail to make Update refuse the way it does while the game is running. Size the window
// to 1180x800 to match the launcher (its minimum is 1024x740). It binds to loopback only and exits
// by itself after [minutes] (default 30), so it
// can't be left running. Everything it reports is staged: nothing here touches a game, a server
// or an install. Bridge calls without a stub answer {ok:false}, the same as a failed call.
import { createServer } from 'node:http'
import { readFile } from 'node:fs/promises'
import { dirname, extname, join, normalize, sep } from 'node:path'
import { fileURLToPath } from 'node:url'

const LAUNCHER = join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'code', 'launcher-lite')
const PORT = Number(process.argv[2]) || 8772
const MINUTES = Number(process.argv[3]) || 30
const TYPES = { '.ttf': 'font/ttf', '.webp': 'image/webp', '.png': 'image/png', '.html': 'text/html; charset=utf-8' }

// Runs in the page, before the launcher's own script. Passed through toString, so it can't close
// over anything from this file.
function stage (scene, result) {
  const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms))
  const listeners = {}
  const emit = (name, value) => (listeners[name] || []).forEach((callback) => callback(value))
  const pixel = 'data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw=='

  const user = { handle: 'zeldfep', username: 'zeldfep', playerId: 787389, avatarUrl: pixel, isMember: true, isAdmin: scene === 'admin' }
  const notes = 'Staged notes.\n\n## What changed\n\n- **A change.** Only here so the panel has something in it.'

  const mods = scene === 'worst'
    ? [
        { id: 1, name: 'Cyber Engine Tweaks', state: 'installed' },
        { id: 2, name: 'Appearance Menu Mod', state: 'installed', updateAvailable: true, reason: 'A different version is expected' },
        { id: 3, name: 'Virtual Car Dealer - Buy and Sell Vehicles Including Modded Ones', state: 'missing' },
        { id: 4, name: 'Immersive Rippers - Expanded Ripperdoc Dialogue', state: 'missing', blockedBy: ['Codeware'] },
        { id: 5, name: 'Better Vehicle Handling - Fully Customizable Driving Physics', state: 'downloading' },
        { id: 6, name: 'Kiroshi Opticals - Crowd Scanner', state: 'failed' },
        { id: 7, name: 'Native Settings UI', state: 'installed' },
        { id: 8, name: 'DLC start phone call off', state: 'installed' },
        { id: 9, name: 'Fast Launch (Skip Intro - Startup Optimizer)', state: 'installed' },
        { id: 10, name: 'Limited HUD', state: 'installed' },
        { id: 11, name: 'Ripperdoc Slot Unlocker', state: 'missing' },
        { id: 12, name: 'Night City Interactions', state: 'installed' }
      ]
    : [
        { id: 1, name: 'Fast Launch (Skip Intro - Startup Optimizer)', state: 'installed' },
        { id: 2, name: 'Native Settings UI', state: 'missing' },
        { id: 3, name: 'DLC start phone call off', state: 'missing' }
      ]
  const personal = scene === 'worst'
    ? [
        { id: 20, name: 'Kabuki Market Clutter Pack (2.5 port)', files: 12, pulled: true, pulledReason: 'Removed from the server list' },
        { id: 21, name: 'HUD', files: 1, state: 'broken' }
      ]
    : []

  // Update scenes: the mod is behind, the frameworks are behind, or a newer test build is out. A
  // successful Update brings it current, so the next check says so, like the real one.
  let updated = false
  const steps = scene === 'frameworks'
    ? ['Checking the frameworks the mod runs on...', 'Checking RED4ext...', 'Installing RED4ext...', 'Checking ArchiveXL...', 'Installing ArchiveXL...', 'Downloading the update...', 'Installing the update...']
    : ['Checking the frameworks the mod runs on...', 'Downloading the update...', 'Installing the update...']

  const fixed = {
    getPaths: { appVersion: '0.0.0-stage' },
    restore: scene === 'signedout' ? { ok: false } : { ok: true, user: scene === 'worst' ? { ...user, isMember: false, username: 'aleksandra.wisniewska.kowalczyk' } : user },
    checkUpdate: () => {
      const current = { ok: true, installed: true, upToDate: true, version: 'v0.3.127', notes }
      if (updated) return scene === 'testbuild' ? { ...current, testBuildTag: 'v0.3.127-test.94' } : current
      if (scene === 'update') return { ...current, upToDate: false }
      if (scene === 'frameworks') return { ...current, upToDate: false, payloadCurrent: true, prerequisitesStale: ['RED4ext', 'ArchiveXL'] }
      if (scene === 'testbuild') return { ...current, testBuildTag: 'v0.3.127-test.93' }
      if (scene === 'launcherold') return { ...current, launcher: { current: '0.3.127', available: '0.3.128', upToDate: false } }
      return current
    },
    prereleaseList: () => ({ ok: true, newer: updated ? null : 'v0.3.127-test.94' }),
    applyUpdate: async () => {
      if (result === 'fail') return { ok: false, error: 'Close Cyberpunk 2077 first - the game is holding the mod files open.' }
      for (const step of steps) {
        emit('onInstallProgress', step)
        await wait(step.startsWith('Downloading') ? 1200 : 450)
      }
      updated = true
      return { ok: true, version: 'v0.3.127' }
    },
    prereleaseInstall: async (tag) => {
      if (result === 'fail') return { ok: false, error: 'Close Cyberpunk 2077 first - the game is holding the mod files open.' }
      await wait(1600)
      updated = true
      return { ok: true, tag }
    },
    manifestStatus: { ok: true, state: 'verified', version: '2026.11.01.01' },
    gameServerStatus: scene === 'notready' ? { ok: true, online: false } : { ok: true, online: true, players: 2 },
    tailscaleStatus: { ok: true, installed: true, connected: true, ip: '100.64.0.12' },
    listMods: () => ({ ok: true, mods, personal, signedIn: true }),
    serverStatus: { ok: true, mode: 'remote', running: true, state: 'running', players: 2, host: '[host]', port: 11778, hasAdminCred: true },
    devUpdates: { updates: [] },
    nexusStatus: { ok: true, signedIn: true, name: 'zeldfep' },
    launchGame: async () => {
      emit('onGameState', { launching: true, running: false })
      setTimeout(() => emit('onGameState', { launching: false, running: true }), 2200)
      setTimeout(() => emit('onGameState', { launching: false, running: false }), 3600)
      return { ok: true }
    },
    verifyInstall: async () => {
      await wait(1400)
      return { ok: true, installed: true, version: 'v0.3.127', redscriptCount: 46 }
    },
    verifyMods: async () => {
      await wait(1400)
      return { ok: true, checked: 1, intact: 1, problems: [], retired: [], manifest: { version: '2026.11.01.01' } }
    },
    addModFromNexus: async () => {
      await wait(1200)
      return { ok: true, message: 'Staged: nothing was added.' }
    },
    openMod: { ok: true },
    restartToUpdate: { ok: true },
    deleteMod: { ok: false, error: 'Staged: nothing was removed.' }
  }

  window.launcher = new Proxy({}, {
    get (_, name) {
      if (typeof name === 'string' && name.startsWith('on')) {
        return (callback) => {
          (listeners[name] = listeners[name] || []).push(callback)
          return () => {}
        }
      }
      return async (...args) => {
        const answer = fixed[name]
        if (typeof answer === 'function') return answer(...args)
        return answer !== undefined ? answer : { ok: false }
      }
    }
  })
  window.stage = { scene, emit }
}

async function launcherPage (scene, motion, result) {
  let html = await readFile(join(LAUNCHER, 'index.html'), 'utf8')
  // A machine's animation setting decides which rules the launcher shows, so one machine could only
  // ever test one of them. &motion=full and &motion=reduced force each, here and nowhere else.
  const forced = { full: '@media not all', reduced: '@media all' }[motion]
  if (forced) html = html.replaceAll('@media (prefers-reduced-motion: reduce)', forced)
  // Either line ending: git checks this file out as CRLF on Windows.
  const anchor = /\r?\n<script>\r?\n/g
  if ((html.match(anchor) || []).length !== 1) {
    throw new Error('launcher-stage: expected exactly one inline <script> in code/launcher-lite/index.html - update launcherPage() to match')
  }
  return html.replace(anchor, (found) => `\n<script>(${stage})(${JSON.stringify(scene)}, ${JSON.stringify(result)})</script>${found}`)
}

const server = createServer(async (request, response) => {
  const url = new URL(request.url, 'http://127.0.0.1')
  try {
    if (url.pathname === '/') {
      const page = await launcherPage(url.searchParams.get('scene') || 'ready', url.searchParams.get('motion'), url.searchParams.get('result'))
      response.writeHead(200, { 'Content-Type': TYPES['.html'] })
      response.end(page)
      return
    }
    const file = normalize(join(LAUNCHER, decodeURIComponent(url.pathname)))
    if (!file.startsWith(LAUNCHER + sep)) throw new Error('outside the launcher folder')
    const body = await readFile(file)
    response.writeHead(200, { 'Content-Type': TYPES[extname(file)] || 'application/octet-stream' })
    response.end(body)
  } catch (error) {
    response.writeHead(error.code === 'ENOENT' ? 404 : 500, { 'Content-Type': 'text/plain' })
    response.end(String(error.message))
  }
})

server.listen(PORT, '127.0.0.1', () => {
  console.log(`launcher stage on http://127.0.0.1:${PORT}/?scene=ready - exits in ${MINUTES} minutes`)
})
setTimeout(() => process.exit(0), MINUTES * 60000)
