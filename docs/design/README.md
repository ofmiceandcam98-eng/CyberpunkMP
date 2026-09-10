# Design records — NOT HERE

UI and menu designs are **internal**. zeldfep, 2026-09-07: *"this does not need to be public
facing server side only … that's for most menus."*

They live on the server, not in this repository:

```
/mnt/vol/projects/_internal-docs/
```

Open them in a browser from there. They need no server of their own and load nothing but a
webfont.

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
