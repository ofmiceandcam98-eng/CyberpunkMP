# Design records

Standalone pages that carry a UI decision and the reasoning behind it. Open them in a
browser; they need no server and load nothing but Google Fonts.

They are **records, not shipped assets**. Each one uses the launcher's real tokens copied out
of `code/launcher-lite/index.html`, so a page and the launcher can drift apart — the launcher
is always the truth, and a page that disagrees with it is out of date, not a spec.

**Do not copy these into `distrib/`.** The ship copies that directory wholesale and anything
left there reaches every player.

| Page | Decision it records |
|---|---|
| `section-rail.html` | The settings rail: hazard-index over tick-rail, and counting SECTIONS rather than rows. Tools went from 13 stops to 4. Shipped in v0.3.116. |
| `type-scale.html` | Twenty-four ad-hoc font sizes collapsed onto seven tokens, with what visibly moves and what does not. Shipped in v0.3.116. |
| `trade-screen.html` | A trade menu in the GAME's UI language, not the launcher's - three columns, cyan for your side, and the rule that any change clears both confirmations. Proposal; nothing built yet. |
| `synced-traffic-plan.html` | Ambient traffic is unsynced today - nothing in the codebase touches it. Option B staged: suppress vanilla traffic first, then replicate through the path that already works. Proposal. |
| `crash-plan-2026-09-07.html` | The 0x80000003 incident: what was fixed, and the table that maps each `[Boot]` line to the ten lines of startup it points at. |

## Why they are in git

They were built in a scratchpad, published as links, and would have died with the temp
directory — which is the same failure the crew rule about tests in the repo exists to stop
("a wiped temp dir once turned 'the tests passed' into somebody's word"). The decisions
themselves live in `docs/MAP.md`; these are the pages that made them decidable.

The migration cutover checklist is deliberately NOT here. It names internal hosts and paths,
and this repository is public — it lives on the server instead.
