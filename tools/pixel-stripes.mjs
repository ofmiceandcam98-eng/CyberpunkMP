// THE PIXEL STRIPES - every striped mark in the launcher, drawn one row of whole pixels at a time.
//
// zeldfep, 2026-11-08, on the plate hatch, the bay hatch and the rail caps: "the render is bad ...
// it keeps drawing everywhere, lets make sure its uniform and the grid is drawn correctly". Each was
// a repeating-linear-gradient, and a gradient's hard edge is not smoothed: on the launcher's screen
// a stripe came out as a ragged staircase - widths jumping between 2 and 3px, steps of 1 and 2 rows
// - with broken fragments wherever it met the edge of its box. These are exact drawings instead:
// whole stripes, every row whole pixels, SVG with crispEdges, used as CSS masks so the colour still
// comes from the design tokens.
//
// Two slopes only, the two that step evenly on the pixel grid: 45 degrees (1px across per row) and
// steep 2:1 (1px across every two rows).
//
//   node tools/pixel-stripes.mjs           prints the :root block to paste into code/launcher-lite/index.html
//   node tools/pixel-stripes.mjs --lines   prints the main window's bar and traces, to paste into its markup
//   import { STRIPES } from './pixel-stripes.mjs'   (tools/tests/launcher_buttons_test.mjs checks the
//                                                    launcher still carries exactly these)

