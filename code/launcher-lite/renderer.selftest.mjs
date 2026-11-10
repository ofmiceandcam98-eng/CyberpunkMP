/**
 * renderer.selftest.mjs - runs the REAL renderer functions out of index.html, without Electron.
 * Run with:  node renderer.selftest.mjs
 *
 * index.html's inline script is the largest stretch of launcher code that no gate reads: it cannot
 * be imported, and `node --check` does not open html. So this lifts named functions out of it as
 * text, the way prereq.selftest.mjs does for main.js, and runs them against a small stand-in for
 * the DOM with everything else named in the call. An undeclared identifier in a lifted function is
 * a ReferenceError here, instead of one on a player's machine. The stand-in refuses any element id
 * index.html does not carry, because the real $() returns null for those and the next line throws.
 *
 * Reaches no network and writes nothing.
 */

import { existsSync, readFileSync } from 'node:fs'
import path from 'node:path'
import vm from 'node:vm'

const text = (relative) => readFileSync(new URL(relative, import.meta.url), 'utf8').split('\r\n').join('\n')

const html = text('./index.html')
const NOTES_FILE = new URL('../../publish/release-notes.md', import.meta.url)

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

// One function of the inline script, as text. They all sit two spaces in and close with a brace
// at that same depth.
function grab (name) {
  const start = html.search(new RegExp(`^  (async )?function ${name} `, 'm'))
  if (start < 0) throw new Error(`index.html has no function ${name} - update this selftest`)
  const end = html.indexOf('\n  }\n', start)
  return html.slice(start, end + 4)
}

class Element {
  constructor (tag, id = null) {
    this.tag = tag
    this.id = id
    this.children = []
    this.ownText = ''
    this.className = ''
    this.title = ''
    this.disabled = false
    this.style = {}
    this.listeners = {}
    this.htmlWrites = []
    this.dataset = {}
    this.attributes = {}
    this.found = new Map()
    this.moves = []
    const classes = new Set()
    this.classList = {
      add: (c) => classes.add(c),
      remove: (c) => classes.delete(c),
      contains: (c) => classes.has(c),
      toggle: (c, on) => ((on ?? !classes.has(c)) ? classes.add(c) : classes.delete(c), classes.has(c))
    }
  }

  setAttribute (name, value) {
    this.attributes[name] = String(value)
  }

  // The button kit keeps its text on an inner .label. A selector answers with the same child every
  // time, so what setHudButton writes is in the button's textContent, as it is on the page.
  querySelector (selector) {
    if (!this.found.has(selector)) this.found.set(selector, this.appendChild(new Element('span')))
    return this.found.get(selector)
  }

  // Moving an element is recorded on the element that moved: [where, next to which id].
  before (element) {
    element.moves.push(['before', this.id])
  }

  after (element) {
    element.moves.push(['after', this.id])
  }

  get textContent () {
    return this.children.length > 0 ? this.children.map((c) => c.textContent).join('') : this.ownText
  }

  set textContent (value) {
    this.children = []
    this.ownText = String(value)
  }

  set innerHTML (value) {
    this.children = []
    this.found.clear()
    this.ownText = ''
    if (value !== '') this.htmlWrites.push(value)
  }

  appendChild (child) {
    this.children.push(child)
    return child
  }

  addEventListener (name, callback) {
    this.listeners[name] = callback
  }
}

// The ids and the hidden-by-default state both come from index.html's own markup, so a test
// starts from what the page really starts from.
function fakeDocument () {
  const elements = new Map()
  return {
    createElement: (tag) => new Element(tag),
    getElementById: (id) => {
      if (elements.has(id)) return elements.get(id)
      const tag = new RegExp(`<[a-z][a-z0-9]*\\b[^>]*\\bid="${id}"[^>]*>`).exec(html)
      if (!tag) throw new Error(`index.html has no element #${id}`)
      const element = new Element(tag[0].slice(1, tag[0].search(/\s/)), id)
      if (/\bstyle="[^"]*display:\s*none/.test(tag[0])) element.style.display = 'none'
      elements.set(id, element)
      return element
    }
  }
}

