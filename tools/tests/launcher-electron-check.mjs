// Starts a packaged launcher in real Electron and checks the button kit there, because the browser
// stage can't prove anything about the packaged app: its fonts, its CSP, its actual Chromium.
//
//   node tools/tests/launcher-electron-check.mjs "<path to Night City Online Launcher.exe>" [screenshot folder]
//
// Safe beside an installed launcher. It uses a throwaway data folder, so it has its own
// single-instance lock and can't see your sign-in or settings, and it stays signed out, so JACK IN
// is locked and can't start the game. The launcher claims nxm:// on every start, so this puts that
// handler back as it found it. A window shows for about 15 seconds. The whole run has a deadline.
import { spawn, spawnSync } from 'node:child_process'
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const [exe, outDir] = process.argv.slice(2)
const PORT = 9333
const DEADLINE_MS = 90000
const NXM_KEY = 'HKCU\\Software\\Classes\\nxm\\shell\\open\\command'
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))
const failures = []
const check = (ok, what, fix) => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`)
  if (!ok) failures.push(`${what} - ${fix}`)
}

if (!exe) {
  console.error('usage: node tools/tests/launcher-electron-check.mjs "<launcher exe>" [screenshot folder]')
  process.exit(2)
}

function readHandler () {
  const result = spawnSync('reg', ['query', NXM_KEY, '/ve'], { encoding: 'utf8' })
  const match = result.stdout && result.stdout.match(/REG_SZ\s+(.+)/)
  return match ? match[1].trim() : null
}

function restoreHandler (before) {
  const now = readHandler()
  if (now === before) return 'unchanged'
  if (before === null) return `was not set before and is now ${now}; left it, because deleting it could remove someone else's setting`
  const result = spawnSync('reg', ['add', NXM_KEY, '/ve', '/d', before, '/f'], { encoding: 'utf8' })
  return result.status === 0 && readHandler() === before ? 'restored' : `COULD NOT RESTORE: it still reads ${readHandler()}`
}

const handlerBefore = readHandler()
const dataDir = mkdtempSync(join(tmpdir(), 'nco-electron-check-'))
const app = spawn(exe, [`--user-data-dir=${dataDir}`, `--remote-debugging-port=${PORT}`], { stdio: 'ignore' })
let startFailed = null
app.on('error', (error) => { startFailed = error })

function cleanUp () {
  if (app.pid) spawnSync('taskkill', ['/PID', String(app.pid), '/T', '/F'], { stdio: 'ignore' })
  const handler = restoreHandler(handlerBefore)
  try { rmSync(dataDir, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 }) } catch { /* temp folder */ }
  console.log(`nxm handler: ${handler}`)
  if (handler.startsWith('COULD NOT')) failures.push(`nxm handler ${handler} - set it back by hand, or start the normal launcher, which claims it again`)
}

setTimeout(() => {
  console.error(`FAIL no result after ${DEADLINE_MS / 1000}s - the launcher did not answer. Run the exe by hand to see whether it opens.`)
  cleanUp()
  process.exit(1)
}, DEADLINE_MS).unref()

async function connect () {
  for (let tries = 0; tries < 60; tries++) {
    if (startFailed) throw new Error(`could not start ${exe}: ${startFailed.message}`)
    if (app.exitCode !== null) {
      throw new Error(`the launcher exited with code ${app.exitCode} on startup`)
    }
    try {
      const targets = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json()
      const page = targets.find((t) => t.type === 'page' && t.url.includes('index.html'))
      if (page) return page.webSocketDebuggerUrl
    } catch { /* not listening yet */ }
    await sleep(500)
  }
  throw new Error(`nothing answered on port ${PORT}: is something else using it, or does this build refuse --remote-debugging-port?`)
}

