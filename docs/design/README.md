# Design records — NOT HERE

UI and menu designs are **internal**. zeldfep, 2026-09-07: *"this does not need to be public
facing server side only … that's for most menus."*

They live on the server, not in this repository:

```
/mnt/vol/projects/_internal-docs/
```

That directory is the storage; the reading surface is the Atlas visualiser's **MOCKUPS**
pane (internal, dev-role only), which serves every design doc posted to the share. Written
working docs — runbooks, checklists, plans — show in its **LIBRARY** pane instead; a
categories file on the share decides which pane a doc belongs to. Post a doc to the share
and it is on the board; nothing needs re-indexing by hand.

## Why they are not in git

This repository is public. A menu mockup gives away unreleased UI, and the reasoning written
beside it gives away more — which parts are guesses, which are load-bearing, and what the
server will and will not accept. None of that is dangerous, and all of it is ours.

## What stays public

Anything a contributor needs in order to build: `docs/MAP.md`, `docs/CLAUDE-HANDOFF.md`,
`docs/DESIGN-LANGUAGE.md`, `CONTRIBUTING.md`. The design language IS public on purpose — it is
the rulebook for code that ships, and a contributor who cannot read it writes something that
does not match.

**The rule:** the launcher and mod are the truth, this repository carries what is needed to
build them, and design records live beside the deployment.
