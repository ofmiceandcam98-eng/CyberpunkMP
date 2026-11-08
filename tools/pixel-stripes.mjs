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

export function rootBlock () {
  return Object.entries(STRIPES).map(([name, s]) => `    /* ${s.what} - ${s.w}x${s.h} */\n    ${name}: ${s.url};\n`).join('')
}

if (import.meta.url === `file:///${process.argv[1].replace(/\\/g, '/').replace(/^\//, '')}`) process.stdout.write(rootBlock())
