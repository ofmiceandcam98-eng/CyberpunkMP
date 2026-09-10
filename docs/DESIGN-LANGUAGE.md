# The design language

What "keep it uniform" means, concretely, so it stops being a taste argument.

zeldfep, 2026-09-07: *"I want the design to be uniform."* This file is that instruction
written down. Everything here is already in `code/launcher-lite/index.html` — this is the
rulebook for it, not a proposal. **The launcher is the truth; if this file disagrees with the
code, the code won.**

Design records that show a decision being made live in `docs/design/`.

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
| `--accent` | `#ff2a32` | **you can act here.** Play, focus rings, primary controls |
| `--hazard` | `#f3c50f` | **stop and read this** — plus the section rail, see below |
| `--ok` | `#3ddc64` | it worked |
| `--bad` | `#f0a05a` | it did not |
| `--update` | `#c2621b` | an update is waiting |
| `--steel` | `#2b303a` | inert chrome — verify button, scrollbar thumb |

**The hazard rule, and it is the subtle one.** Yellow means *warning* everywhere in this app —
the waiting queue row, the `/// WARNING ///` prefix, the busy spinner. It ALSO carries the
section rail, by zeldfep's call on 2026-09-07. The two are kept apart **by form, not by hue**:

- **Flat hazard = a warning.**
- **Striped hazard = position.** The rail's active plate and its end caps are the only striped
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
Avatars are the only exception — they are pictures of people.

**The signature is the clipped corner, not a radius:**

```css
clip-path: polygon(0 0, calc(100% - 6px) 0, 100% 6px, 100% 100%, 6px 100%, 0 calc(100% - 6px));
```

Top-right and bottom-left notched. Used on the rail's arrows and its active plate. Reach for
this when something needs to read as a HUD element rather than a web control.

**Hatching and striping carry meaning, and each angle is spoken for:**

| Pattern | Where | Says |
|---|---|---|
| `120deg`, fine, 2px/6px | `.play::before` | this is the primary control |
| `-45deg`, 14px, animated | `.play.busy` | working, do not press again |
| `45deg`, 3px/7px, hazard | rail caps | end of travel |
| `45deg`, 5px/10px, hazard | rail active plate | you are here |

**Marks:** `❮❮` (`\276E\276E`) trails an action. `///` prefixes a warning
(`/// WARNING ///`). `›` leads a list item.

---

## Layout

- **No visible scrollbars.** zeldfep, 2026-09-04: *"I dont want scroll bar."* The modal body
  scrolls with `scrollbar-width: none` and the section rail is the navigation. If you add a
  scrolling region, give it a way to be crossed that is not a scrollbar.
- **Navigate by section, not by row.** Panes carry `.rail-group` headings and the rail counts
  those. A pane with no headings falls back to counting `.tool` rows — that is a fallback, not
  a target. If a pane grows past about six rows, group it.
- **Space with `gap`**, not per-element margins.
- **No inline `style=` for anything the kit has a class for.** Six inputs each carrying their
  own inline styling is how three radii and an undeclared colour token got in.

---

## Copy

Write from the player's side of the screen. A control says what happens; the result says it
happened. Errors name what broke and what to do — the Checkup panel is the standard to match:
it says *"Server target — <address> — the published server"* then *"no route"*, which is a
diagnosis, not an apology.

Release notes are for players, not for us. Say what changed, say what did not, and say plainly
when a build contains no gameplay change so nobody hunts for a fix that is not in it.

---

## Before you change how something looks

Build the mockup first and put it in front of whoever is deciding. Both of the last two UI
decisions were made in a minute from a page and would have been an argument without one.

**The mockup is a decision aid, not a record.** zeldfep, 2026-09-07: *"its built in so I dont
mind the mock ups being missing, we'll just build on what we have."* Once a decision ships, the
code IS the record and this file is the rulebook for it — a page that disagrees with the
launcher is stale, not a spec. Keep one in `docs/design/` while it is still useful and let it
go when it is not.

**Never copy one into `distrib/`** — the ship carries that directory wholesale to every player.