// Lifts `functions` into one scope. `state` becomes the file's `let` variables, which those
// functions read and assign by name; `globals` takes the document and returns anything else they
// need. Whatever is not named here is undeclared, and running it throws.
function page ({ functions, state = {}, globals = () => ({}) }) {
  const document = fakeDocument()
  const deps = { document, $: (id) => document.getElementById(id), ...globals(document) }
  const lets = Object.keys(state).map((name) => `let ${name} = __state.${name}`).join('\n')
  const body = functions.map(grab).join('\n\n')
  // eslint-disable-next-line no-new-func
  const fns = new Function('__state', ...Object.keys(deps),
    `'use strict'\n${lets}\n${body}\nreturn { ${functions.join(', ')} }`)(state, ...Object.values(deps))
  return { ...fns, $: deps.$ }
}

check('the inline script parses', (() => {
  const script = /<script>\n([\s\S]*?)<\/script>/.exec(html)
  if (!script) return 'index.html has no inline <script> - update this selftest'
  try {
    // eslint-disable-next-line no-new
    new vm.Script(`(async () => {\n${script[1]}\n})`)
    return 'parses'
  } catch (err) {
    return err.message
  }
})(), 'parses')

// --- patch notes -------------------------------------------------------------------------------

const NOTES_FUNCTIONS = ['renderNotes', 'parseNotes', 'stripMarkdown']

// What the panel ends up holding: h4 and p as text, ul as its items.
function renderedNotes (markdown) {
  const notes = page({ functions: NOTES_FUNCTIONS })
  notes.renderNotes(markdown)
  const body = notes.$('changelogBody')
  check('the notes never reach innerHTML', body.htmlWrites, [])
  return body.children.map((c) => c.tag === 'ul'
    ? { ul: c.children.map((li) => li.textContent) }
    : { [c.tag]: c.textContent })
}

check('no notes says so', renderedNotes('  \n '), [{ p: 'No notes for this build.' }])

const WRAPPED = [
  '## What changed — v9.9.9',
  '',
  '**Night City Online runs on the new patch.** This is the first release built for it:',
  'connect, pick your character, spawn, drive. It does NOT run on the old one - the',
  'launcher will tell you.',
  '',
  '- **CONNECT works again.** On the main menu the game has a world loaded',
  '  behind it, which made the mod think you were already in the city.',
  '- **Trading: the eddies offer moves by 1 per press** instead of 500.',
  '- Looks like a broken install and it is not: **it is the game update, there is',
  '  nothing to reinstall.** The launcher checks first.',
  '',
  '**Still broken - known:**',
  '',
  '- **Inventory is not saved yet.** Nothing you pick up',
  '  is kept between sessions.',
  '## Older',
  'A paragraph right under a heading',
  'that wraps.'
]
const WRAPPED_RENDERED = [
  { h4: 'What changed — v9.9.9' },
  { p: 'Night City Online runs on the new patch. This is the first release built for it: connect, pick your character, spawn, drive. It does NOT run on the old one - the launcher will tell you.' },
  {
    ul: [
      'CONNECT works again. On the main menu the game has a world loaded behind it, which made the mod think you were already in the city.',
      'Trading: the eddies offer moves by 1 per press instead of 500.',
      'Looks like a broken install and it is not: it is the game update, there is nothing to reinstall. The launcher checks first.'
    ]
  },
  { p: 'Still broken - known:' },
  { ul: ['Inventory is not saved yet. Nothing you pick up is kept between sessions.'] },
  { h4: 'Older' },
  { p: 'A paragraph right under a heading that wraps.' }
]

check('hard-wrapped paragraphs and list items come back whole', renderedNotes(WRAPPED.join('\n')), WRAPPED_RENDERED)
check('the same with the CRLF a Windows checkout has', renderedNotes(WRAPPED.join('\r\n')), WRAPPED_RENDERED)

