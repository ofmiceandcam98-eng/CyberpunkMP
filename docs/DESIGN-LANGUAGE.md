# The design language

What "keep it uniform" means, concretely, so it stops being a taste argument.

zeldfep, 2026-09-07: *"I want the design to be uniform."* This file is that instruction
written down. Everything here is already in `code/launcher-lite/index.html` — this is the
rulebook for it, not a proposal. **The launcher is the truth; if this file disagrees with the
code, the code won.**

Design records that show a decision being made are internal; `docs/design/README.md` says where.

---

## The one rule

**Pick a token. Do not invent a value.**

Every number in this kit was chosen once, against the others. A new row that invents its own
size or colour does not look wrong on its own — it looks wrong in aggregate, six months later,
when there are twenty-four font sizes and nobody can say which is correct. That has already
happened here twice, and both cleanups cost an evening.

---

## Colour

| Token | Value | Means |
|---|---|---|
| `--ground` | `#0d0e11` | the page behind everything |
| `--surface` | `#120a0d` | a raised thing — cards, panels, buttons at rest |
| `--line` | `#2a1418` | every border and rule |
| `--ink` | `#e8e2e2` | text you are meant to read |
| `--ink-dim` | `#9a8b8d` | text that supports it — descriptions, labels, disabled |
| `--accent` | `#f73932` | **you can act here.** Play, focus rings, primary controls |
| `--hazard` | `#f3c50f` | **stop and read this** — plus the section rail, see below |
| `--ok` | `#3ddc64` | it worked |
| `--bad` | `#f0a05a` | it did not |
| `--update` | `#c2621b` | an update is waiting |
| `--update-2` | `#6b3410` | the Update plate's base — a shade of `--update`, never a new hue |
| `--update-tread` | `#9a4a14` | the second band of the Update plate's working stripe |
| `--steel` | `#2b303a` | inert chrome — secondary plates, scrollbar thumb |
| `--accent-2` | `#79221b` | the accent ramp: the red fade on the lines and the progress bar |
| `--accent-3` | `#542226` | deeper in the ramp — a secondary plate's base, trailing plates and hairlines |
| `--accent-4` | `#26060a` | where the ramp ends, just above the ground |
| `--plate-dead` | `#2a0d11` | a kit button that cannot be pressed yet |
| `--tread-2` | `#ae3b36` | the second band of the working stripe |
| `--glitch` | `#3ce8ef` | **the click glitch and nothing else.** The one hue outside this palette |

**The red is the game's own** (zeldfep, 2026-11-09: *"the red is good its just a bit vibrant check
the cyberpunk color pallet"*). The old `#ff2a32` had the game's hue with more saturation than any
red in `base\gameplay\gui\common\main_colors.inkstyle`. `--accent` is that file's
`NameplateBackground`; `--accent-2`, `--accent-3` and `--tread-2` are its
`Fullscreen_PrimaryForegroundDarker`, `Fullscreen_SecondaryBackground3` and `MildRed`. A new red
comes from that file too.

**The hazard rule, and it is the subtle one.** Yellow means *warning* everywhere in this app —
the waiting queue row, the `/// WARNING ///` prefix, the busy spinner. It ALSO carries the
section rail, by zeldfep's call on 2026-09-07. The two are kept apart **by form, not by hue**:

- **Flat hazard = a warning.**
- **Striped hazard = position.** The rail's pointer and its end caps are the only striped
  hazard in the app.

So a new warning state is flat. Always. A striped one now reads as "you are here", and the
signal quietly stops working for everyone.

**Two literals are allowed on purpose.** Discord blurple `#5865f2` on the sign-in and join
buttons — it is a brand colour, not ours, and tokenising it would imply we get to change it.
`#140406` as ink on top of accent or hazard fills, because near-black on those two is a fixed
pairing, not a palette entry.

**A `var(--x, fallback)` whose token is declared NOWHERE is a bug wearing a default.** This
file had two: `--bg` (six inputs silently rendering `#111` instead of a surface colour) and
`--warn` (a third amber, `#e0af68`, on screen beside `--hazard` and `--bad`). Neither shows up
in a review that only reads the rule, because the rule reads correctly. Grep before you trust
a fallback.