// Percent-encode everything but the characters a quoted data URI can carry as they are.
const encode = (s) => s.replace(/[^A-Za-z0-9_.\-~/:=' ,]/g, (c) => '%' + c.charCodeAt(0).toString(16).toUpperCase().padStart(2, '0'))

function svg (w, h, runs) {
  const d = runs.map(([x, y, rw, rh]) => `M${x} ${y}h${rw}v${rh}h-${rw}z`).join('')
  const body = `<svg xmlns='http://www.w3.org/2000/svg' width='${w}' height='${h}' viewBox='0 0 ${w} ${h}' shape-rendering='crispEdges'><path d='${d}'/></svg>`
  return `url("data:image/svg+xml,${encode(body)}")`
}

// n whole stripes leaning '/', each `stripe` px wide and `gap` px apart, `h` px tall. A stripe moves
// 1px right every `rowsPerStep` rows going up (1 = 45 degrees, 2 = steep 2:1). The block's slanted
// sides run with the stripes, so none is cut.
function block (n, h, stripe, gap, rowsPerStep) {
  if (h % rowsPerStep) throw new Error(`a ${h}px block cannot be whole ${rowsPerStep}-row steps`)
  const steps = h / rowsPerStep
  const period = stripe + gap
  const runs = []
  for (let i = 0; i < n; i++) {
    for (let s = 0; s < steps; s++) runs.push([i * period + s, h - (s + 1) * rowsPerStep, stripe, rowsPerStep])
  }
  const w = (n - 1) * period + (steps - 1) + stripe
  return { w, h, url: svg(w, h, runs) }
}

// A repeating tile: one stripe per period, leaning '/' or '\', wrapping at the tile's edge.
function tile (period, stripe, rowsPerStep, lean) {
  const h = period * rowsPerStep
  const runs = []
  for (let s = 0; s < period; s++) {
    const y = lean === '/' ? h - (s + 1) * rowsPerStep : s * rowsPerStep
    const first = Math.min(stripe, period - s)
    runs.push([s, y, first, rowsPerStep])
    if (stripe > first) runs.push([0, y, stripe - first, rowsPerStep])
  }
  return { w: period, h, url: svg(period, h, runs) }
}

export const STRIPES = {
  '--px-hatch-play': { ...block(4, 28, 2, 4, 2), what: "JACK IN's hatch: 4 whole stripes, steep 2:1" },
  '--px-hatch-done': { ...block(3, 18, 2, 4, 2), what: "Done's hatch: JACK IN's in proportion, 3 stripes" },
  '--px-hatch-bay': { ...block(6, 6, 2, 4, 2), what: "the bay's hatch block on its step: 6 stripes" },
  '--px-dashes': { ...block(2, 3, 18, 6, 1), what: 'the two dashes under a plate\'s base, 45-degree ends' },
  '--px-caps': { ...block(3, 5, 4, 4, 1), what: "the rail's caution caps: 3 hazard stripes at 45 degrees" },
  '--px-hazard': { ...tile(6, 3, 1, '/'), what: "the arrow's stripes: a 6px 45-degree tile, 3 on 3 off" },
  '--px-tread': { ...tile(40, 20, 1, '\\'), what: 'the working tread: a 40px 45-degree tile leaning the other way' }
}

// THE MAIN WINDOW'S LINES - zeldfep, 2026-11-08: the header's old circuit trace redrawn thick ("look
// at the original path and try to fix up the old style ... I like thick lines for those"), and the
// top-left bar and echo dash kept, only updated ("I don't [want] the design changed just updated on
// it I think it adds character"). Polygons on whole pixels with crispEdges, so they step the same on
// every screen. The trace comes in from the edge, jogs down at 45 degrees and lands on a square pad;
// a second trace runs through the same bend to its own pad further across. The diagonal's width is
// t*sqrt(2), so the bend keeps the line's weight. The footer's is the same drawing turned round,
// computed rather than transformed so it stays on whole pixels.
const stop = (offset, cls) => `<stop offset="${offset}" class="${cls}"/>`
const gradient = (id, x1, x2, stops) => `<linearGradient id="${id}" gradientUnits="userSpaceOnUse" x1="${x1}" x2="${x2}" y1="0" y2="0">${stops}</linearGradient>`

function trace (flip, id) {
  const W = 900; const t = 4; const d = t + 4; const s = Math.round(t * Math.SQRT2); const p = d + t; const y0 = 2
  const y1 = y0 + t + 3; const X1 = 444; const X2 = X1 + s + Math.round(3 * Math.SQRT2) - (t + 3); const pad1 = 364; const pad2 = 260
  const H = y1 + d + t + 1
  const P = ([x, y]) => (flip ? [W - x, H - y] : [x, y])
  const pts = (a) => a.map(P).map((c) => c.join(',')).join(' ')
  const box = (x0, y, x1, h) => { const [a, b] = flip ? P([x1, y + h]) : P([x0, y]); return `x="${a}" y="${b}" width="${x1 - x0}" height="${h}"` }
  const run = (y, X, xP) => [[W, y], [X, y], [X - d, y + d], [xP + p, y + d], [xP + p, y], [xP, y], [xP, y + d + t], [X + s - d - t, y + d + t], [X + s - t, y + t], [W, y + t]]
  const ux = (x) => (flip ? W - x : x)
  return `<svg class="ln-trace" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" aria-hidden="true"><defs>` +
    gradient(id, ux(W), ux(pad2), stop(0, 'ln-a') + stop(0.8, 'ln-a') + stop(1, 'ln-a2')) +
    gradient(id + 't', ux(pad2), ux(40), stop(0, 'ln-a') + stop(0.35, 'ln-a2') + stop(1, 'ln-out')) +
    '</defs>' +
    `<rect ${box(40, y1 + (d + t) / 2 - 1, pad2, 2)} fill="url(#${id}t)"/>` +
    `<polygon points="${pts(run(y0, X1, pad1))}" fill="url(#${id})"/>` +
    `<polygon points="${pts(run(y1, X2, pad2))}" fill="url(#${id})"/>` +
    `<rect class="ln-pad" ${box(pad1, y0, pad1 + p, d + t)}/>` +
    `<rect class="ln-pad" ${box(pad2, y1, pad2 + p, d + t)}/>` +
    '</svg>'
}

export const LINES = {
  bar: '<svg class="ln-bar" width="360" height="10" viewBox="0 0 360 10" aria-hidden="true"><defs>' +
    gradient('ln-b', 0, 320, stop(0, 'ln-a') + stop(0.55, 'ln-a') + stop(1, 'ln-a2')) +
    gradient('ln-e', 331, 360, stop(0, 'ln-a2') + stop(1, 'ln-a3')) +
    '</defs><polygon points="0,0 320,0 310,10 0,10" fill="url(#ln-b)"/><polygon points="341,0 360,0 350,10 331,10" fill="url(#ln-e)"/></svg>',
  header: trace(false, 'ln-h'),
  footer: trace(true, 'ln-f')
}

export function rootBlock () {
  return Object.entries(STRIPES).map(([name, s]) => `    /* ${s.what} - ${s.w}x${s.h} */\n    ${name}: ${s.url};\n`).join('')
}

if (import.meta.url === `file:///${process.argv[1].replace(/\\/g, '/').replace(/^\//, '')}`) {
  process.stdout.write(process.argv[2] === '--lines' ? Object.entries(LINES).map(([name, svg]) => `<!-- ${name} -->\n${svg}\n`).join('') : rootBlock())
}