check('a link that crosses a wrap still loses its markup',
  renderedNotes('Read [the full\n  notes](https://example.com/a) first.'), [{ p: 'Read the full notes first.' }])
check('a blank line between items stays one list',
  renderedNotes('- one\n  more\n\n- two'), [{ ul: ['one more', 'two'] }])
check('two paragraphs stay two', renderedNotes('one\n\ntwo'), [{ p: 'one' }, { p: 'two' }])
check('a paragraph after a list ends the list',
  renderedNotes('- item\n\nafter\n\n- next'), [{ ul: ['item'] }, { p: 'after' }, { ul: ['next'] }])
check('a list item directly under a paragraph is not folded into it',
  renderedNotes('intro\n- item'), [{ p: 'intro' }, { ul: ['item'] }])
check('a second paragraph inside an item stays its own paragraph',
  renderedNotes('- item\n\n  **Please test.** More\n  text.'), [{ ul: ['item'] }, { p: 'Please test. More text.' }])
check('numbered steps are not folded into one line, and a wrapped step is',
  renderedNotes('1. first\n2. second\n   wrapped\n3. third'), [{ p: '1. first' }, { p: '2. second wrapped' }, { p: '3. third' }])
check('a quote starts its own block',
  renderedNotes('before\n> **Quoted.** Text'), [{ p: 'before' }, { p: '> Quoted. Text' }])
check('a code fence keeps its lines apart',
  renderedNotes('4. Add options:\n```\n--online\n--port=1\n```\nafter'),
  [{ p: '4. Add options:' }, { p: '```' }, { p: '--online' }, { p: '--port=1' }, { p: '```' }, { p: 'after' }])
check('a table row or image closes the block rather than vanishing into it',
  renderedNotes('above\n| a | b |\nbelow\n<img src="x">\nlast'), [{ p: 'above' }, { p: 'below' }, { p: 'last' }])
check('markup in the notes stays text',
  renderedNotes('Fixed <img src=x onerror=alert(1)> here\n  and more.'), [{ p: 'Fixed <img src=x onerror=alert(1)> here and more.' }])

// The real file. Nothing here names a version or a sentence, because the notes change with every
// release and this runs on every ship: it asks whether the renderer kept what the source says.
if (!existsSync(NOTES_FILE)) {
  check(`publish/release-notes.md exists at ${NOTES_FILE.pathname} - the real-notes checks need it`, false, true)
} else {
  const source = readFileSync(NOTES_FILE, 'utf8')
  const sourceLines = source.split(/\r?\n/).map((l) => l.trim())
  const rendered = renderedNotes(source)

  const words = (s) => s.replace(/\[([^\]]+)\]\([^)]+\)/g, '$1').replace(/[*`]/g, '').split(/\s+/).filter(Boolean)
  const flat = rendered.flatMap((b) => b.ul || Object.values(b))
  const isLayout = (l) => !l || l.startsWith('|') || l.startsWith('<')

  check('real notes: every word in the source reaches the panel, in order',
    words(flat.join(' ')).join(' '),
    words(sourceLines.filter((l) => !isLayout(l)).map((l) => l.replace(/^(#+\s*|[-*] )/, '')).join(' ')).join(' '))

  // Items worked out here from the source, by markdown's rule: from the marker to the next blank
  // line, marker or heading.
  const items = []
  let current = null
  for (const line of sourceLines) {
    if (/^[-*] /.test(line)) {
      current = [line.slice(2)]
      items.push(current)
    } else if (isLayout(line) || line.startsWith('#')) {
      current = null
    } else if (current) {
      current.push(line)
    }
  }
  const itemWords = (list) => list.map((item) => words(item).join(' '))
  const lis = rendered.flatMap((b) => b.ul || [])

  check('real notes: each list item arrives whole, wrapped lines and all',
    itemWords(lis), itemWords(items.map((lines) => lines.join(' '))))
  check('real notes: the check above is not vacuous - the file does wrap its list items',
    items.some((lines) => lines.length > 1), true)

  // The symptom itself: a wrapped line of an item or paragraph standing alone in the panel.
  let inFence = false
  const wrappedLines = sourceLines.filter((l, i) => {
    if (l.startsWith('```')) inFence = !inFence
    const before = sourceLines[i - 1] ?? ''
    return !inFence && !isLayout(l) && !isLayout(before) && !before.startsWith('#') &&
      !before.startsWith('```') && !/^([-*] |#|\d+[.)]\s|>|```)/.test(l)
  })
  const { stripMarkdown } = page({ functions: NOTES_FUNCTIONS })
  const blocks = new Set(flat)
  check('real notes: no wrapped line stands alone as its own paragraph or item',
    wrappedLines.filter((l) => blocks.has(stripMarkdown(l))), [])
  check('real notes: the check above is not vacuous - the file does wrap its lines',
    wrappedLines.length > 0, true)
}