async function run () {
  const socket = new WebSocket(await connect())
  await new Promise((resolve, reject) => {
    socket.addEventListener('open', resolve, { once: true })
    socket.addEventListener('error', () => reject(new Error('could not attach to the launcher page')), { once: true })
  })

  let nextId = 1
  const pending = new Map()
  const pageErrors = []
  socket.addEventListener('message', (event) => {
    const message = JSON.parse(event.data)
    if (message.id && pending.has(message.id)) {
      const { resolve, reject } = pending.get(message.id)
      pending.delete(message.id)
      if (message.error) reject(new Error(message.error.message))
      else resolve(message.result)
    } else if (message.method === 'Runtime.exceptionThrown') {
      pageErrors.push(message.params.exceptionDetails.exception?.description || message.params.exceptionDetails.text)
    } else if (message.method === 'Log.entryAdded' && /security|Content Security Policy/i.test(message.params.entry.text)) {
      pageErrors.push(message.params.entry.text)
    }
  })
  const send = (method, params = {}) => new Promise((resolve, reject) => {
    const id = nextId++
    pending.set(id, { resolve, reject })
    socket.send(JSON.stringify({ id, method, params }))
  })
  const read = async (expression) => {
    const { result, exceptionDetails } = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })
    if (exceptionDetails) throw new Error(`page threw on: ${expression.slice(0, 80)}`)
    return result.value
  }
  // The launcher's startup checks finish at their own pace and change how many lines the status
  // takes, which moves the buttons. Aiming before that settles misses, so wait for two matching reads.
  const centre = async (id) => {
    const measure = () => read(`(() => { const r = document.getElementById('${id}').getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 } })()`)
    let last = await measure()
    for (let tries = 0; tries < 20; tries++) {
      await sleep(400)
      const now = await measure()
      if (now.x === last.x && now.y === last.y) return now
      last = now
    }
    return last
  }
  const under = (at, id) => read(`(() => { const u = document.elementFromPoint(${at.x}, ${at.y}); return 'pointer at ' + ${Math.round(at.x)} + ',' + ${Math.round(at.y)} + ' is over ' + (u ? (u.id || u.className || u.tagName) : 'nothing') + ', window ' + innerWidth + 'x' + innerHeight + ', ${id} hover=' + document.getElementById('${id}').matches(':hover') })()`)
  const mouse = (type, at, extra = {}) => send('Input.dispatchMouseEvent', { type, x: at.x, y: at.y, ...extra })
  const shot = async (name) => {
    if (!outDir) return
    mkdirSync(outDir, { recursive: true })
    const { data } = await send('Page.captureScreenshot', { format: 'png' })
    writeFileSync(join(outDir, `${name}.png`), Buffer.from(data, 'base64'))
  }

  await send('Runtime.enable')
  await send('Log.enable')
  await send('Page.enable')

  // The startup checks change the layout when they land: a finished update check can put an Update
  // button beside Verify, right under where the pointer was aimed. Touch nothing until they're done.
  for (let tries = 0; tries < 60; tries++) {
    const checking = await read(`(() => {
      const text = ['srvText', 'updateText', 'tsText'].map((id) => document.getElementById(id)?.textContent || 'Checking')
      return text.some((t) => t.includes('Checking'))
    })()`).catch(() => true)
    if (!checking) break
    await sleep(500)
  }
  await sleep(500)

  const facts = await read(`(async () => {
    await document.fonts.ready
    const play = document.getElementById('play')
    return {
      kitButtons: document.querySelectorAll('.hud').length,
      playLayers: !!play.querySelector('.back') && !!play.querySelector('.face .label') && !!play.querySelector('.pips'),
      playLabel: play.querySelector('.label').textContent,
      playFont: getComputedStyle(play).fontFamily,
      fontLoaded: document.fonts.check('16px "NCO Display"'),
      clipOnButton: getComputedStyle(play).clipPath,
      faceClip: getComputedStyle(play.querySelector('.face')).clipPath.slice(0, 7),
      reduced: matchMedia('(prefers-reduced-motion: reduce)').matches,
      version: document.getElementById('footerVersion').textContent,
      doneBackground: getComputedStyle(document.getElementById('closeSettingsDone')).backgroundColor,
      chrome: navigator.userAgent.match(/Chrome\\/(\\d+)/)?.[1],
      width: innerWidth,
      height: innerHeight
    }
  })()`)
  console.log(`running: ${facts.version}, Chromium ${facts.chrome}, window ${facts.width}x${facts.height}, animations ${facts.reduced ? 'OFF' : 'on'}`)
  check(facts.kitButtons >= 4, `the button kit is in this build (${facts.kitButtons} buttons)`, 'this exe was not built from the kit commit')
  check(facts.playLayers && facts.playLabel === 'JACK IN', 'JACK IN has its layers and its label', 'something wrote textContent on it; use setHudButton')
  check(facts.fontLoaded && facts.playFont.includes('NCO Display'), 'the display font loads in the packaged app', 'fonts/ is missing from the package or the CSP blocks it')
  check(facts.clipOnButton === 'none' && facts.faceClip === 'polygon', 'the clip-path is on the inner layer, not the button', 'the focus ring would be cut off')
  check(facts.doneBackground === 'rgb(255, 42, 50)', "the settings modal's Done button kept its red", 'the .play rules were removed')
  await shot('electron-rest')

  // A real pointer sends a stream of moves. Chromium can drop a single synthetic one before the
  // window is active, which looks exactly like a hover rule that doesn't work.
  // The window is on a real desktop, so the real cursor can win too. What's being judged is whether
  // the rule applies WHILE hovered, so keep moving until Chromium says it is hovered.
  const verify = await centre('verify')
  for (let tries = 0; tries < 8; tries++) {
    for (const step of [40, 20, 10, 0]) {
      await mouse('mouseMoved', { x: verify.x - step, y: verify.y })
      await sleep(60)
    }
    if (await read(`document.getElementById('verify').matches(':hover')`)) break
    await sleep(250)
  }
  await sleep(600)
  const hover = await read(`(() => {
    const b = document.getElementById('verify')
    return { dots: [...b.querySelectorAll('.pips i')].map((i) => getComputedStyle(i).opacity), plate: getComputedStyle(b.querySelector('.face')).transform, base: getComputedStyle(b.querySelector('.back')).transform }
  })()`)
  check(hover.dots.every((d) => d === '1'), 'hover fills the load bar', `the hover rule did not apply in Electron; ${await under(verify, 'verify')}`)
  if (facts.reduced) {
    check(hover.plate === 'none', 'with animations off, hover does not move the plate', 'the reduced-motion rules did not apply')
  } else {
    check(hover.plate.includes('1, -1') && hover.base.includes('-6, 6'), 'hover lifts the plate off its base', 'the plate hover rule did not apply')
  }
  await shot('electron-hover')

  // Pressed, then released somewhere else, so Verify's own click never fires.
  await mouse('mousePressed', verify, { button: 'left', buttons: 1, clickCount: 1 })
  await sleep(200)
  const pressed = await read(`getComputedStyle(document.querySelector('#verify .face')).transform`)
  if (!facts.reduced) check(pressed.includes('-3, 3'), 'pressing drops the plate into its base', `the :active rule did not apply, plate reads ${pressed}; ${await under(verify, 'verify')}`)
  const away = { x: 8, y: 8 }
  await mouse('mouseMoved', away, { button: 'left', buttons: 1 })
  await mouse('mouseReleased', away, { button: 'left', buttons: 0, clickCount: 1 })

  // Add mod with an empty box only prints a message, so it can really be clicked.
  await read(`(() => {
    window.__anims = []
    document.addEventListener('animationstart', (e) => window.__anims.push(e.animationName), true)
    document.getElementById('addModInput').value = ''
  })()`)
  const add = await centre('addModBtn')
  await mouse('mouseMoved', add)
  await sleep(100)
  await mouse('mousePressed', add, { button: 'left', buttons: 1, clickCount: 1 })
  await mouse('mouseReleased', add, { button: 'left', buttons: 0, clickCount: 1 })
  await sleep(90)
  await shot('electron-glitch')
  await sleep(400)
  const clicked = await read(`({ status: document.getElementById('status').textContent, anims: window.__anims, disabled: document.getElementById('addModBtn').disabled })`)
  check(clicked.status.includes('Paste a Nexus'), 'a click on Add mod reaches its real handler', 'the click did not land on the button')
  if (facts.reduced) {
    check(!clicked.anims.includes('glitch-a'), 'with animations off, the click does not glitch', 'the reduced-motion rules did not apply')
  } else {
    check(clicked.anims.includes('glitch-a') && clicked.anims.includes('glitch-scan'), 'the click fires the glitch', `animations seen: ${clicked.anims.join(', ') || 'none'}`)
  }

  await read(`document.getElementById('verifyAllMods').focus()`)
  await send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 })
  await send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 })
  await read(`document.getElementById('verifyAllMods').focus()`)
  await sleep(500)
  const focus = await read(`(() => {
    const a = document.activeElement
    return { id: a.id, visible: a.matches(':focus-visible'), dots: [...a.querySelectorAll('.pips i')].map((i) => getComputedStyle(i).opacity), outline: getComputedStyle(a).outlineStyle }
  })()`)
  check(focus.id === 'verifyAllMods' && focus.visible && focus.outline === 'solid' && focus.dots.every((d) => d === '1'), 'keyboard focus shows the ring and fills the load bar', JSON.stringify(focus))
  await shot('electron-focus')

  check(pageErrors.length === 0, 'no page errors or CSP blocks', pageErrors.slice(0, 3).join(' | '))
  socket.close()
}

let code = 0
try {
  await run()
} catch (error) {
  console.error(`FAIL ${error.message}`)
  code = 1
}
cleanUp()
if (failures.length) {
  console.error(`\n${failures.length} check(s) failed:\n${failures.join('\n')}`)
  code = 1
} else if (!code) {
  console.log('\nall good in real Electron')
}
process.exit(code)