---

## Type

Two faces, and only two. `--mono` (`ui-monospace, "Cascadia Mono", Consolas`) for anything
machine-shaped — labels, readouts, counts, versions, keys. `--sans` (`system-ui`) for anything
a person wrote — names, descriptions, prose.

Seven sizes. There is no eighth.

| Token | Size | Job |
|---|---|---|
| `--fs-micro` | `.58rem` | rail numbers, badges, mod buttons |
| `--fs-label` | `.68rem` | uppercase mono labels, tabs, footer |
| `--fs-small` | `.74rem` | descriptions, status lines, hints — the biggest group |
| `--fs-body` | `.84rem` | setting names, selects, the lede |
| `--fs-lead` | `1.02rem` | the Play button, and only that |
| `--fs-head` | `1.15rem` | brand, page title, modal title |
| `--fs-icon` | `1.2rem` | chrome glyphs: the gear, the close |

**Labels are mono, uppercase, `letter-spacing: .14em`, weight 700, `--ink-dim`.** That treatment
is the kit's section-heading voice (`.rail-group`, `.admin-title`); reuse it rather than
inventing a second kind of label.

The one relative size in the file is `.72em` on the Play chevron. It is sized against the
button, not the page, and making it absolute breaks it the moment the button changes.

---

## Shape

**Square.** `--radius: 2px`, and most of the kit is `0`. Round corners are not this app.
Avatars are cut as the gear's hexagon, with a red ring.

**The signature is the clipped corner, not a radius:**

```css
clip-path: polygon(0 0, calc(100% - 6px) 0, 100% 6px, 100% 100%, 6px 100%, 0 calc(100% - 6px));
```

Top-right and bottom-left notched. Used on the rail's arrows. Reach for
this when something needs to read as a HUD element rather than a web control.

