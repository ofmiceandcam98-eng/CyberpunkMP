// Static checks on the launcher's button kit, runnable anywhere with Node and no browser.
//
//   node tools/tests/launcher_buttons_test.mjs
//
// The kit's text lives on an inner span. Assigning textContent to one of these buttons wipes out
// its layers with no error at all: the button keeps working and just stops looking like itself.
// A $('id') that no longer exists throws while the script is loading, which kills every button in
// the launcher, so an id lost in a markup change is checked for here too.
import { readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const FILE = join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'code', 'launcher-lite', 'index.html')
// Line endings normalised: a Windows checkout has CRLF, and the --px-* check reads values up to ';\n'.
const html = readFileSync(FILE, 'utf8').replace(/\r\n/g, '\n')
const scriptAt = html.lastIndexOf('<script>')
const markup = html.slice(0, scriptAt)
const script = html.slice(scriptAt)
const failures = []

function fail (what, where, fix) {
  failures.push(`FAIL ${what}\n     where: ${where}\n     fix:   ${fix}`)
}

// Every hud button in the markup has the layers the kit expects.
const HUD_BUTTONS = ['play', 'verify', 'update', 'verifyAllMods', 'addModBtn', 'closeSettingsDone']
for (const id of HUD_BUTTONS) {
  const match = markup.match(new RegExp(`<button[^>]*id="${id}"[^>]*>([\\s\\S]*?)</button>`))
  if (!match) {
    fail(`hud button #${id} is missing`, 'the markup in index.html', `restore <button id="${id}">; its handler looks it up by id`)
    continue
  }
  if (!/class="[^"]*\bhud\b/.test(match[0])) fail(`#${id} is not a .hud button`, `<button id="${id}">`, 'give it the hud class, see THE BUTTON KIT')
  if (!/class="label"/.test(match[1])) fail(`#${id} has no .label span`, `<button id="${id}">`, 'setHudButton writes to .label, so add <span class="label" data-text="...">')
  if (!/data-text="/.test(match[1])) fail(`#${id}'s label has no data-text`, `<button id="${id}">`, 'the click glitch reads data-text; add it')
  if (!/aria-label="/.test(match[0])) fail(`#${id} has no aria-label`, `<button id="${id}">`, 'the glitch layers repeat the text, so name the button explicitly')
}

// Nothing writes textContent or innerText straight onto those buttons.
if (/playBtn\.(textContent|innerText)\s*=/.test(script)) {
  fail('playBtn has its text assigned directly', 'the launcher script', "use setHudButton(playBtn, text, state)")
}
for (const id of HUD_BUTTONS) {
  if (new RegExp(`\\$\\('${id}'\\)\\.(textContent|innerText)\\s*=`).test(script)) {
    fail(`#${id} has its text assigned directly`, 'the launcher script', 'use setHudButton(btn, text, state)')
  }
}
for (const [, id, body] of script.matchAll(/\$\('(\w+)'\)\.addEventListener\('click', async \(\) => \{([\s\S]*?)\n {2}\}\)/g)) {
  if (HUD_BUTTONS.includes(id) && /\bbtn\.(textContent|innerText)\s*=/.test(body)) {
    fail(`#${id}'s click handler assigns btn.textContent`, `$('${id}') click handler`, 'use setHudButton(btn, text, state)')
  }
}

// Settings' buttons are the bracket button (.sb): the same layers as the kit, and the same rule -
// their text goes through setHudButton, or textContent wipes the corners, the dots and the label.
const SETTINGS_BUTTONS = [...markup.matchAll(/<button class="sb [^"]*"[^>]*\sid="(\w+)"/g)].map((m) => m[1])
if (SETTINGS_BUTTONS.length < 30) {
  fail(`only ${SETTINGS_BUTTONS.length} bracket buttons in Settings`, 'the settings markup in index.html', 'every settings action is a <button class="sb sb-bracket glitches"> - see THE BRACKET BUTTON')
}
for (const id of SETTINGS_BUTTONS) {
  const match = markup.match(new RegExp(`<button[^>]*id="${id}"[^>]*>([\\s\\S]*?)</button>`))
  if (!/class="label" data-text="/.test(match[1])) fail(`#${id} has no .label with data-text`, `<button id="${id}">`, 'copy the layers from another bracket button')
  if (!/<span class="c"/.test(match[1])) fail(`#${id} has no corners`, `<button id="${id}">`, 'copy the layers from another bracket button')
  if (!/aria-label="/.test(match[0])) fail(`#${id} has no aria-label`, `<button id="${id}">`, 'the glitch layers repeat the text, so name the button explicitly')
}
// Writes straight onto one of them, directly or through a variable bound to it in the same block.
for (const block of script.split(/\n(?= {2}(?:async )?function |\n {2}\$\()/)) {
  const bound = new Set()
  for (const m of block.matchAll(/(?:const|let)\s+(\w+)\s*=\s*\$\('(\w+)'\)/g)) if (SETTINGS_BUTTONS.includes(m[2])) bound.add(m[1])
  for (const id of SETTINGS_BUTTONS) {
    if (new RegExp(`\\$\\('${id}'\\)\\.(textContent|innerText|innerHTML)\\s*=`).test(block)) {
      fail(`#${id} has its text assigned directly`, 'the launcher script', 'use setHudButton(btn, text, state)')
    }
  }
  for (const name of bound) {
    const hit = block.match(new RegExp(`\\b${name}\\.(textContent|innerText|innerHTML)\\s*=.*`))
    if (hit) fail(`a settings button has its text assigned directly: ${hit[0].trim().slice(0, 60)}`, 'the launcher script', 'use setHudButton(btn, text, state)')
  }
}
// The bracket button draws nothing clipped, so its focus ring can never be cut off.
for (const rule of markup.matchAll(/^\s*\.sb(\.[\w-]+)*\s*\{[^}]*clip-path/gm)) {
  fail(`a clip-path sits on the bracket button: ${rule[0].trim().slice(0, 60)}`, 'THE BRACKET BUTTON', 'the bracket button has no clip; keep it that way so the focus ring shows')
}

// The gear (G1) is a drawn cog in a clipped hexagon. Its name is all a screen reader gets, a clip on
// the button itself hides its focus ring, and a text write wipes the cog and the hexagon.
const gear = markup.match(/<button class="gear[^"]*"[^>]*\sid="openSettings"[^>]*>/)
if (!gear) {
  fail('the Settings gear is missing', 'the header markup in index.html', 'keep <button class="gear glitches" id="openSettings"> with its hexagon, cog and ticks')
} else if (!/aria-label="[^"]+"/.test(gear[0])) {
  fail('the Settings gear has no aria-label', 'the header markup in index.html', 'it is an icon; give it aria-label="Settings"')
}
for (const rule of markup.matchAll(/^\s*\.gear\s*\{[^}]*clip-path/gm)) {
  fail(`a clip-path sits on the gear button: ${rule[0].trim().slice(0, 60)}`, 'THE GEAR', 'clip the .hx layers, so the focus ring still shows')
}
if (/\$\('openSettings'\)\.(textContent|innerText|innerHTML)\s*=/.test(script)) {
  fail('the script writes text into the Settings gear', 'the launcher script', 'the gear holds an SVG cog and its hexagon; leave its contents alone')
}

// The ladder's rungs are 1px lines placed on whole pixels and the arrow has to share a pixel with
// its tick. railSnap pins the rail's top margin to get there, by layout; a transform on the rail -
// in the stylesheet or set by script - smears every rung, because rungs are snapped before it.
for (const rule of markup.matchAll(/^\s*\.section-rail\s*\{[^}]*\}/gm)) {
  if (/transform\s*:/.test(rule[0])) {
    fail('the section rail is positioned with a transform', '.section-rail in index.html', 'centre it with top: 0; bottom: 0; margin: auto 0; railSnap lands it on a whole pixel')
  }
}
if (/rail\.style\.(transform|translate)\s*=/.test(script)) {
  fail('the script moves the section rail with a transform', 'the launcher script', 'move it by layout (railSnap sets marginTop): a transform is applied after the 1px rungs are snapped, so it smears them')
}
if (!/function railSnap[\s\S]*?rail\.style\.marginTop\s*=/.test(script)) {
  fail('railSnap no longer pins the rail onto a whole pixel', 'railSnap in the launcher script', 'keep it: auto margins leave the rail on half a pixel whenever the leftover is odd, and the arrow then sits half a pixel off its tick')
}

// THE PIXEL GRID (zeldfep, 2026-11-08: "make sure its uniform and the grid is drawn correctly").
// A gradient's hard edge is not smoothed on the launcher's screen, so a diagonal at any slope but
// 45 degrees or 2:1 steps unevenly, and a striped gradient comes out ragged with broken ends.
// Stripes are the pixel drawings from tools/pixel-stripes.mjs; everything else stays on the grid.
const styles = markup.replace(/\/\*[\s\S]*?\*\//g, '')
for (const m of styles.matchAll(/repeating-linear-gradient\(\s*(-?\d+)deg/g)) {
  if (Number(m[1]) % 90 !== 0) {
    fail(`a ${m[1]}deg striped gradient`, 'the styles in index.html', 'draw diagonal stripes from the pixel stripes (tools/pixel-stripes.mjs, --px-*), never a gradient')
  }
}
for (const m of styles.matchAll(/(?<!repeating-)linear-gradient\(\s*(-?\d+)deg/g)) {
  if (Number(m[1]) % 45 !== 0) {
    fail(`a ${m[1]}deg gradient - its hard edge steps unevenly`, 'the styles in index.html', 'cut it at 45 or 135 degrees, which step 1px per row')
  }
}
const { STRIPES } = await import(pathToFileURL(join(dirname(fileURLToPath(import.meta.url)), '..', 'pixel-stripes.mjs')).href)
for (const [name, s] of Object.entries(STRIPES)) {
  const at = styles.indexOf(`${name}: `)
  const value = at < 0 ? null : styles.slice(at + name.length + 2, styles.indexOf(';\n', at))
  if (value !== s.url) {
    fail(`${name} ${value === null ? 'is missing' : 'does not match the generator'}`, ':root in index.html', 'run node tools/pixel-stripes.mjs and paste its output into :root')
  } else if (!styles.includes(`var(${name})`)) {
    fail(`${name} is drawn but never used`, 'the styles in index.html', 'use it, or drop it from tools/pixel-stripes.mjs')
  }
}
// Wires and traces: every bend at 45 degrees. (Icons such as the gear's cog are smoothed SVG and
// are not held to this.)
const wires = [
  ...[...html.matchAll(/<svg class="wire"[\s\S]*?<\/svg>|id="updateWire"[\s\S]*?<\/svg>/g)].flatMap((s) => [...s[0].matchAll(/d="(M[^"]+)"/g)]),
  ...styles.matchAll(/path\('(M[^']+)'\)/g)
]
if (wires.length < 5) fail(`only ${wires.length} wire paths found`, 'the wire SVGs and --wire-path in index.html', 'the wire check lost its targets - fix its selectors')
for (const m of wires) {
  let x = 0; let y = 0
  for (const [, cmd, a, b] of m[1].matchAll(/([MLHV])\s*(-?[\d.]+)(?:\s+(-?[\d.]+))?/g)) {
    const nx = cmd === 'V' ? x : Number(a)
    const ny = cmd === 'H' ? y : cmd === 'V' ? Number(a) : Number(b)
    if (cmd === 'L' && nx !== x && ny !== y && Math.abs(nx - x) !== Math.abs(ny - y)) {
      fail(`a wire bends at ${Math.abs(ny - y)}:${Math.abs(nx - x)} in ${m[1]}`, 'the wire paths in index.html', 'make the diagonal run as far across as down, so it steps 1px per row')
    }
    x = nx; y = ny
  }
}

// Update has two homes and placeUpdate moves it between them. Setting display on it anywhere else
// shows it without its wire, or hides it and leaves the wire pointing at nothing.
for (const line of script.split('\n')) {
  if (/(\$\('update'\)|updateBtn)\.style\.display\s*=/.test(line)) {
    fail('the Update button is shown or hidden directly', `the launcher script: ${line.trim().slice(0, 70)}`, "call placeUpdate('path' | 'row' | null) so its wire and readout follow it")
  }
}

// Every clip-path in the kit stays off the button, or its focus ring disappears.
const kit = html.match(/THE BUTTON KIT ---[\s\S]*?\/\* --- admin server panel --- \*\//)
if (!kit) {
  fail('THE BUTTON KIT section is missing', 'the styles in index.html', 'keep the kit between its comment and the admin server panel')
} else {
  for (const rule of kit[0].matchAll(/^\s*(\.hud|\.plate|\.wired|\.glitches)(\.[\w-]+)*\s*\{[^}]*clip-path/gm)) {
    fail(`a clip-path sits on the button itself: ${rule[0].trim().slice(0, 60)}`, 'THE BUTTON KIT', 'clip the .face or .back layer instead, so the focus ring still shows')
  }
  if (!/@media \(prefers-reduced-motion: reduce\)/.test(kit[0])) {
    fail('the kit has no reduced-motion block', 'THE BUTTON KIT', 'animations turned off must stop the movement and the glitch')
  }
}

// Every id the script looks up statically exists in the markup, unless the script creates it.
const ids = new Set([...markup.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1]))
const created = new Set([...script.matchAll(/\.id\s*=\s*'([^']+)'/g)].map((m) => m[1]))
const looked = new Set([...script.matchAll(/\$\('([^']+)'\)/g)].map((m) => m[1]))
for (const id of looked) {
  if (!ids.has(id) && !created.has(id)) {
    fail(`$('${id}') has no matching element`, 'the launcher script', `add id="${id}" back to the markup, or remove the lookup; a null here can throw while the script loads`)
  }
}

if (failures.length) {
  console.error(failures.join('\n'))
  console.error(`\n${failures.length} launcher button check(s) failed`)
  process.exit(1)
}
console.log(`launcher buttons: ${HUD_BUTTONS.length} hud buttons, ${SETTINGS_BUTTONS.length} bracket buttons and ${looked.size} element lookups check out`)
