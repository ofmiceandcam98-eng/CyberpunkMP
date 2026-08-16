/**
 * preload.js - the bridge between the page and Electron.
 *
 * You asked for three files; this is a necessary fourth. With contextIsolation
 * on (which it must be), the page cannot reach Node or Electron directly. This
 * script runs in a privileged context and hands the page a deliberately tiny
 * API instead.
 *
 * The alternative - nodeIntegration: true - would give every script in the page,
 * including anything pulled in by a dependency, the ability to spawn processes
 * and read the filesystem. This is a few lines to avoid that.
 *
 * Note what is NOT exposed: the access token. The page can ask who is signed in
 * and ask to launch the game. It cannot obtain the credential.
 */

import { contextBridge, ipcRenderer } from 'electron'

contextBridge.exposeInMainWorld('launcher', {
  // Start the Discord OAuth flow. Resolves to { ok, user } or { ok: false, error }.
  login: () => ipcRenderer.invoke('discord:login'),

  // Restore a saved session, if there is a still-valid one.
  restore: () => ipcRenderer.invoke('discord:restore'),

  // Forget the session in the main process.
  logout: () => ipcRenderer.invoke('discord:logout'),

  // Update checks. Play is refused unless the check says up to date.
  checkUpdate: () => ipcRenderer.invoke('update:check'),
  applyUpdate: () => ipcRenderer.invoke('update:apply'),

  // Deeper than checkUpdate: confirms the files are actually present and intact,
  // not just that the recorded version matches.
  verifyInstall: () => ipcRenderer.invoke('update:verify'),

  // What the people working on the mod have been changing, posted through the
  // coordination API and published with the release.
  devUpdates: () => ipcRenderer.invoke('devUpdates:list'),

  // The coordination key, for anyone the role map says is a dev. Fetched on demand
  // rather than pushed, so it is never sitting in the page for someone screen-sharing.
  devKey: () => ipcRenderer.invoke('devKey:fetch'),

  // Which body a new character starts from. Chosen here because it is baked into the
  // world template and cannot be changed once the game is running.
  getBodyType: () => ipcRenderer.invoke('bodyType:get'),
  setBodyType: (value) => ipcRenderer.invoke('bodyType:set', value),

  // The coordination service itself - started alongside the game server, controllable
  // on its own. Host machine only.
  coordStatus: () => ipcRenderer.invoke('coord:status'),
  startCoord: () => ipcRenderer.invoke('coord:start'),
  stopCoord: () => ipcRenderer.invoke('coord:stop'),

  // Opens the invite to Cam's tailnet in the real browser.
  openTailscaleInvite: () => ipcRenderer.invoke('tailscale:invite'),

  // The curated Nexus mod list, and the Nexus account used to fetch from it.
  // The API key is never handed to the page - only whether one is stored, and the name.
  listMods: () => ipcRenderer.invoke('mods:list'),
  verifyMods: () => ipcRenderer.invoke('mods:verify'),
  openMod: (id) => ipcRenderer.invoke('mods:open', id),
  deleteMod: (id) => ipcRenderer.invoke('mods:delete', id),
  onModProgress: (callback) => {
    ipcRenderer.on('mod-progress', (_e, info) => callback(info))
  },

  nexusSsoLogin: () => ipcRenderer.invoke('nexus:ssoLogin'),
  nexusSignIn: (key) => ipcRenderer.invoke('nexus:signIn', key),
  nexusStatus: () => ipcRenderer.invoke('nexus:status'),
  nexusSignOut: () => ipcRenderer.invoke('nexus:signOut'),

  // Desktop + Start Menu shortcuts. Asked once on first run; this is the way back.
  createShortcuts: () => ipcRenderer.invoke('shortcuts:create'),

  // Developer overlay. The main process decides who is allowed to change this.
  getDebugMode: () => ipcRenderer.invoke('debug:get'),
  setDebugMode: (enabled) => ipcRenderer.invoke('debug:set', enabled),

  // Where the launcher lives, and how to remove it. Both confirm in the main process.
  openInstallDir: () => ipcRenderer.invoke('launcher:openInstallDir'),
  uninstallLauncher: () => ipcRenderer.invoke('launcher:uninstall'),

  // The launcher updating itself: progress reports in, restart request out.
  onLauncherUpdate: (callback) => {
    ipcRenderer.on('launcher-update', (_e, info) => callback(info))
  },
  restartToUpdate: () => ipcRenderer.invoke('launcher:restartToUpdate'),

  // Paths and state for the settings screen.
  getPaths: () => ipcRenderer.invoke('paths:get'),
  openFolder: (which) => ipcRenderer.invoke('paths:open', which),

  // First-time install: prerequisites and the mod together.
  installEverything: () => ipcRenderer.invoke('install:everything'),
  onInstallProgress: (callback) => {
    ipcRenderer.on('install-progress', (_e, step) => callback(step))
  },

  // Removal. Both confirm with a dialog in the main process before deleting.
  uninstallMod: () => ipcRenderer.invoke('mod:uninstall'),
  resetLauncher: () => ipcRenderer.invoke('launcher:reset'),

  // Is the game server up, and how many people are on it.
  gameServerStatus: () => ipcRenderer.invoke('game:serverStatus'),

  // Manual fallback when the game cannot be found automatically.
  pickGameDir: () => ipcRenderer.invoke('game:pickDir'),
  pickModDir: () => ipcRenderer.invoke('mod:pickDir'),

  // Opens a known download page in the user's browser. The renderer passes a NAME,
  // not a URL - so a compromised page cannot use this to open anything it likes.
  openLink: (which) => ipcRenderer.invoke('links:open', which),

  // Tailscale presence and tunnel state - so "server offline" and "your VPN is
  // down" are never confused for each other.
  tailscaleStatus: () => ipcRenderer.invoke('tailscale:status'),
  openTailscaleDownload: () => ipcRenderer.invoke('tailscale:download'),

  // Admin: choose where the server lives.
  pickServerDir: () => ipcRenderer.invoke('server:pickDir'),

  // Open the Night City Online invite in the user's browser.
  openInvite: () => ipcRenderer.invoke('discord:openInvite'),

  // Start the game. The main process re-checks sign-in; this is not trusted.
  launchGame: () => ipcRenderer.invoke('game:launch'),

  // Server controls. Every one of these re-checks admin in the main process -
  // the renderer asking nicely is not authorisation.
  serverStatus: () => ipcRenderer.invoke('server:status'),
  startServer: () => ipcRenderer.invoke('server:start'),
  stopServer: () => ipcRenderer.invoke('server:stop'),
  restartServer: () => ipcRenderer.invoke('server:restart'),

  // Fired if the process fails to start after we handed off (bad path, missing exe).
  onLaunchError: (callback) => {
    ipcRenderer.on('launch-error', (_event, message) => callback(message))
  },

  // Fired when the game exits with a failure code, with the log already saved.
  onGameCrashed: (callback) => {
    ipcRenderer.on('game-crashed', (_event, info) => callback(info))
  }
})