**The pixel grid** (zeldfep, 2026-11-08: *"lets make sure its uniform and the grid is drawn
correctly"*). The launcher's screen does not smooth a gradient's hard edge or a clipped shape's
diagonal, so the pixel grid shows through: a diagonal at any slope but two steps unevenly, and a
gradient stripe comes out ragged with broken ends where it meets its box. So:

- **Two slopes only: 45 degrees** (1px across per row) **and steep 2:1** (1px across every two
  rows). Chamfers, notches, the bay's step, the main window's bar and traces,
  wires, the chevrons, the gear's hexagon, the warning tag, the rail's carets - all one of the two.
- **No stripe is a gradient.** Every striped mark is a pixel drawing from
  `tools/pixel-stripes.mjs` - whole stripes, whole pixels - pasted into `:root` as `--px-*` and used
  as a CSS mask, so its colour is still a token. A block's slanted sides run with its stripes, so no
  stripe is cut into a fragment.
- `launcher_buttons_test.mjs` fails a diagonal stripe gradient, a gradient cut at any angle but a
  multiple of 45, a wire bent off 45, and a `--px-*` that no longer matches the generator.

**Striping carries meaning, and each stripe is spoken for:**

| Stripe | Where | Says |
|---|---|---|
| hatch: steep 2:1, 2px on 4 off, whole stripes | `.plate .hatch` (JACK IN, Done), the bay's step | this is the primary control |
| tread: 45deg leaning `\`, 20px, animated | a working `.hud` button | working, do not press again |
| hazard: 45deg, 4px on 4 off, 3 whole stripes | rail caps | end of travel |
| hazard: 45deg, 3px on 3 off, two yellows | the rail's pointer | you are here |

Done's hatch is JACK IN's in proportion (3 stripes, 18px tall), not a narrower copy - squeezed into
16x10 the old gradient rendered as a fleck, a stroke and a dot.

**Marks:** `❮❮` trails an action. On a plate it is drawn - two clipped chevrons - not typed:
`\276E` is in none of our fonts, and the fallback draws it as two soft arcs at button size.
`///` prefixes a warning (`/// WARNING ///`). `›` leads a list item.

---

## Buttons

zeldfep, 2026-11-05, choosing from a live test page: *"I like A, C and D all at once"*. The main
actions and the mods panel use one kit, in `THE BUTTON KIT` in `code/launcher-lite/index.html`:
one button with separate moments, so its effects never compete.

| Moment | The plate (`.hud.plate`) | The wired plate (`.hud.wired`) |
|---|---|---|
| Rest | solid plate on an offset base, a raised tab with four square dots | the same tab, fed by a ring node and a trace |
| Hover or Tab | lifts off its base; the dots fill one at a time | a signal runs down the trace; the dots fill |
| Press | drops into the base | pushes onto the trace |
| Click | the text tears and a scan line drops, once | the same, except on the small Install size |
| Working | the dots chase; the `-45deg` stripe runs on JACK IN | the dots chase |

- **The little load bar is the hover cue.** The four dots say "this is live" on hover and on
  keyboard Tab. **The glitch belongs on the click**, because hover would replay it constantly.
- **The glitch never delays what the click does.** It plays over the click and the action starts at once.
- **A working button keeps its lit plate.** Dark means it can't be pressed yet. Busy is not that.
- **Don't show progress you can't measure, or a result you don't have.** "Files OK" only appears after
  the check passed, and a check with no known length chases its dots instead of filling a bar.
- **Every clip-path sits on an inner layer, never on the button**, or the keyboard focus ring is clipped
  away with it. Plain `button.action` buttons still have that problem.
- **Write the label with `setHudButton`.** `textContent` wipes out the layers inside the button with no
  error. `tools/tests/launcher_buttons_test.mjs` checks for this.
- **With animations turned off, nothing travels or tears.** Colour and the load bar still answer.

**Update stands in your path** (zeldfep, 2026-11-07, picking option 2 of the Update test page).
It is the plate in `--update` orange with `#140406` text; white on that orange is 4.15:1 and fails.

- **When an update blocks JACK IN, Update sits above it**, with a wire down into JACK IN's raised
  tab. Hover or Tab sends a signal down the wire and JACK IN's dots stir.
- **Each real step is printed on the wire** as the main process reports it, as a `--fs-micro` mono
  readout. A refusal is said there too, because the status line is far below the button.
- **Done, it turns steel with a green "Updated", then lifts away**, and JACK IN comes alive when the
  next check confirms the files. Green on the orange would be 2.3:1.
- **A newer test build blocks nothing**, so the same button sits in the row beside Verify as a steel
  plate with orange dots. `placeUpdate` is the only thing that shows, hides or moves it.

**Settings has its own small button, the bracket** (zeldfep, 2026-11-08, from two test pages:
*"S2 and C's bays full"*). Settings is a dense list of rows, so its actions are small and quiet.

- **One size: 8rem by 26px**, its label on `--fs-micro` - the step the type doc gives Settings' row
  buttons in its own Settings mock, with the mod buttons and `.hud-mini`. The first cut used the label
  step, which is the tabs' and headings' step. No plate: the label sits inside four registration
  corners. Hover or Tab closes the corners in, turns them red and fills the dots under the label;
  a press flashes a steel fill; the click glitch fires once. Anything that deletes has a red label
  in red corners. Nothing on it is clipped, so its focus ring always shows.
- **Write its text with `setHudButton`**, like the kit. `launcher_buttons_test.mjs` fails a direct
  `textContent` write on any of them.
- **Each group is a module bay**: a notched frame on a hairline, a lit label tab with the group's
  number, a hatch block on the step, a spec line in machine-label text (`3 TOOLS · NETWORK`) and a
  four-dot greeble. The pane draws no frame of its own, so frames are never nested.
- **The tabs are plates**: steel, with the open one lit red.
- **Done stays the kit's red plate.** It closes the window, so it stays the loud one.
- **The window keeps one height**, so the tab bar never moves, and it is a real dialog: focus moves
  in, Tab stays inside, and the launcher behind it is inert until it closes.

See any state of the real page in a browser with `node tools/tests/launcher-stage.mjs`.

---

## Layout

- **No visible scrollbars.** zeldfep, 2026-09-04: *"I dont want scroll bar."* The modal body
  scrolls with `scrollbar-width: none` and the section rail is the navigation. If you add a
  scrolling region, give it a way to be crossed that is not a scrollbar.
- **Navigate by section, not by row.** Panes carry `.rail-group` headings and the rail counts
  those. A pane with no headings falls back to counting `.tool` rows — that is a fallback, not
  a target. If a pane grows past about six rows, group it.
- **The rail is a ladder** (R3, zeldfep, 2026-11-08: *"R3 and G1 but make sure we align the ladder
  to the numbers"*). Each section is a numbered major tick at the point you scroll to to reach it,
  and a striped pointer rides the ladder as you scroll. The rungs are built from the numbers: each
  number is snapped onto the 7px rung grid, every y is a whole pixel, the digits are trimmed to
  cap height so their middle is the tick, and the pointer is centred on that same line. The
  pointer interpolates between the numbers' own scroll points, so it is on a number exactly when
  you are at that section. A fixed pattern from the top, with numbers placed independently, is
  how the first cut put 02 and 03 between rungs. **The arrow is also the handle** (*"make the rail
  arrow on ladder scrollable"*): drag it and the pane scrolls with it, through the same sums run
  backwards, so it stays under the cursor and dropping it on a number lands at that section; a
  press on the bare ladder jumps it there; the wheel over the rail scrolls the pane. The numbers
  and the step tabs stay, and they are the keyboard's way in. Keep the rail on whole pixels BY
  LAYOUT (`railSnap` pins its top margin): the rungs are snapped before any transform is
  applied, so a transform smears them, and an unsnapped rail leaves the arrow half a pixel off
  its tick. `launcher_buttons_test.mjs` fails either.
- **The gear is a hex bolt** (G1, the same pick): a hexagon plate, a red hairline over steel,
  holding a drawn cog on `--fs-icon`, with three ticks under it as its load bar. Hover or Tab
  lights the hairline, fills the ticks and turns the cog one tooth; a click drops the scan line.
  The cog is an SVG because every font draws the gear glyph differently.
- **The main window is three bays on one set of columns** (zeldfep, 2026-11-08). `--cols` is shared
  by the header and `main`, so the name sits over the left bay, the user tabs over the middle and
  the gear at the far end. The sides are equal, so the middle stays on the window's centre. The
  bays are Settings' module bays in the boxes' own colours: `--surface` and a `--line` hairline,
  with only the shape new. **No boxes around the name or the user tabs.**
- **Its lines are drawings, not gradients**: the top-left bar and echo dash (*"I think it adds
  character"*, so the design stays), and the thick bus along the header and footer, two traces
  through one 45-degree bend to their own pads. They come from `tools/pixel-stripes.mjs --lines` and
  `launcher_buttons_test.mjs` fails them if they drift.
- **Its bays stay on whole pixels.** A clip-path edge is not snapped the way a border is, so a bay
  on a fraction blurs its 1px frame. `--cols` rounds the middle to an even width, the footer has a
  whole height, and `snapBays` lands the bays that sit under text of fractional height.
- **Space with `gap`**, not per-element margins.
- **No inline `style=` for anything the kit has a class for.** Six inputs each carrying their
  own inline styling is how three radii and an undeclared colour token got in.

---

## In-game ink screens (coordinate space)

The launcher rules above are HTML/CSS. The in-game menus and HUDs are ink widgets in
redscript, and they have their own coordinate reality — learned by measurement over three
builds, so read this before drawing a second ink screen.

- **Author at 1920×1080; the canvas is scaled to the root.** Compose every ink screen in the
  mockup's 1920×1080 units. The menu root is a larger fixed virtual space, so scale the
  canvas by `rootWidth / 1920` from a **top-left pivot** — anchor and anchor-point both at the
  top-left, so scaling grows down-and-right rather than from the centre. A fill/centre pivot
  puts the whole composition off-screen.
- **`GetScreenSpacePosition()` returns REAL DISPLAY PIXELS, not authored units.** They happen
  to be equal at 1080p and diverge by the scale factor at any other resolution — which is why
  a screen that felt fine on one monitor missed every click on a 1440p one.
  `GetWindowSpacePosition()` returns `0,0` and is useless; do not reach for it.
- **Convert to authored units ONCE, at the input boundary.** Take the screen-pixel click,
  divide by pixels-per-unit (`displayHeight / 1080`), and hand authored units to everything
  downstream. Do NOT carry two coordinate spaces or offset the hit rectangles from the draw
  rectangles — that hack cost three builds; the real fix is one conversion, after which
  hit-testing and drawing share a single space.
- **Same rules on the HUD layer, not just the menu.** An overlay that hangs off the chat/HUD
  controller scales by `rootWidth / 1920` the same way. The pattern generalises; the medium
  does not change between menu and HUD.

---

## In-game ink screens (look and behaviour)

From the character selector and the trade overlay. Their mockups are out of date now, so each
rule here has been checked against the code that shipped.

**Building one**

- **Build the tree at runtime from primitives** (`MpCsRect`, `MpCsText`, `MpCsBorder`). An
  `.inkwidget` authored from the command line hard-crashed the client when it was spawned
  (2026-09-13). The JSON round trip is not byte-exact either, so never edit
  `multiplayer_ui.inkwidget`, which holds chat, emotes and the job list.
- **An `inkImage` binds an `.inkatlas`, never a raw `.xbm`.** The selector's backdrop is a
  single-texture atlas.
- **A full-screen canvas stays non-interactive.** An interactive one swallows input and soft-locks
  the game.
- **When you probe the engine, give silence a branch.** An unresolvable resource never calls back,
  so a probe with only success and failure paths reports nothing. Run a known-good control beside
  it.

**Menu screens**

- **A menu screen replaces the menu.** zeldfep, 2026-09-08: *"not as an overlay — as a new layer
  within the menu"*. The game has no moddable way to add a menu scenario, because the name one
  opens is registered in the engine, so hiding the menu list is the only way there. It is also a
  correctness fix: clicks that missed a hit rectangle were landing on the buttons behind it.
- **Putting the menu back comes first and is unconditional.** `PopulateMenuItemList` shows the
  list before it decides anything, because a hidden list with no way back is a main menu nobody
  can use. Hide after the menu's own refresh, or the refresh puts it straight back.
- **A screen you can close needs a way back in.** CHARACTER SELECT is in the connected menu for
  this reason. Escape arrives as both `back` and `cancel`, so handle both.

**HUD overlays**

- **A tool for dealing with someone sits beside the conversation.** The trade overlay is drawn on
  the chat controller with chat live to its left, not as a full-screen menu. Trades get negotiated
  in text, and a full-screen menu pulls you out of an RP scene.
- **Place it as a fraction of the measured root**, not in pixels (`MpTrShiftFracX/Y`). zeldfep,
  2026-09-14: *"move them right past the chat box; dynamic to monitor size"*.
- **Tune the size live, then bake it.** The trade mockup was honest about game scale and the
  shipped overlay still needed a zoom of 2.6 on a real screen. `/trsave` keeps a tuned placement.
- **Over live gameplay, panels are see-through**: a low-alpha fill, background blur and a bright
  border, so each box reads as its own (zeldfep, 2026-09-14: *"blur see through"*).
- **No clipped-corner diamonds in ink.** zeldfep read them as clutter on the selector and the talk
  button (2026-09-14), and nothing draws them now. The clipped corner is a launcher mark.
- **Changing an offer clears both confirmations.** The server enforces it: somebody who agreed to
  the old offer has not agreed to this one.

**Legible from across the room**

- **A token is where colour starts; it still has to be read against what is behind it.** The
  selector's yellow shipped at double the token on two channels, which is bloom, and was illegible
  over the city backdrop (zeldfep, 2026-09-09: *"make it a bit darker"*). Stay at or just above 1.0.
- **One accent per screen.** Gold on the selector, cyan on trade. Red is the shared warning.
- **Text over an image gets a scrim.** The selector puts black at .55 under the roster and .45
  along the bottom.
- **Selection moves as well as recolours.** The selected card steps 14 units right, which reads
  when colour alone does not.
- **One filled control per screen: the one that ends it.** ENTER NIGHT CITY is the only solid
  button on the selector. Everything else is an outline.

**Mocking one up**

- **Mock at game scale, on the ground it will sit on.** Author inside a 1920×1080 stage and scale
  the stage as one piece, so 14 means 14 game units. Put something busy behind it: a transparent
  panel over a flat ground looks identical to an opaque one.
- **Say which numbers are read from the code and which are proposals.** Treat the proposals as
  unproven until a real screen has judged them.
- **Draw it, don't guess it.** zeldfep, 2026-09-08: *"how about you let me draw where the buttons
  are instead of guessing"*. When placement is in dispute, list the numbers the code actually
  passes so they can be read and corrected. Date that list, because the first layout change makes
  it stale.

---

## Copy

Write from the player's side of the screen. A control says what happens; the result says it
happened. Errors name what broke and what to do — the Checkup panel is the standard to match:
it says *"Server target — <address> — the published server"* then *"no route"*, which is a
diagnosis, not an apology.

Release notes are for players, not for us. Say what changed, say what did not, and say plainly
when a build contains no gameplay change so nobody hunts for a fix that is not in it.

---

## Documents

Working pages, mockup write-ups and reports have their own look, and it is not the launcher's.
zeldfep, 2026-11-05, on the Selector Draw Map: *"it had a simple yet unique style"*, and on whether
it should shape the launcher: *"Just for documents"*. The seven type sizes are the launcher's and
do not apply here.

| Part | Treatment |
|---|---|
| Eyebrow | mono 12px, `.38em`, uppercase, `--hazard` |
| Title | 42px bold, uppercase, line-height `.98` |
| Sub line | mono 13px, `.2em`, uppercase, faint: what it is and its date |
| Rule | 1px, `--accent` fading to nothing |
| Section label | mono 13px, `.24em`, uppercase, `--accent`, a hairline running out to the edge |
| Body | 15px at 1.55, lines held to about 76 characters |
| Table head | mono 11px, `.16em`, uppercase, faint |
| Numbers and code | mono, cyan `#3ce8ef`, tabular figures |

- **Colour sorts the page by job**: gold eyebrow, red section labels, cyan numbers and code, white
  row names, grey notes. The colours are the launcher's tokens plus `#6b5f61` for faint text. Cyan
  means data here; the rule that keeps it to the click glitch is the launcher's.
- **Almost no boxes.** A section is a label and a hairline. Tables have row hairlines and no cell
  borders.
- **Loud once.** One box with a hazard strip, for the thing that bit, and nothing competing with
  it. Cut the strip at 45 degrees: the old pages' 115-degree gradient steps unevenly on a screen
  that does not smooth it.
- **Open with the words of whoever asked**, in bold, then the numbers.
- **Say how you know.** Tag each item with its state (works, unproven, open, blocked, measured),
  and give a table of facts a column for how each was established. A build succeeding is not a
  result.
- **A stale page says so at the top**, names what is still live, and stays where it is.
- **Fonts:** the pages name Chakra Petch and Share Tech Mono without loading them, so they render
  in system fallbacks, and that is the look zeldfep kept (2026-11-05). A mockup of the game does
  load a face close to the game's.

---

## Before you change how something looks

Build the mockup first and put it in front of whoever is deciding. Both of the last two UI
decisions were made in a minute from a page and would have been an argument without one.

**The mockup is a decision aid, not a record.** zeldfep, 2026-09-07: *"its built in so I dont
mind the mock ups being missing, we'll just build on what we have."* Once a decision ships, the
code IS the record and this file is the rulebook for it — a page that disagrees with the
launcher is stale, not a spec. Post one to the internal-docs share while it is still useful
(`docs/design/README.md` says where and how it gets read) and let it go when it is not.

**Never copy one into `distrib/`** — the ship carries that directory wholesale to every player.