// --- JACK IN reason and the network invite -----------------------------------------------------

const MEMBER = { handle: 'v', username: 'v', playerId: 1, avatarUrl: 'x', isMember: true, isAdmin: false }
const STRANGER = { ...MEMBER, isMember: false }
const ADMIN = { ...MEMBER, isAdmin: true }

const INVITE_BUTTON = "Join the server's network"

// The real render and renderTailnetInvite together, on a machine that is on Tailscale and cannot
// reach the server: the state where the invite is the answer.
function launcher (over = {}) {
  return page({
    functions: ['render', 'renderTailnetInvite', 'setHudButton', 'armHudButton', 'playOnce', 'launcherIsOld'],
    state: {
      signedInUser: null,
      gameState: { launching: false, running: false },
      updateState: { installed: true, upToDate: true },
      serverState: { online: false },
      tailscaleState: { installed: true, connected: true, ip: '100.64.0.1' },
      launcherUpdate: null,
      ...over
    },
    globals: (document) => ({
      playBtn: document.getElementById('play'),
      loginBtn: document.getElementById('login'),
      refreshNexus: () => {},
      showAdminPanel: () => {}
    })
  })
}

const inviteShown = (l) => l.$('joinTailnet').style.display !== 'none'
const reason = (l) => l.$('playReason').textContent

function rendered (user, over) {
  const l = launcher(over)
  l.render(user)
  return l
}

check('the reason quotes the label the invite button really has',
  html.includes(`id="joinTailnet" style="display:none">${INVITE_BUTTON}</button>`), true)

for (const [who, user] of [['signed out', null], ['not a member', STRANGER], ['member', MEMBER], ['admin', ADMIN]]) {
  const l = rendered(user)
  check(`${who}: the reason only names the invite button when it is on screen`,
    !reason(l).includes(INVITE_BUTTON) || inviteShown(l), true)
}

const NETWORK_OPENS = "The server's network opens to you once you have joined the Discord"
check('not a member: told what comes first',
  reason(rendered(STRANGER)), `Join the Night City Online Discord — press the button above  ·  ${NETWORK_OPENS}`)
check('signed out: told what comes first',
  reason(rendered(null)), `Sign in with Discord  ·  ${NETWORK_OPENS}`)
check('member: still sent to the button, which is there',
  [reason(rendered(MEMBER)).includes(INVITE_BUTTON), inviteShown(rendered(MEMBER))], [true, true])

// Every way out of an account (Log out, Sign out and reset, a refused login) lands on render(null).
{
  const l = launcher()
  const toolRow = () => [l.$('openTailscaleInvite').disabled, l.$('openTailscaleInvite').textContent]

  l.render(MEMBER)
  check('a member who cannot reach the server is offered the invite',
    [inviteShown(l), toolRow()], [true, [false, 'Open invite']])

  l.render(null)
  check('logging out takes the invite off the screen', inviteShown(l), false)
  check('and the Tools row stops offering it', toolRow(), [true, 'Members only'])

  l.render(MEMBER)
  check('signing back in offers it again', [inviteShown(l), toolRow()], [true, [false, 'Open invite']])
}

