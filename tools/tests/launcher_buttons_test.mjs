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
const HUD_BUTTONS = ['play', 'verify', 'verifyAllMods', 'addModBtn']
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

// .play looks like JACK IN's old class, but the settings modal's Done button still depends on it.
if (!/class="action play" id="closeSettingsDone"/.test(markup) || !/^\s*\.play \{/m.test(markup)) {
  fail("the settings modal's Done button lost its .play styling", 'the styles and #closeSettingsDone', 'keep the .play rules and class="action play" on Done')
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
console.log(`launcher buttons: ${HUD_BUTTONS.length} hud buttons and ${looked.size} element lookups check out`)
