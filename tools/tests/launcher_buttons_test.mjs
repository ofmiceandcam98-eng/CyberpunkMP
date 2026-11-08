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
import { fileURLToPath } from 'node:url'

const FILE = join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'code', 'launcher-lite', 'index.html')
const html = readFileSync(FILE, 'utf8')
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