check('a first render, signed out, leaves the invite hidden and the Tools row honest',
  (() => {
    const l = rendered(null)
    return [inviteShown(l), l.$('openTailscaleInvite').disabled]
  })(), [false, true])
check('a member already on the network is not offered it',
  inviteShown(rendered(MEMBER, { serverState: { online: true, players: 1 } })), false)

// --- mod list ----------------------------------------------------------------------------------

const mainSource = text('./main.js')

// The real mods:list handler out of main.js, so what the panel is handed below is what the main
// process really answers, not what a test assumes it might.
function modsListHandler ({ published, installed = {}, nexusKey = 'a-key' }) {
  const start = mainSource.indexOf("ipcMain.handle('mods:list', async () => {")
  if (start < 0) throw new Error('main.js has no mods:list handler - update this selftest')
  const end = mainSource.indexOf('\n})\n', start)
  let handler = null
  const deps = {
    ipcMain: { handle: (_name, fn) => { handler = fn } },
    path,
    existsSync: () => true,
    findGameDir: () => 'game',
    fetchModList: async () => published,
    sortByDependencies: (mods) => mods,
    refreshUserLevel: async () => 'player',
    LEVELS: { player: 0, moderator: 1, admin: 2, owner: 3 },
    isAdmin: () => false,
    loadInstalledMods: () => installed,
    describeInstalledMod: (mod, records) => ({ state: records[String(mod.nexusModId)] ? 'installed' : 'missing' }),
    fetchModInfo: async () => null,
    recordPaths: (record) => record.files || [],
    pulledMods: new Map(),
    describeUpdate: async () => null,
    loadNexusKey: () => nexusKey
  }
  // eslint-disable-next-line no-new-func
  new Function(...Object.keys(deps), `'use strict'\n${mainSource.slice(start, end + 3)}`)(...Object.values(deps))
  return handler
}

const OWN_MOD = { 555: { name: 'My Own Mod', version: '1.0', files: ['archive/pc/mod/own.archive'] } }
const LISTED = { nexusModId: 111, name: 'Listed Mod' }

const emptyList = await modsListHandler({ published: [], installed: OWN_MOD })()
check('main process: an empty published list with a player\'s own install beside it is a real answer',
  [emptyList.ok, emptyList.mods, emptyList.personal.map((m) => m.name)], [true, [], ['My Own Mod']])

const devOnlyList = await modsListHandler({ published: [{ ...LISTED, devOnly: true }], installed: OWN_MOD })()
check('main process: so is a list of only dev mods, read by a player',
  [devOnlyList.mods, devOnlyList.personal.map((m) => m.name)], [[], ['My Own Mod']])

const signedOutOfNexus = await modsListHandler({ published: [LISTED], installed: OWN_MOD, nexusKey: null })()
check('main process: being signed out of Nexus is NOT a way there - it does not empty the list',
  [signedOutOfNexus.signedIn, signedOutOfNexus.mods.map((m) => m.name)], [false, ['Listed Mod']])

const bothLists = await modsListHandler({ published: [LISTED], installed: OWN_MOD })()
const nothing = await modsListHandler({ published: [] })()

function modPanel (answer) {
  const deleted = []
  const panel = page({
    functions: ['renderMods', 'renderPersonalMods', 'makeUpdateButton', 'refreshMods', 'makeWiredMini', 'makeRemoveButton', 'setHudButton'],
    state: { modState: [], personalState: [], modsSignedIn: null },
    globals: () => ({
      // The kit's markup templates. The stand-in records innerHTML rather than parsing it.
      WIRED_MINI: '<wired mini>',
      BIN_ICON: '<bin icon>',
      setStatus: () => {},
      window: {
        launcher: {
          listMods: async () => answer,
          deleteMod: async (id) => {
            deleted.push(id)
            return { ok: true }
          }
        }
      }
    })
  })
  return { ...panel, deleted, rows: () => panel.$('modList').children }
}

