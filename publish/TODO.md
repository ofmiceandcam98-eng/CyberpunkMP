**Legend** — 🧍 can be done alone · 👥 needs two people online · 🔧 being worked on

## 👥 The one thing that helps most

**Two people in the world at once on v0.3.55+, then post your client log.**

Right now everyone sees *other players wearing a copy of their own character*. The data is fine — the correct appearance arrives, unpacks, and their clothes apply properly. Only the face and body fail. The last two builds log the single number that says why, and it takes one minute of standing near each other to capture.

Log lives in `red4ext\plugins\zzzCyberpunkMP\logs\` — post the **whole file**, not a screenshot. Ignore the size Explorer shows; Windows reports it stale while the game has the file open, so a full log can look empty.

## 👥 Also worth confirming

- **Do other players have clothes now?** Equipping was being skipped for anything the body already carried. Fixed in v0.3.51, unconfirmed.
- **Does the name box appear?** Finish the creator and a box captioned CHARACTER NAME should open. Escape skips it; `/name` works any time.
- **Does NEW CHARACTER actually replace your character?** It genuinely could not until v0.3.56 — the server only saved a character for someone who had none, so remaking yourself was silently thrown away.
- **Do new characters arrive at the Japan Town spawn?** Fixed server-side; it was deciding "brand new" from the account instead of the character.

## 🧍 Can be done alone

- **Report crashes with the log.** This is how nearly every bug so far was found.
- **Try the weapon wheel as a passenger in a moving car.** It crashed a client once and we cannot chase it without a reliable repro. Tell us what you were doing when it happened.
- **Hit Verify files in the launcher.** It now reports every copy of the mod installed and which release each came from. An old copy still loads and overrides the current one.
- **Try a fresh install.** It breaks in ways that only affect people who *don't* already have the mod, so nobody notices.

## 🔧 Being worked on

- **Remote players are built on Panam's character record.** Blanking her name stopped the PANAM nameplates, but the game now invents a random one instead — and her gang and criminal record still show when you scan someone. Everything scanner-related waits on this.
- **First and last name**, with real validation, instead of one free-text box.
- **Kiroshi scanner reading multiplayer characters** — name, occupation, affiliation, bio, in the game's own scanner rather than a custom menu. Server sends a public profile only.

## Known issues

- **Mistyped commands go out as public chat.** Typing `/setname` when you meant `/name` says it to everyone instead of telling you the command doesn't exist.
- **Vehicles are only loosely synchronised.** Riding as a passenger is buggy and cars can bounce, because each machine simulates physics independently. Fixing it properly means one machine owning each vehicle — real work, not a patch.
- **Damage between players doesn't register.**
- **Quests are still on for everyone.** Deliberate — being turned off when the server moves to real hosting.

## Recently fixed

- Chat was never broken, only invisible — the box was never being drawn.
- The launcher was forcing one shared starter save on every launch, so everyone arrived as the same person and it overwrote any character they'd made.
- Two fixes that "didn't work" had never been deployed — the build shipped code but not data files.
- You can no longer launch the game twice, and a leftover copy of the mod is moved aside automatically.