const YOUR_MODS = '/// YOUR MODS — not on the server’s list ///'
const NO_LIST_HINT = 'The server has no recommended mods yet. When it adds some, they appear here.'
// The button kit's mods panel: the badge reads INSTALLED, and the remove button is a drawn bin with
// no text of its own (the 🗑 emoji rendered as a 6px smudge). The tests below find it by its title.
const ownRow = 'My Own ModINSTALLED'

{
  const panel = modPanel(emptyList)
  await panel.refreshMods()
  check('an empty recommended list does not hide the player\'s own mods',
    panel.rows().map((r) => r.textContent), [YOUR_MODS, ownRow])
  check('and the hint still says why the list is empty',
    [panel.$('modHint').textContent, panel.$('modCount').textContent], [NO_LIST_HINT, ''])

  const remove = panel.rows()[1]?.children.find((c) => c.title === 'Remove this mod')
  await remove?.listeners.click()
  check('and the mod can be removed from there', panel.deleted, ['555'])
}

{
  const panel = modPanel(devOnlyList)
  await panel.refreshMods()
  check('a list of only dev mods does not hide them either',
    panel.rows().map((r) => r.textContent), [YOUR_MODS, ownRow])
}

{
  const panel = modPanel(bothLists)
  await panel.refreshMods()
  check('with both lists, the recommended ones still come first',
    panel.rows().map((r) => r.textContent), ['Listed ModRECOMMENDEDInstall', YOUR_MODS, ownRow])
  check('and the count still counts the recommended ones',
    panel.$('modCount').textContent, '1 mods · 0 installed')
}

{
  const panel = modPanel(nothing)
  await panel.refreshMods()
  check('nothing listed and nothing installed draws nothing', [panel.rows(), panel.$('modHint').textContent], [[], NO_LIST_HINT])
}

// --- the Update button -------------------------------------------------------------------------

// The real renderUpdateState and placeUpdate. Update has two homes: in the player's path, above
// JACK IN with its wire, when an update blocks play; in the row beside Verify for a newer test build.
function updateRow (updateState, newerTestBuild = null) {
  return page({
    functions: ['renderUpdateState', 'placeUpdate', 'playOnce', 'setHudButton'],
    state: { updateState, newerTestBuild, manifestSummary: '' }
  })
}

const placed = (l) => {
  const btn = l.$('update')
  return {
    shown: btn.style.display !== 'none',
    wire: l.$('updateWire').style.display !== 'none',
    home: btn.classList.contains('in-path') ? 'path' : btn.classList.contains('optional') ? 'row' : null,
    moves: btn.moves
  }
}

const BEHIND = { installed: true, upToDate: false, version: 'v0.3.127' }

{
  const l = updateRow(BEHIND)
  l.renderUpdateState()
  check('an update that blocks JACK IN puts Update above it, wired in',
    placed(l), { shown: true, wire: true, home: 'path', moves: [['before', 'updateWire']] })
  l.renderUpdateState()
  check('a second render does not move it again, so the arrival does not replay', l.$('update').moves.length, 1)

  l.$('update').dataset.state = 'busy'
  l.$('update').disabled = true
  l.renderUpdateState()
  check('a render while it is working does not re-arm it', l.$('update').disabled, true)
}

{
  const l = updateRow({ installed: true, upToDate: true, version: 'v0.3.127', testBuildTag: 'v0.3.127-test.93' }, 'v0.3.127-test.94')
  l.renderUpdateState()
  check('a newer test build puts Update in the row beside Verify, with no wire',
    placed(l), { shown: true, wire: false, home: 'row', moves: [['after', 'verify']] })
}

for (const [what, state] of [['up to date', { installed: true, upToDate: true, version: 'v0.3.127' }],
  ['not installed', { installed: false }], ['offline', { installed: true, offline: true }], ['still checking', null]]) {
  const l = updateRow(state)
  l.renderUpdateState()
  check(`${what}: no Update and no wire`, [placed(l).shown, placed(l).wire], [false, false])
}

// The launch bay's spec line says how many of its lamps are lit.
const lampLine = vm.runInNewContext(`(${grab('lampLine').trim()})`)
check('every lamp lit', lampLine(4, 4), '4 systems · all green')
check('one lamp dark', lampLine(3, 4), '3 of 4 systems green')
check('nothing lit yet', lampLine(0, 5), '0 of 5 systems green')

// The launcher's own update. Each row is one thing the page can know when it decides on JACK IN.
const launcherOld = (launcherUpdate, updateState) =>
  vm.runInNewContext(`${grab('launcherIsOld')}; launcherIsOld()`, { launcherUpdate, updateState })
const behind = { launcher: { upToDate: false } }
const level = { launcher: { upToDate: true } }
check('an update downloading locks JACK IN', launcherOld({ state: 'downloading' }, level), true)
check('an update waiting to be applied locks JACK IN', launcherOld({ state: 'ready' }, level), true)
check('a failed updater still knows the release is newer', launcherOld({ state: 'failed' }, behind), true)
check('a launcher with no updater that is behind locks JACK IN', launcherOld(null, behind), true)
check('the updater saying current is the last word', launcherOld({ state: 'current' }, behind), false)
check('a launcher level with the release is not old', launcherOld(null, level), false)
check('a failed updater with GitHub unreachable blocks nobody', launcherOld({ state: 'failed' }, { offline: true }), false)
check('no answer from anywhere yet blocks nobody', launcherOld(null, null), false)

// And JACK IN really obeys it, for an admin too, with everything else ready and the reason first.
const readyToPlay = { serverState: { online: true }, signedInUser: ADMIN }
const jackIn = (over) => {
  const l = rendered(ADMIN, { ...readyToPlay, ...over })
  return { locked: l.$('play').disabled, reason: reason(l).split('  ·  ')[0] }
}
check('nothing wrong: JACK IN is open', jackIn({}).locked, false)
check('update ready: JACK IN is locked and says press Restart to update',
  jackIn({ launcherUpdate: { state: 'ready' } }), { locked: true, reason: 'Your launcher is out of date — press Restart to update' })
check('update downloading: JACK IN is locked and says so',
  jackIn({ launcherUpdate: { state: 'downloading' } }), { locked: true, reason: 'Your launcher is out of date — its update is downloading' })
check('updater failed but the release is newer: locked, with the download named',
  jackIn({ launcherUpdate: { state: 'failed' }, updateState: { installed: true, upToDate: true, launcher: { upToDate: false } } }),
  { locked: true, reason: 'Your launcher is out of date — press Download the new launcher' })

// "Old" is behind the release, not different from it.
const mainJs = text('./main.js')
function grabMain (name) {
  const start = mainJs.search(new RegExp(`^function ${name} `, 'm'))
  if (start < 0) throw new Error(`main.js has no function ${name} - update this selftest`)
  return mainJs.slice(start, mainJs.indexOf('\n}\n', start) + 2)
}
const ManifestKit = await import('./manifest.js')
const describeLauncherUpdate = vm.runInNewContext(`(${grabMain('describeLauncherUpdate')})`, { app: { getVersion: () => '0.3.128' }, ManifestKit })
const released = (version) => describeLauncherUpdate({ assets: [{ name: `launcher-version-${version}.txt` }] }).upToDate
check('behind the release is out of date', released('0.3.129'), false)
check('level with the release is up to date', released('0.3.128'), true)
check('ahead of the release is not out of date', released('0.3.127'), true)
check('versions compare as numbers, not as text', released('0.3.99'), true)

// The trap behind "Restart to update is broken": the assisted installer only reopens the app
// when it runs silently, so a visible install closes the launcher and waits on a wizard.
const packageJson = JSON.parse(text('./package.json'))
check('the installer is still the assisted kind these two checks are about', packageJson.build.nsis.oneClick, false)
check('Restart to update installs silently and reopens the app', /quitAndInstall\(true, true\)/.test(mainJs), true)
check('closing the launcher does not start a silent install with no relaunch', /autoInstallOnAppQuit = false/.test(mainJs), true)

console.log(`\n${passed} passed, ${failed} failed`)
process.exitCode = failed === 0 ? 0 : 1
