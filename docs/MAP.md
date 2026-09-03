# The Map

**What this is:** the call-back document — the ledger of everything open, in flight, or
deliberately deferred, followed by the geography of the code and the pipelines. When
something feels missed, this is where it should already be written down. Maintained by
whoever lands or finds things (both Claude streams included): landing an item removes it
from the ledger in the same commit; finding one adds it. A ledger that is not updated in
the landing commit is how items get missed twice.

Last full revision: 2026-08-22, after v0.3.106 (player combat).
Partial pass 2026-08-30 (Claude Code): the character-start run. Dogtown solved and
confirmed in game, the Phantom Liberty prologue traced to the gamedef and removed, the
clean start shipped, phone calls stopped at the right layer, the manifest key minted and
parked. Rewrote the appearance row - it was filed as "waiting on a test build" and has
since shipped twice, so it is now a live bug rather than an undelivered fix. Did NOT
re-audit combat/vehicle-damage/modlist below.
Partial pass 2026-08-29 (Copilot, VM checkout): reconciled THE crash entry against
what actually shipped (v0.3.111-113) and the commits between 3bf2446 and d5d506f -
nobody had touched this file across that whole run, which is exactly the "missed
twice" failure mode this doc warns about. Did NOT re-audit combat/vehicle-damage/
manifest/modlist sections below - those are as of 2026-08-26 still.

---

## 1. THE LEDGER

### IN FLIGHT — what is being worked on right now (2026-09-02)
Keep this block current; it is the first thing anyone should read. Cam: *"we should update
the mental map pretty often."* A stale in-flight list is worse than none, because it sends
people to work that is already done.

- **PLAYER-TO-PLAYER CALLS — BUILT, NOT SHIPPED (`7be0a20`).** Server only, **no protocol
  change**. Cam's rule, stricter than the brief that prompted it: *"player to player calls are
  the only calls that can come through."* Every game-originated call stays blocked.
  - **THE SONGBIRD GATE IS NOT MODIFIED, AND THAT IS THE DESIGN.** Do not "improve" this by
    relaxing `PhoneSystem.OnTriggerCall` — read this first. `OnTriggerCall` takes a
    `questTriggerCallRequest`, which is the **quest system's** request type; the quest system
    is the only thing that builds one. Its `isPlayerTriggered` field means *"the player
    triggered this QUEST call"* — ringing a fixer back from the journal — **not** "a
    multiplayer player started a call". Gating on it would let a class of story calls back in
    and would make the Songbird block depend on a field the mod never sets and cannot audit.
  - **A player call never becomes a `questTriggerCallRequest`.** It arrives as a chat command,
    lives in `CallStore`, and is presented by the mod. The two kinds of call share no field
    and no entry point, so the origin is unambiguous **by construction** rather than by
    inspection. "Player calls work AND the prologue stays blocked" is therefore not a test
    that has to pass — it is a property of the shape.
  - **Verified, not asserted:** `Quests.reds` is unmodified, and every mention of
    `PhoneSystem` / `OnTriggerCall` / `questTriggerCallRequest` / `isPlayerTriggered` across
    all changed files is a **comment**. No line of code in the feature references any of them.
  - **The brief's "do not build a parallel system, route through PhoneSystem" was declined
    deliberately.** Routing a player call through `OnTriggerCall` means forging a
    `questTriggerCallRequest`, after which the gate can no longer tell the two apart. That
    instruction and the brief's own acceptance criteria are in tension; the criteria win.
  - **Voice is what makes it a call.** Proximity voice already existed; a connected call also
    routes the speaker's frames to the other party regardless of distance, checked against the
    listener's **active** character. Resolved once per frame, not per listener — that loop
    runs for everybody online, 50×/second per speaker.
  - **Sessions in memory, history on disk.** A call is a conversation, not property: a restart
    mid-call should mean the call ended, which an empty session list already means. Held in a
    `std::list` — `Active()`/`Find()`/`Expired()` hand out pointers, and a vector would
    reallocate and dangle them intermittently, only on a busy server.
  - **A blocked call reads exactly like an unanswered one.** A refusal that differs is a
    refusal that tells somebody they were blocked.
  - **No call id is accepted from the client.** `/answer`, `/decline`, `/hangup` resolve the
    sender's own active call, so one player cannot hang up another's.
  - **Switching characters ends the outgoing character's call** before the switch. Belt and
    braces today — the puppet check already refuses an in-world switch — but it is the line
    that stops a call surviving if that rule is ever relaxed.
  - Commands: `/call`, `/answer`, `/decline`, `/hangup`, `/calls`. Ring timeout 30s.
  - **35 checks pass** against the real `CallStore.h`, covering the brief's matrix and the
    "one player's two characters share nothing" case.
  - **The remaining piece is presentation.** This is surfaced through chat, not the phone UI.
    A real incoming-call panel is a presentation layer over the same session state — build it
    against `CallStore`, never as a second call implementation.

- **DIGITAL LIFE: MESSAGING, CONTACT NAMES AND BLOCKING — BUILT, NOT SHIPPED.** Phase 1 of the
  "character digital life" brief ChatGPT wrote on 2026-09-02. **Server-only — no protocol
  change**, so unlike the slot work this could ship on its own.
  - **Read the brief against the code before building any more of it.** Most of its Phase 1
    and half of Phase 2 already existed: `CharacterId`, a unique per-character `PhoneNumber`,
    per-character `Contacts`, `/number`, `/addcontact`, `/contacts`, and `/pay` (which is
    already server-authoritative, atomic, and audit-logged — brief §10 describes what is
    built). **Two of its instructions would have regressed decisions already made:** it
    specifies `char_8f31a92c` for character ids, which is the 16-hex shape Cam rejected as
    "too big" and which has no check symbol; and 10-digit phone numbers, where ours are
    already unique strings and changing the digit count would break every number handed out.
    Neither was adopted. Its central rule — **the character owns the digital identity, the
    account does not** — was already how `CharacterRecord` is built.
  - **`MessageStore` (`code/server/native/MessageStore.h`), addressed by `CharacterId` and
    never by account.** Its own store, not a field on `CharacterRecord`, for a reason that
    matters: `SaveCharacter` REPLACES a character wholesale from what the client reported, so
    a message arriving between a client's read and its write would vanish silently. Same class
    of bug as the money thrash.
  - **Offline is the normal case.** A message is written to disk first and delivered second,
    and `Delivered` is stored per message rather than assumed — so a recipient who was away,
    crashed, or dropped the push gets it on their next arrival. `MarkDelivered` runs *after*
    every line is handed to the connection: showing a message twice is a blemish, losing one
    is the bug.
  - **Conversations are keyed on the sorted pair.** A-texts-B and B-texts-A are one thread.
    Unsorted, they are two threads each holding half of what was said, and both people see
    the other ignoring them — a bug that reads as a UI problem for a week. Repaired on load
    too, so a hand edit cannot create it.
  - **Contacts gained a name (`Contact` struct) with a two-way-safe migration**, verified by
    a standalone test: an existing bare-string list loads, and an unnamed contact still
    *writes* as a bare string, so only accounts that actually save a name change shape and a
    rollback keeps working for everyone else. Deleting a contact never touches message
    history — that falls out of messages being their own store rather than hanging off the
    phone book.
  - **Blocking is per character, silent, and aimed at a number.** Blocked messages are
    accepted and dropped, never refused: a refusal is a signal, and "delivery failed for this
    number, sent for that one" tells somebody they were blocked. Nothing is stored either —
    storing and never delivering would dump the backlog on whoever later unblocks.
  - **`/addcontact` compared Discord ids, which the slot work made wrong.** It would tell a
    player their own second character's number was "your own number" and refuse it. Now
    compared by character. **Look for more of these** — every account-keyed phone lookup is
    suspect now that an account can hold four characters.
  - Commands: `/text`, `/texts`, `/read`, `/contactname`, `/delcontact`, `/block`,
    `/unblock`, `/blocked`. Delivery on arrival hooks the spawn path, which is also where a
    character *switch* lands — so switching hands over the new character's inbox and none of
    the old one's.
  - **Not built, and why:** calls (§12–13) need `PhoneSystem.OnTriggerCall`, which is
    currently blocking *every* call — see [[project-cyberpunkmp-phone-calls]] before touching
    it. Money attachments (§9–11) are **blocked on the money persistence bug**, and building
    transfers on a balance that does not survive a session is how duplication bugs are born.
    Social, email, computers, files, contracts, taxi and media (§18–28, §41–55) are later
    phases with no foundation dependency on this one.

- **SLOTS, SOFT DELETE AND THE SELECTOR — BUILT, NOT SHIPPED (`9a70e1d`).** Branch only, no
  release, `main` untouched. **This is a flag day when it does ship** — `CharacterSummary`
  gains `slot`/`is_active`, `AuthenticationResponse` gains `character_slots`, and there are
  two new client messages, so every un-rebuilt client is refused. Batch anything else waiting
  on a protocol change with it.
  - **Most of the server model was already here and unused.** `Characters` is a vector,
    `ActiveSlot` exists, `RetiredCharacters` exists, and `RetireCharacter` already took a slot
    and already did a **soft delete** — the row moves rather than being erased, so an id is
    never reissued and a deletion can be undone by hand. What was missing was everything above
    it, and the fact that `SendCharacterList` collapsed the roster to the active character
    wrapped in a list of one.
  - **One slot for a player, four for admin and above** (`PlayerStore::SlotsForLevel`). A
    permission, so the server decides and the client is told — an allowance the client
    computes is one it can raise. **Lowering it never deletes anything:** an account over the
    limit keeps every character and simply cannot make another.
  - **A lifetime row ceiling of 60, separate from the slot count**, and necessary *because*
    deletion is soft: create-and-delete-and-create writes a new row every time.
  - **Slots are NOT contiguous and are NOT an identity.** Retiring the character in slot 1 of
    three leaves 0 and 2 occupied. The panel draws by walking slots and looking each up in the
    roster, never by walking the roster — the other way silently renumbers. Anything keyed on
    a slot number is a bug waiting for a deletion.
  - **`SelectSlot` refuses an empty slot** rather than falling back to slot 0. "You asked for
    a character that is not there, so here is a different one" is how somebody ends up playing,
    and then saving over, a character they did not choose.
  - **Why the selector was off, fixed at the cause.** The note that replaced those lines said
    the panel would say *"signing in… forever against a connection nobody opened"* — true,
    because nothing on that screen asked the server who the account was. A **CHARACTERS** menu
    entry now opens the connection deliberately and polls until the roster lands, and the panel
    is built only once the answer is here. It either shows the roster or is not on screen.
  - Roster accessors bounds-check, and `GetRosterSlot` answers **-1** for a bad index rather
    than 0 — zero is a real slot, and a caller reading it as one would offer to delete the
    character in slot 0 when asked about a row that is not there.

- **Built on the branch, NOT shipped (`a0346ce`).** Cam's instruction was to build and not
  ship. On `feat/world-state` only: no release, and deliberately not pushed to `main`, since
  `main` is the deploy.
  - **A character lifecycle STATE, replacing `m_characterLive`.** That boolean was standing
    in for five states, and the missing one is what cost a character: at the moment of the
    overwrite Cam had a live connection, a valid record, **and a body in the world that was
    neither**. `Connected → Selected → Restoring → Live → Detached`, where `Live` is the only
    state in which writing to the stored record is legal. Every transition logs; `Live →
    Detached` logs as a **warning**, so that session would have read
    `Live -> Detached (world detach)` seventy seconds before the save. A refused save now
    names the state it refused in.
  - **The wire's presence width is now said out loud.** netpack emits `kXPresenceBits` beside
    every generated struct (39 of them). The bitfield is sized to a message's optional field
    count and a reader expecting a different width misreads every field after it — none of
    which is visible in the `.proto`. Not a guard (the protocol identifier already refuses a
    mismatched client): a **name**, so a diff of the generated header shows `4 -> 6` rather
    than showing nothing.
  - **CORRECTION it caught immediately:** `SpawnCharacterResponse` reads **4** presence bits,
    not 6. I derived it by hand once while my own two extra fields were still applied, read 6,
    and repeated that as the baseline in a commit message, a design doc and an artifact. The
    conclusion never changed; the number was wrong three times. That is the whole argument for
    naming a value rather than re-deriving it.
  - **NOT built, deliberately: "placement is not a teleport".** Its own entry in
    `docs/CHARACTER-LIFECYCLE.md` says it is an experiment and that nobody should fix it
    speculatively. Building it because it appeared on a list would be exactly that. It stays a
    question: does 2.31 offer a placement path that preloads streaming, and are we skipping it
    with `TeleportLocalPlayer`?

- **The character selector is OFF, and two requested features are blocked behind it.**
  `MainMenu.reds:331-342` disables the trash can and the panel, because both only make sense
  once the menu has asked the server who this account is — and it no longer does. Consequence
  worth knowing before anyone re-plans this:
  - **Delete a character is BUILT.** `DeleteCharacter()` native, the
    `OnMultiplayerDeleteCharacter` handler, and the confirm flow all exist and still compile.
    **Uncommenting `MainMenu.reds:338-340` is all it takes to bring the entry back.**
  - **Four character slots for admins (requested, NOT built)** needs the selector alive
    first. The plumbing is already there: `AuthenticationResponse` carries a *list* of
    `CharacterSummary` (deliberately a list, "a list of length one costs nothing today and is
    the whole difference later"), and `CharacterRecord` already has `Slot` and `CharacterId`.
    What is missing is the client panel that draws four slots and says which are in use.
- **"I am not the character I made" — ROOT CAUSE FOUND 2026-09-02. TWO bugs, one fixed.**
  Cam: *"the character we created would not be the character we play as, it is also not
  phantom veronica"* — a third person entirely. That is two independent faults stacking:
  - **(A) `OwnSave` has no idea which save is the character. STILL OPEN — this is the big
    one.** It loads "the newest save that is not `MultiplayerStart`", which is not an
    identity, it is an accident of file order. On 2026-09-01 it loaded `AutoSave-12` — a
    throwaway female Corpo from a probe run two days earlier. Any newer save wins: another
    test character, a singleplayer session, anything. Fixing it needs a save NAMED for the
    character (`ManualSave(saveName: String)` exists on `inkISystemRequestsHandler`, so the
    mod can name its own saves) and a load that matches that name rather than a position in
    a list. That is ChatGPT's Phase 1 items 1-4 and it is the correct next piece of work.
  - **(B) A failed appearance restore destroyed its own input. FIXED.** See below.
- **Appearance restore: the bytes were spent before the attempt. Fixed, NOT VERIFIED LIVE.**
  On a fresh connect the server sends the appearance while the player is still on the MAIN
  MENU, because `MpLoadOwnCharacterSave` has not loaded a save yet. From the 2026-09-01 log,
  in order: `server sent 9206 bytes` → `restore BEGIN` → `FAILED: GetState returned nothing`
  → **then** `[OwnSave] loading 'AutoSave-12'`. The old code did
  `const auto bytes = std::move(m_restoreAppearance)` BEFORE trying, so that doomed first
  attempt consumed the only copy; the world then attached with nothing left to apply and
  the player looked like whatever save had loaded. Now the code asks whether a live
  customization state exists BEFORE spending the bytes, keeps them when it does not, and
  clears them only after a commit is accepted. Cheap to check: the log should say
  `appearance held - no live customization state yet` once, then a real BEGIN after the
  world attaches.
- **Older notes on the same area, still true:**
  - **The restore runs on characters it promised to skip.** On a brand-new character the
    log says `NEW character - possessions discarded, restore will run empty` and then
    immediately `Deserialize ccstate COMPLETE - body is female`. It is applying the stored
    blob over the character the player just made.
  - **The stored blob is the contaminated one** — 9141 bytes, female — and it is
    re-uploaded on every autosave and every disconnect (`sent 9141 bytes of appearance to
    the server`), so the bad record keeps refreshing itself.
  - **The commit is inert, which is why nobody is visibly Veronica.** `ReFinalizeState`
    and `FinalizeState` are refused in gameplay; `InitializeOptionsFromFinalizedState` is
    accepted and does not rebuild the body. So the restore "succeeds" and changes nothing —
    it is noise that also gates saving (`MaySaveCharacter` refuses on a FAILED restore).
  - Cam's report from the field: *"im not the character i just made, im some man but not my
    male character."* Do not re-attempt the creator-only commit functions
    (`InitializeState` / `ReFinalizeState` / `FinalizeState`) — all three are measured and
    refused.
- **The world-template plan (Cam, 2026-08-28), not started.** Phantom Veronica should
  propagate WORLD state and nothing else: doors she opened stay open for everyone (excluding
  housing and vehicles), quests she finished count as finished for everyone, then quests off
  entirely — all gated on Dogtown being open. Mechanism already exists: `WorldFact` on
  `SpawnCharacterResponse.facts`. `06af8b5` (open Dogtown on a fresh deployment) and
  `51756fc` (stop quest calls at `PhoneSystem`) are the first pieces of this.

- **Dogtown is SOLVED — stop looking for a quest.** The gate is the quest fact
  `ep1_side_content >= 1`, a pause condition in
  `ep1\openworld\combat_zone_gate\combat_zone_gate.questphase`, which references **no q301
  fact at all**. Confirmed in game 2026-08-29: Cam drove *and* walked through the border on
  a character with `q301_done=0`. A live server takes `/fact ep1_side_content 1` once;
  `06af8b5` seeds it for fresh deployments. Do NOT complete q301 to open Dogtown.
- **The forced Songbird prologue is a CHARACTER-START problem, not a phone problem.**
  `MainMenu.reds` fires `SpawnEvent(n"OnNewGame")`; `preGameScenarios.script:308` routes to
  the EP1 branch whenever Phantom Liberty is installed, so **every character the mod has
  ever created is a PL standalone start** (level 15, `ep1_standalone=1`, `q301_active=1`).
  `51756fc` genuinely stops every phone call — zero presented in a full session — and the
  conversation still happened, because it is a scene the quest drives. Suppression cannot
  stop a running quest; prevention is the only route.
- **Clean multiplayer start: SHIPPED 2026-08-30 (`c1518c4`), needs field testing.** A game
  start is a `.gamedef` — root quests + spawn tag + world, and the game ships 824 of them.
  `ep1\quest\ep1_standalone.gamedef` lists **three independent** root quests, only one of
  which is the story. `code\assets\Archives\packed\...\zz_NightCityOnline_CleanStart.archive`
  (8 KB, a second archive; ArchiveXL is handed the whole directory) overrides it with:
  `cyberpunk2077_ep1_standalone.quest` (base Night City, prologue skipped) +
  **`ep1.quest`** (CDPR's non-standalone EP1 root: sets `ep1_installed=1`, enters
  `ep1.questphase` at `Base`, no story) + `ep1_preorder.quest`. Nothing authored.
  - **Proven before shipping:** a fresh character came up with an **empty quest log** and
    **not one EP1 fact** (`ep1_standalone=0 ep1_active=0 q301_active=0` against 1/1/1 in the
    control), and Songbird never called. Save `ManualSave-132` had a completely empty fact
    list.
  - **CONFIRMED WORKING 2026-08-30, shipped build, Cam's machine.** World on, story off,
    together. The fact signature at spawn:
    `ep1_installed=1` (only `ep1.quest` sets it — proof the EP1 world entry took),
    `ep1_standalone=0`, `q301_active=0`, `q301_done=0`. No Songbird, empty quest log,
    and **Dogtown still reachable**. This is the goal state from Cam's brief — *"we should
    just spawn in the world, no opener, no songbird dialogue, no quests, nothing"* — reached
    without completing, skipping or failing a single quest.
  - **Level 15 came from the story root.** Clean-start characters are level 1 with no base
    prologue quests marked done. The server will have to grant progression; it already
    grants the starter kit and eddies.
  - **A spawn tag is content owned by its quest.** The first probe kept the stock
    `#q301_spwn_ep1standalone_opener` while deleting the quest that defines it, and players
    **fell through the map**. Now `#q000_spwn_start` — the base game's own, which is a
    holding room rather than a place; the server moves new arrivals on connect
    (`/setstart`). Every shipped gamedef pairs its spawn with its own quest.
  - **HAZARD:** `Ship.ps1:574` copies `distrib\launcher\mod\assets` wholesale and never
    cleans it. Anything left in `assets\Archives\` ships to every player — keep probes and
    experiments out of `distrib`.

- **Manifest signing: key MINTED, deliberately PAUSED (2026-08-30). Read this before
  touching it.** Cam's ed25519 keypair exists. The public half is
  `ed25519-public:l9q5uBPf2IRZr1wyVzRCDIvF6LQdMl9r86VQUpyx89c=:38d98b61`. The secret is
  parked at `~/.nco-manifest-key.paused` — moved OFF the path `Ship.ps1` reads, on purpose.
  - **Why it is parked and not live.** `Ship.ps1:908` refuses to publish a manifest its own
    pins cannot verify, but that checks `main.js` in the REPO, not the launcher a player is
    running. A manifest signed by a key an installed launcher has never seen does not read
    as "unsigned" — it reads as a **BAD SIGNATURE**, which "refuses Ready and never falls
    back" (`main.js:867`). Signing before the pin has been delivered locks every player out
    of the game.
  - **The trap if you only half-resume.** Renaming the key back WITHOUT pinning it in
    `MANIFEST_PUBKEYS` does not just fail to sign — it makes `Ship.ps1` die on the verify
    step and **blocks every future ship**. Key present + pin absent is the one combination
    that is worse than either alone.
  - **The safe sequence, when there is budget to watch it:** rename the key back → add the
    public line to `MANIFEST_PUBKEYS` in `main.js` → ship the LAUNCHER carrying that pin
    while still unsigned (point `NCO_MANIFEST_KEY_FILE` at a path that does not exist) →
    wait for players to update → only then let a ship sign. Two releases, and the gap
    between them is the point.
  - Paused at Cam's call: *"i dont have enough tokens to fully fix anything until wednesday
    just in case we break anything doing this."* zeldfep's key (`882c415a`) is already
    pinned and unaffected.
- **The version number no longer identifies a build.** `Ship.ps1 -Mod` does NOT cut a new
  version — it republishes mod assets into whatever release is already `latest`. **Three
  different `ModPayload.zip` builds now exist under the tag `v0.3.113`** (28 Aug, and two on
  30 Aug). Players still update correctly because the launcher compares the **asset id**,
  not the version string, but `.nco-version` reads the same number for three builds, which
  will mislead the first bug report that quotes it. Cutting a real `v0.3.114` needs a
  `## What changed - v0.3.114` section in `publish\release-notes.md` and a FULL ship
  (which also republishes the 103 MB installer). Held on the same 2026-08-30 budget call.

- **NOBODY COULD SEE ANYBODY, and it was never the puppet system. Fixed `ec2858d`,
  awaiting a live run.** Stop looking at `PuppetDriver`, remote animation, or the
  appearance path — none of them ever ran. `NetworkWorldSystem::Spawn` was **never called
  once** in twenty-one session logs: no `[Spawn]` line of any kind, not even the "queued"
  or "CreatePuppet failed" branches.
  - **What was happening.** Since `f66a40a` (2026-08-24) the client re-derives each load's
    cell from its position and drops it when its answer differs from the server's —
    `[World] dropped map-invalid character load …`. That rejected EVERY character and
    vehicle load for six days. Movement and appearance are separate messages so they kept
    arriving, which is why the player list showed people, `/tp` moved them server-side, and
    the client logged `movement for id … but no puppet is registered under it`. It looked
    exactly like a spawn bug and was not one.
  - **Two faults, both server-side.** The grid was declared twice — `Level.cpp` bins by
    `sCellSize = 6000`, `GameServer.cpp` advertised `60000`, ten times out. And `ToCell`
    used a float-to-int cast (truncates toward zero) while the client uses `std::floor`;
    those agree only for positive coordinates and Night City is mostly negative. At
    `x = -1769` the server sent cell `0` and the client expected `-1`.
  - **Fix is server-side ONLY** — no client rebuild, no player update. `kCellSize`,
    `kLoadRadius`, `kUnloadRadius` now live on `Level` as the contract they are;
    `GameServer` advertises those constants; `ToCell` floors. **Never put a literal back in
    either place** — the client is entitled to compute what the server computes.
  - **Verify with:** `[Spawn] remote id … - ccstate N bytes` appearing and
    `dropped map-invalid` stopping. If the drops stop and players are still invisible, that
    is a different bug — the puppet spawn itself — and the `[Spawn]` line will name it.
    Production deploys from this branch but the cron defers while anyone is online.

**Landed since the last pass, so nobody re-opens them:** `/rename` for admins is IMPLEMENTED
(`ChatSystem.cpp:1513`, `kAdmin`-gated, keyed on the character id, and deliberately does NOT
clear `NameChosen` — it repairs a name rather than handing out a fresh naming attempt).
Difficulty is pinned to Very Hard for every connected player (`d5d506f`, `77971ee`,
`Difficulty.reds`, which reads the index by name rather than hardcoding 3).

### Standing decrees (law, not open items - violating one is a bug by definition)
- **Boot policy** (2026-08-21): the game boots STRAIGHT TO THE MENU -
  `-skipStartScreen` + Fast Launch auto-install, both halves stay (main.js).
- **The footprint rule** (2026-08-21): uninstall leaves NOTHING - every write
  location lives in launcherFootprint()/installer.nsh, both layers, always in sync.
- **The helper rule** (2026-08-22): content mods are never load-bearing - "they
  should be there for helping us, not as a variable." No feature depends on a Nexus
  mod, none gates Play/join, none enters the install digest, no load-bearing
  component may depend on one. Load-bearing set: payload + the six MIT prerequisites,
  exactly. Enforced structurally: generator refusals (allowlist + nexus+required +
  edge direction), launcher treats a smuggled manifest as invalid, server disables
  checks rather than load one, CurseForge ClientMods lane retired. Design test for
  every feature: "does it work on a modless install?" (rimtek proved the value).
  Doc: MANIFEST-ARCHITECTURE.md F7.
  **SHIPPING STATUS: the enforcement is NOT in v0.3.107** - it was committed nine
  minutes before that release was published but was not in the tree that built it,
  so it goes out in v0.3.108. Consequence that matters: v0.3.107 launchers still
  read a MISSING `required` field as required, so every modlist entry must keep
  `required:false` until v0.3.108 is the floor. modlist.json says v0.3.108 for
  this reason - do not "correct" it back to .107.

### Needs a live session (built, never validated with humans)
- **Player combat (Cam) — SHIPPED v0.3.104/105/106, never tested with two humans.**
  Stages 1-10 of the brief: PvP damage server-decided, quickhacks on players, hack-warning
  telegraphs, server-owned health/ammo/RAM pool, wanted-level clear on down, scan shows
  real names. Rode along: the v0.3.100 voice-playback fix (communications-device fallback).
  Flag-day was v0.3.105; **v0.3.106 is client-only**, so the live server is correct for it.
  Server logs one `[COMBAT]` and one `[QUICKHACK]` line per validated event - a two-client
  disagreement is then a specific number, not a feeling.
  **The engine-level blocker is solved** (see the combat row in the code map).
  Genuinely open, in order:
  1. **Quickhack id is sent as 0 - FIXED IN CODE, needs one live confirm.** The fear
     ("weak handle whose type varies") was answered from the vanilla sources instead of
     a runtime dump: gameplayRoleComponent.script declares `var action :
     ScriptableDeviceAction` (strong, concrete), and vanilla ScriptedPuppet calls
     GetObjectActionID() on it in exactly our filtered context. MpUploadQuickhackId now
     returns TDBID.ToNumber of that id (zero stays the no-action fallback). RESIDUAL
     RISK: the server table lists QuickHack.Base* record ids - if the wire delivers
     LEVELED variants instead, hacks still refuse as unknown; the refusal log prints the
     raw id, and `tools/hackid.py <id>` turns it back into a name offline (same
     CRC32+length encoding as QuickhackComponent.h, anchored to its static_assert), so
     one live session closes it either way. **COMPILE-CHECKED 2026-08-22 on Cam's 2.31
     install** (`tools\CheckScripts.ps1`, 40 .reds staged, Combat.reds hash-matched to the
     repo copy): `OK - redscript compiles`, so `GetObjectActionID()` resolves on the real
     game scripts and the abort-everything risk is closed. What is still unproven is the
     VALUE on the wire - Base* vs LEVELED record ids - which only a live hack answers.
  2. **Cooldown**: `ObjectAction_Record.Cooldown()` → `Cooldown_Record.Duration()` and
     `.Modifiable()`. If Modifiable is false, use Duration and delete `MinIntervalMs`,
     which is ONLY an anti-spam floor and is labelled as such - do not tune it to imitate
     a cooldown. Solo-answerable: the CET mod at
     `bin/x64/plugins/cyber_engine_tweaks/mods/nco_hackdump` dumps it ~15s after load.
  3. **Hit zone vocabulary**: zones travel as the native `hitShapeName` CName hash, no
     invented enum. The names live in a `gameHitShapeBVH` the .ent only references, so
     they cannot be read statically - every hit logs `[HitShape] '<name>'`. Solo: shoot
     any NPC.
  4. Two-client validation of the whole thing. Nothing has crossed between two machines.
- **Vehicle damage: the native path is PROVEN, the probe is written, nothing has been run.**
  Read from the game's own sources at `<game>\tools\redmod\scripts` - every claim below has
  a file and line, none of it is inferred. Vehicle health is a stat pool:
  `gamedataStatPoolType.Health` on the vehicle's own EntityID, owned by StatPoolsSystem.
  Read `GetStatPoolValue(id, Health, false)`, set `RequestSettingStatPoolValue(...)`, observe
  `RequestRegisteringListener(id, Health, listener)` → `OnStatPoolValueChanged(old, new,
  percToPoints)` (vehicleComponent.script :79, :4543, :4545, :6304). Damage arrives as
  `gameHitEvent` and **`VehicleObject` already overrides `DamagePipelineFinalized`**
  (vehicles.script:1123) - the game announcing it finished calculating. Attacker is
  `evt.attackData.GetInstigator()`, proven in use at vehicles.script:1137; weapon is
  `attackData.GetWeapon()` → WeaponObject → `GetWeaponRecord()` (attackData.script:219 -
  GetWeaponRecord is NOT on AttackData, which cost a compile); amount is
  `evt.attackComputed.GetTotalAttackValue(Health)`, the same call Combat.reds:97 already
  uses for players. Destruction: `gameVehicleDestructionEvent` (hitEvents.script:51),
  destroyed = health custom limit forced to 0.0, stages via `EvaluateDamageLevel` →
  `m_damageLevel` 0-3. **No CET, no WolvenKit, no native work needed for the core loop.**
  Design consequence: bullets, explosions, collisions and environment ALL converge on the
  same Health pool, so one listener catches every source - collision damage needs no special
  case to synchronise, only to assign blame.
  **THE OPEN QUESTION IS ANSWERED - 2026-08-26, measured on Cam's install. The reported
  value is EXACT and the server should validate against it, never recompute it.** One
  shotgun blast at a parked street car:
  - **14 hits inside ONE millisecond** (18:32:10.997-.998), each reporting ~8 damage
  - health 1050.319946 -> 932.885620, so **spent 117.434326**
  - **sum of the 14 reported values = 117.434414** - agreement to 0.000088, float noise
  So `evt.attackComputed.GetTotalAttackValue(Health)` is trustworthy. Also confirmed live:
  the attacker resolves (`by PLAYER`), a street car has ~1050 HP, and
  `DamagePipelineFinalized` fires BEFORE the pool settles - all 14 read the same "health
  before", and the probe's 0.15s delayed readback is what caught the settled value.
  **Protocol consequence, and it is a real design input:** damage arrives PER HIT, not per
  shot. One trigger pull can be 14 events in a millisecond, so the wire format must batch
  or tolerate bursts - 14 packets per shotgun blast is not acceptable.
  **Trap recorded because it nearly produced a wrong conclusion from correct data:** the
  probe's first verdict compared ONE hit against the WHOLE burst's delta and printed
  MISMATCH fourteen times. The data was perfect; the comparison was wrong. A single hit is
  expected to be far smaller than the total. Verdict logic since corrected to say BURST.
  Still to answer, and it needs two humans: does a REMOTE vehicle (kinematic, physics
  suppressed by MakeRemoteDriven) receive the damage pipeline at all, and on whose machine?
  That decides whether the shooter or the owner reports damage.
  Second unknown the probe also settles: whether a REMOTE vehicle (kinematic, physics
  suppressed by MakeRemoteDriven) receives the damage pipeline at all, which decides whether
  the shooter or the owner reports it.
- **Late-join vehicles** (885f252): join while someone drives → you must see the car,
  and parked cars. **test.14 is DEAD** (2026-08-23, zeldfep hit both failure modes):
  its protocol predates the combat flag-day while the test server runs e15f6f5, AND
  installing it over a v0.3.106 install orphaned the combat-era .reds against its older
  DLL - redscript aborted the whole compilation naming Combat/Hackable/Scanner. The
  launcher extract now clears shipped dirs first (extractPayloadClean), so the orphan
  class is closed for the NEXT build. **test.15 is CUT** (2026-08-23, zeldfep's go):
  its ModPayload is byte-identical to v0.3.107's (quickhack fix aboard, superset =
  safe over .107 even on merge-extract launchers), protocol verified identical to the
  e15f6f5 test server (zero proto/netpack commits since). Checklist rides the release
  notes: late-join vehicles, quickhack live confirm, denial popups, -sync-trace drives.
- **Milestone-1 handshake**: SERVER SIDE VALIDATED LIVE 2026-08-23 - a stale client
  (voice-era protocol) was refused five times with the correct identifiers in the log
  and a clean kRefused close. Still open: the in-game denial POPUP (the refused
  client was scriptless, so the redscript popup could not render - needs a stale but
  script-intact client), MaxPlayer (5th player refused), password check. Also
  validated live the same night: the /character new confirm guard (a bare command
  arrived and retired nothing).
- **Download queue + install lock**: Install-missing shows the queue, strictly one at a
  time; clicking Add-mod mid-queue waits its turn; the INSTALLING strip never shows two
  installs braided.
- **Nexus manager**: YOUR MODS section, Add-mod by link/number, Update lands on the pin.
- **zeldfep's nxm:// hand-off**: broken only on his PC, works for everyone else. v0.3.103
  writes the whole story to his trail — after his next Mod Manager Download click, read
  `logs/clients/zeldfep/launcher-trail.log` on the NAS: no arrival line = browser/registry
  on his machine (browser protocol-block most likely); arrival + failure = the reason is
  in the line.

### Vehicle architecture (audited 2026-08-26 — read before touching the vehicle system)
- **The live vehicle and the persistent record used to be strangers.** `VehicleComponent`
  held only TweakDBID - the MODEL - so a spawned car knew it was "a Hella" and never "YOUR
  Hella", and `Level.cpp` referenced the persistence layer zero times. Garages, damage that
  persists, theft, cargo and recovery were each blocked on that one missing link, not on the
  features. **FIXED c13231a:** `VehicleComponent.RecordId` is bound at spawn. Built clean,
  NOT yet live-tested - summon an owned car and check the spawn log names a record.
  Trap for anyone extending it: match on `ModelName`, never on `VehicleRecord::Model` -
  /givecar sets Model to `std::hash<std::string>` of the record name, which is not a
  TweakDBID and never equals what the client sends, so binding on it compiles, runs, and
  matches nothing forever. The id is recomputed from ModelName with `TweakDBIDFromName`.
- **Parentage is AUTHORITY, not ownership, and that is deliberate.** `child_of(player)`
  answers "whose machine simulates this", `HandleMoveEntityRequest` refuses movement from
  anyone but the parent's connection, and `TransferAuthority` re-parents plus bumps an epoch
  so late packets from the old simulator are dropped (revoke before assign, so the failure
  mode is a brief freeze rather than two simulators fighting). Keep that mechanism. What it
  should stop doing is doubling as the vehicle's identity and lifetime.
- **Already true, do not "fix" it again:** `ReleaseVehicleIfEmpty` PARKS rather than
  destroys - the destruction it used to do was a workaround for a different bug (entering a
  car spawned a fresh entity every time; seven copies once stacked in one road), and that is
  prevented at the other end now.
- **Persisted today:** id, owner, model, ModelName, plate, price, and a SALE lock (not a
  door lock). **Not persisted:** position, rotation, health, damage, driver, garage/stored
  state, destroyed state, customization.
- **Honest limit on the whole system:** the server is authoritative over identity and
  permission, not physics. It relays positions and sanity-checks them; it does not simulate
  vehicles and realistically cannot. Anywhere a brief says "server-authoritative physics",
  what is achievable is server-authoritative STATE with validated client motion.
- **Design call nobody has made:** the phone lists MODELS and cannot list instances, so
  "summon my second Quadra" has no native expression. The server must decide which instance
  a model-summon resolves to - nearest stored, last driven, or explicit via /garage.

### Known bugs, diagnosed, unfixed
- **THE crash: SOLVED 2026-08-26/27 - it was never entity readiness. It was a genuine
  data race: two threads inside flecs' `flecs_stack_restore_cursor` on the same stack
  allocator at the same instant** (`0da3c9b`, `559828f`, `0fa2bb9`). flecs's stack
  allocator is deliberately unlocked - documented as per-stage, single-threaded - and the
  game's own job system was calling into our flecs world off the main thread from THREE
  places: the combat hit hook (every bullet), `VehicleSystem::OnVehicleEnter`, and
  `OnVehicleReady`. All three were wrongly assumed main-thread "because they are RTTI
  methods called from redscript" - a caller being redscript says nothing about which
  thread the engine's job system dispatches it on, and that wrong assumption cost a full
  day before someone measured instead of read. Fixed by removing flecs from every
  off-thread path (a lock-free `ServerIdRegistry`, modelled on `PuppetRegistry`) rather
  than locking the allocator - locking it would have cost frame rate on the hit hook,
  which fires for every bullet in Night City. A second, unrelated structural bug
  (`98b12fa`) was fixed alongside it: an `OnAdd` observer that added a component during
  its own dispatch, which flecs's own source warns against.
  **Confirms the vehicle-mount crash and the weapon-fire crash were the SAME bug** - the
  v0.3.112 release notes say so directly ("Same cause behind the crash when getting on a
  bike or into a car"), and `0fa2bb9`'s follow-up session recorded ZERO 0xC0000005 crashes
  after the fix, on a session that used to reliably produce them within seconds.
  **The entity-readiness theory in `docs/CRASH-FIX-BRIEF.md` and the female-appearance
  correlation below were both red herrings** - real, honestly-reported dead ends, not
  wrong observations, but not the cause. Do not resume that brief's Tier 1/Tier 3 work;
  it was chasing the wrong mechanism. Treat CRASH-FIX-BRIEF.md as historical from here.
  **STILL OPEN, and it is a DESIGN question now, not a crash:** A disconnects while
  driving, B stays online, C joins later - the car legitimately survives (parking on
  zero-players doesn't cover it) and hands C a car with no valid driver. Whether this
  still misbehaves post-race-fix is UNMEASURED; worth one session confirming it merely
  looks odd now rather than crashing, before spending time on it as a crash.
  Historical shape of the investigation, kept for anyone re-deriving it: dies within
  seconds of `[Interpolation] movement for id N but no puppet is registered` ->
  `HandleVehicleEnterMessage: queueing mount` -> `OnVehicleReady` -> `DoMount` /
  `MakeRemoteDriven: SetKinematic ... done`. Reproduced on demand 2026-08-26 via the
  mundane trigger (disconnect while driving, then rejoin - the car survives in the live
  flecs world with no persistence trail, gets replayed to the rejoiner, dead 1.6s after
  `MakeRemoteDriven` finishes). A parallel, separately-chased correlation (never proven,
  now understood to be the same race): remote APPEARANCE apply (`AddItemToSlot failed`
  x106/evening for one player), matching a female-character bias that turned out to be
  coincidental exposure to the race rather than a female-specific code path. Two things
  landed from that investigation and remain true and useful regardless of the real cause:
  `Level::ClearAbandonedVehicles` clears every vehicle server-side when the player count
  hits zero (`0e00c77`), and the CRASH-FIX-BRIEF.md fixes I (Copilot) actually implemented
  2026-08-24 - the AppearanceSystem span-lifetime UB and the CMPReader silent-corruption
  fix - were real bugs worth having fixed even though they were not THE crash.
- **EACCES shell-fallback destroys crash telemetry: FIXED** (`681e3af`, 2026-08-24/29,
  Copilot). The shell wrapper's own near-instant exit (always code 0) is no longer logged
  through the same path that writes "game exited with code N" - it has its own distinct
  log line, and the real game process is still tracked separately by `watchForGameExit()`.
- **Ghost remotes on join: SOLVED (2026-08-23) - they are /npc entries with garbage
  records.** The test server's npcs.json holds 7 NPCs; FIVE carry the literal string
  `Character.<record>` (the usage line's placeholder, typed verbatim and persisted),
  plus a made-up `Character.Steve_urgelles` and a bare `Character.Panam`. Count
  matches the client logs exactly: six bail `entity id is not dynamic`, the seventh
  (entity 9e03d1, 0-byte appearance) half-spawns and got an interpolation controller
  attach SECONDS before Cam's solo 0xC0000005 - garbage NPCs are now suspect #1 in
  the test-server flavor of THE crash. Live has NO npcs.json, so live ghosts (if the
  sweep's were live) have another source. Immediate cure: `/npc clear` (admin, in
  game). Code fix: /npc now REFUSES records containing `<`/`>` (placeholder paste),
  not just warns (`681e3af`, 2026-08-24/29) - the base-game-records warning added
  2026-08-23 only ever described the problem, this actually stops it.
- **Parked persisted vehicles + MakeRemoteDriven: DISAGREEMENT, unresolved, needs a
  human call, not another edit.** This entry originally claimed a parked replayed car
  must NOT be made kinematic and that MakeRemoteDriven belongs to occupied vehicles
  only. Current `VehicleSystem.cpp::OnVehicleReady` argues the opposite on purpose, in
  its own comment: physics-off (`MakeRemoteDriven`) runs for EVERY network-spawned
  vehicle unconditionally, occupied or not, specifically so a parked copy can never
  simulate physics against the local world's own copy of the same car. I (Copilot,
  2026-08-24) read that comment and did not touch this, because the two positions
  contradict each other and I could not tell which one was reasoning about a live bug
  versus which one was the fix already applied. Whoever picks this up: check `git log`
  on that function before changing it either direction - one of these two claims is
  stale.
- **Mod 22114 (70 files, still unnamed) verify-flip churn**: reinstalled 4x with
  IDENTICAL archive hashes yet verification keeps flipping back to broken on Cam's
  machine - something on disk rewrites or removes its files after install. Identify the
  mod first (tools/hackid.py will not help - it is a Nexus id; check the page when
  Cloudflare allows, or Cam names it), then diff which recorded files fail.
- **Death-respawn loop for creator-flow players** (ashencorridor 17, rimtek 59 respawns).
  Mechanism: immortality blocks death but not damage; the health floor fires "downed",
  revive teleports to the respawn point, where a naked level-1 keeps getting farmed →
  ~20-30s loop. Only hits fresh characters from the new creator; leveled admins resume
  elsewhere and fight back. Fix direction (offered, not built): a few seconds of DAMAGE
  immunity post-revive (gameGodModeType.Invulnerable window) and/or full-health revive;
  operational relief: `/setspawn` somewhere safe. Owner: whoever grabs it first — it
  spans Cam's creator flow and the Death.reds layers.
- **Character face/body loads from the LOCAL save, not the server: ACTIVELY BEING FIXED
  BY CAM, 2026-08-28, not the stale "known/unfixed" state v0.3.113's notes still describe.**
  Real progress since that release, in order: `54d9a85` wired the server to send the owner
  their OWN stored appearance (previously it only ever went to everyone else) and the
  client to attempt applying it locally. `78713d7` found `InitializeState` was never even
  reached - every customization method lives on `gameuiICharacterCustomizationSystem`, the
  INTERFACE, not the concrete class the game hands back (which has an empty function
  list) - fixed the dispatch to resolve against the interface explicitly. `e795a52`
  (latest) overturned the earlier conclusion that a live customization state requires the
  creator: `equipmentSystem.script:4992` calls `GetState()` during ordinary gameplay, so
  `GetLiveCustomizationState()` now resolves the same way instead of requiring
  `InitializeState` at all.
  **STILL UNPROVEN, and it is a pure visual check nothing here can substitute for:**
  whether deserializing into that live state and calling `ReFinalizeState` actually
  rebuilds the body mid-session. Cam's own words: "Phantom Veronica on screen is the only
  test that counts." Needs one live join to answer. Do not restart this investigation from
  scratch - the dispatch bug and the InitializeState/live-state question are BOTH already
  closed; only the visual outcome of the final apply is open.
  **RULED OUT BY CAM 2026-08-30: the mod-spawned-puppet approach is NOT the way.** The
  Phase 1 experiment (`2356232`, `LocalPuppet.reds`, the `-mod-local-puppet` flag and its
  `IsModLocalPuppet*` natives) asked whether a mod-spawned puppet could BECOME the local
  player, as an alternative to changing the vanilla V's body in place. Cam's call: *"we wont
  be doing the mod-spawned puppet thing."* Do not revive it, and do not treat it as the
  fallback when the `GetState` path is tested. The remaining route is the one already
  written - write the stored ccstate into the live customization state and re-finalize.
  The experiment code is now dead weight: it is off unless `-mod-local-puppet` is passed, so
  it harms nothing shipping, but it should be removed rather than left to look like an open
  option. See the "Local Puppet Migration" artifact for the reasoning it was built on.
- **The template's inventory lands on the player. FIX WRITTEN 2026-08-28, UNTESTED
  (d926ca9).** Same disease as the row above - Phantom Veronica supplying identity instead of
  just the world. The strip runs, the kit lands correctly (5 items, 20,000 eddies, no chrome),
  and **~88 seconds later** 124 stacks and 14 cyberware appear; the 90s autosave then writes
  them to the server as the character's real inventory, which is why they survive reconnects.
  **NOT a vanilla engine grant** - a fresh start has none of that, and the counts vary by
  session (409/60, then 124/14) where a fixed grant would not. It is her save data arriving
  late. Fix is `MpStarterSettlement` (Inventory.reds): waits for the stack count to grow past
  the kit, waits for it to stop moving, cleans ONCE, disarms permanently. Detects rather than
  sleeping - 88s is one machine's observation, not a contract.
  **HARD CONSTRAINT - do not "improve" this into a recurring strip.** Cam's rule: *"any new
  weapon, clothing, cyberware, money or any item a person grabs or buys stays on them."* A
  cleanup on a timer, on inventory change, or on reconnect would delete a gun somebody just
  bought. It arms ONLY inside `ShouldEquipRestored()`, true only for a starter kit, true only
  at character creation. NEXT: make a character, watch `settlement: armed` -> `INITIALIZED`,
  then buy something, reconnect, confirm it survived.
- **Money does not persist. NOT STARTED (2026-08-28).** 84 eddies picked up; every subsequent
  capture read exactly `20000`. Nothing decayed - a gain never entered the record. Candidate:
  a second restore ran mid-session (`restore DONE: money 20000 -> 20000`) re-applying the
  server's snapshot, which would also violate the items-must-stay rule above. Instrument every
  boundary before touching it: capture -> send -> record -> save -> reload -> apply.
- **Character overwrite - FIXED 2026-08-28 (fbdff4a), and the fix needed a second fix
  (019a4cc).** Cam lost a character: connected, restored correctly (13 stacks, 20,000 eddies),
  pressed join from the main menu - which detaches the world and rebuilds it from the LOCAL
  save - and 70s later the disconnect save captured the template and sent it as him
  (`409 stack(s), 872 eddies`). `m_restorePending` could not catch it; it only covers the
  window before the FIRST restore. Now `m_characterLive`, set when a restore completes and
  cleared by ANY world detach, gates all four save paths through one rule
  (`MaySaveCharacter()`). **The second fix matters as much:** that guard made the only exit
  save (in `Disconnect()`) refuse on quit-to-desktop and quit-to-menu, which tear the world
  down on the engine's schedule - trading a rare catastrophic loss for a routine small one.
  The save now runs at the TOP of `OnBeforeWorldDetach`, the last instant the body is still
  the server's character. Cam caught this: *"also make sure the game saves when you quit."*
- **The invisible chat box - SOLVED 2026-08-28 (96da4bf, shipped v0.3.113).** Never a
  visibility problem. A native parent-chain walk (`LogWidgetAncestry`, added because
  `inkWidget` in 2.31 has `Reparent` and **no `GetParentWidget`**, so script can only walk
  DOWN) showed every ancestor visible at opacity 1 - and the `hud` canvas at `pos=(0,0)`,
  then `(47,1688)` thirteen seconds later, right after an `OnVehicleEnter`. `(0,0)` is how the
  asset authors it, and the ONLY things that ever moved it were the `to_vehicle` /
  `from_vehicle` animations. Nothing played one at startup, so the whole custom HUD drew in
  the top-left corner until the player got into a car. A `from_vehicle` now plays when the
  connection comes up. **Not cosmetic:** the name prompt fires at spawn into a box that was
  not on screen, and a name is chosen ONCE - that is where "JulianJulian Vale" came from, and
  why Cam has asked for an admin `/rename` keyed on `CharacterId`, not the Discord account.
  Eliminated with evidence so nobody re-treads it: the widget matches its authored state
  exactly, and `multiplayer_ui` in `prototype_hud.inkhud` carries the MOST permissive context
  list of all 70 entries and is otherwise field-for-field identical to `new_phone`. Decoded by
  building the WolvenKit CLI from source (`dotnet build WolvenKit.CLI`, then
  `convert serialize`) - worth knowing that route exists.
- **Exit-grace 250m pop**: the 4s vehicle-exit grace freezes the puppet's interpolation
  target while the real player keeps moving → teleport-pop when grace ends
  (`[MultiMovement] delta runaway (250m)`, live). Fix: keep updating the target during
  grace, only withhold the controller attach. (VehicleSystem.cpp / InterpolationSystem.)
- **`AddItemToSlot failed`** on remote puppets' equipment (cosmetic: some clothing
  missing on remotes; possibly items the viewer lacks). Not investigated.
- **rimtek-class ping (170ms) vs the fixed 100ms interpolation budget** — far players
  look the choppiest. NOW MEASURABLE: `tools/netlab/` (Python lab; README has the
  charter — Python is the lab, C++ is the ship). First synthetic results (2026-08-22):
  on the rimtek profile today's vehicle algorithm starves 75% of frames with 162
  teleport-pops; the `vehicle_dr` candidate (dead reckoning + projective blend) gets
  0 pops / 0.03% starvation / lower error, and is identical to baseline on clean
  links. `adaptive` delay looks right for players (30ms effective delay on LAN vs
  80). FIXED (2026-08-30): its vehicle numbers were previously nonsense (94.6%
  starvation, 65m mean error) because `_target_delay()` sized its buffer from jitter
  alone, enough slack for players (who extrapolate through a gap) but not vehicles
  (which freeze solid the instant a second sample isn't already queued - see
  `Baseline.render`'s "vehicles never extrapolate" rule). Added a per-kind margin
  (2 periods of look-ahead for vehicles instead of 1) in `strategies.py`; rimtek/vehicle
  now starve%=38.2, err_mean=2.95 (beats baseline's 3.24), correction_m=45 (was 884).
  Still loses to `vehicle_dr`, which remains the right candidate to port - `adaptive`
  was never meant for vehicles, it's just no longer lying about it. NEXT:
  capture real traces — launch a far player's client with `-sync-trace` (dev flag,
  hand-added; writes NDJSON into the mod's logs, ships to the NAS automatically),
  then `replay.py --trace file --validate` to prove the lab's baseline matches the
  shipped C++ before promoting any candidate to InterpolationSystem.cpp.
  Real-roads pipeline is ready: `paths/` banks recorded drives as reusable truth
  (`--save-path` / `--path`), `maps/` + `calibrate.py` draw results over a locally
  extracted city map (image never committed - CDPR asset). A capture-ready client
  build (combat tip + -sync-trace) is STAGED but the test.15 pre-release is
  DELAYED on zeldfep's call (2026-08-22) - cut it when he says go; the test server
  is already rebuilt at e15f6f5 to pair with it.

### Manifest system: built, waiting on two keys and one deploy
- **Cam's signing key**: he runs `node tools/manifest/keygen.cjs`, posts the PUBLIC line
  to the feed; it gets pinned in `MANIFEST_PUBKEYS` (main.js) next to zeldfep's
  (`882c415a`). Until then his -Mod ships warn and go manifest-less (migration state, by
  design).
- **First real manifest ship**: any -Mod ship from a keyed machine publishes
  `server-manifest.json` + `.sig`; launchers start verifying automatically.
- **Server-side arming**: copy the shipped manifest to the server's `config/` dir —
  absent file = checks disabled (migration). Then manifest_version + install_digest
  gates go live at the door.
- **Milestone 2** (docs/MANIFEST-ARCHITECTURE.md §11): transactional installs with
  rollback; launcher self-update signature gate (closes the trust loop); HashProtocol
  hashing dependency CONTENT (until then: a common.proto change is a protocol change BY
  CONVENTION — both sides ship together); developer mod scanner; compatibility matrix
  UI; staging-channel manifests; resumable downloads; clientOnly whitelist then
  `unknownMods: block`.

### Modlist debts
- **Audioware (12001) + RedData (14139): PULLED** after boot-blocking the game
  ("invalid native definitions", live). Mechanism confirmed on Cam's PC 2026-08-22:
  a HALF-REMOVED mod — plugin DLLs gone, `r6/scripts/Audioware|RedData` sources left —
  is exactly that failure (scripts declaring natives whose DLL is absent). His fix:
  delete those two script folders; `NoIntroVideos.reds` stays (Fast Launch, no
  natives). The launcher now refuses that half-state: Remove is blocked while the
  game runs, a partial deletion keeps the record instead of orphaning survivors, and
  emptied folders are pruned. Re-add conditions in modlist.json `_pulled`:
  verify their RED4ext/Codeware needs against our pins (RED4ext 1.29.1 / Codeware
  1.18.0) on a TEST install, and encode 12001→14139 in `requires`.
- **Unconfirmed ids**: 22114 (police/prison RP, unnamed). Confirm on Nexus before
  naming. (4198 is CONFIRMED ArchiveXL and pulled — see modlist `_pulled`.)

### Operational debts
- **"Built and pushed" is NOT "deployed" - three surfaces, each of which bit once on
  2026-08-28.** Every time, a correct fix looked broken because the thing under test was not
  the thing that was built, and each cost a full test round-trip with Cam. Verify the artifact
  contains the specific change, **by string, not by timestamp**, before asking anyone to test.
  1. **Redscript does not ship with the build.** The DLL is a symlink into
     `build\windows\x64\release\`, so it is live the instant it links. Redscript is not: the
     plugin's `assets` folder is a JUNCTION to `distrib\launcher\mod\assets`, and
     **`xmake install -o distrib Client` prints "install ok!" and leaves edited `.reds` at
     their previous contents.** Force-copy, then check the deployed file. Never
     mirror/`/PURGE` - `distrib` legitimately carries `World\CharacterProfile.reds` and
     `World\KiroshiScanner.reds`, which are not in `code`. `Ship.ps1` is safe (line 343
     force-copies, 346-348 verify), so releases were never affected - only the manual path.
  2. **The launcher writes THROUGH the symlink.** Installing a release replaces the dev build
     with the release binary, so a fix built minutes earlier silently disappears and the game
     logs behaviour from code no longer in the tree. Rebuild after any launcher install.
  3. **A fix that exists only in git reaches nobody.** The appearance fix (`e795a52`) was
     committed, pushed to main, and verified in the local build - then v0.3.113 shipped
     without it, because the release had already been cut. The launcher can only deliver what
     is in a release.
- **No manifest signing key** (`~/.nco-manifest-key`), so releases ship without
  `server-manifest.json` and players get no manifest verification. NOT new - v0.3.107 through
  v0.3.113 all lack it. `tools\manifest\keygen.cjs` fixes it permanently; it generates a key,
  so it is Cam's to run.
- **Live server runs feat-built code while `main` lags** — Cam deploys the live box by
  hand from feat; the NAS cron tracks main. Either merge feat→main at a stable point or
  keep the cron pointed where deploys actually come from. Divergence is how a "clean"
  push to main could someday roll the live server BACKWARD.
- **Nexus SSO application** still unanswered (publish/nexus-sso-request.md) — manual
  API-key paste remains the sign-in path.
- **Server password has wire+server support but no launcher UI** (settings.serverPassword
  is settable by hand only).
- **Puppet record flip** (mannequin → Character.Jackie/WaPanam story rigs) still waits
  on live validation via `-puppet-record` / `/npc`.
- **reason 4 disconnects** are abrupt game closes, not crashes — cosmetic, but mapping
  GNS close reasons better would stop them reading as failures in every log review.

---

## 2. CODE MAP — where things live, and the gotcha that bites there

| Area | Path | What lives there | Load-bearing gotcha |
|---|---|---|---|
| Protocol | `code/protocol/*.proto` | Wire messages; netpack generates per-package `kIdentifier` (FNV1a64 of the file's own message surface) | Changing client/server.proto = flag-day, self-enforcing. **common.proto content is NOT hashed** — treat as protocol change by convention (M2 fixes) |
| Transport | `code/common/Network/` | GNS server/client, 17-byte handshake, kRefused refusal, snappy framing, drain-before-close | Transport identifier check fires BEFORE auth; old clients see kRefused only if they carry it |
| Server core | `code/server/native/GameServer.cpp` | Auth pipeline in order: protocol → password → manifest_version → install_digest → unmanaged policy → capacity → Discord → ban → capacity again | Discord verify runs on a worker thread; everything world-touching returns via task queue. Digest canonical string must stay byte-identical to manifest.js |
| World | `code/server/native/Game/Level.cpp` | Cells, spawns, vehicle enter/exit, **late-join replay** (characters, then vehicles, then seats) | Replication is observer-based (Components/*.cpp) — observers only reach players PRESENT; anything a late joiner needs must ALSO be in AddPlayer's replay |
| Server config | `<server>/config/` | server.json, bans, players, worldfacts, vehicles, audit.log, **server-manifest.json (optional)** | Malformed bans.json aborts boot on purpose; malformed manifest disables checks loudly |
| Status API | `code/server/loader/Systems/WebApi.cs` | `/api/v1/status/` (Players, Uptime, State, ManifestVersion, Release), client-log intake | Host publishes only UDP; probe the API via the tailscale sidecar's netns |
| Client boot | `code/client/Main.cpp` | RED4ext plugin entry, framework registration, 2.31 warn, BuildInfo stamping | Input Loader missing = hard MessageBox refusal; game version is warn-only |
| Client settings | `code/client/App/Settings.cpp` | ALL launch args (`--key=value` form ONLY — space-separated silently ignored) | The launcher→DLL channel is argv, one-shot; there is no config file |
| Client net | `code/client/App/Network/NetworkService.cpp` | Auth request fill (build_stamp, manifest attest, unmanaged, password), denial capture → NetworkWorldSystem | Denials must be stored BEFORE Close() or the popup has nothing to say |
| Client world | `code/client/App/World/` | NetworkWorldSystem (join/detach/denial natives), VehicleSystem (load/enter queue, exit grace), InterpolationSystem, AppearanceSystem, PuppetRegistry | Every native needs a matching `native func` line in the .reds or ALL scripts fail with UNRESOLVED_METHOD |
| Scripts | `code/assets/redscript/` | MainMenu (join arming), Death.reds (immortality + floor + menu backstop), Combat.reds (hit hook, weapon poll, quickhack requests), Hackable.reds, World/*.reds | redscript is ONE compilation unit — one broken file boots the game with no scripts at all. **No hex literals** (`0xFF...` is a parse error that kills every script). Match integer widths exactly — `GetMagazineAmmoCount` returns Uint32, and mixing it with Int32 is NO_MATCHING_OVERLOAD |
| Combat | `code/server/native/Game/Level.cpp` (handlers) + `Components/{Health,Weapon,Quickhack}Component.h` + `code/assets/redscript/Combat.reds` | Detect → validate → broadcast → apply. Server owns health, magazine, RAM pool | **The game computes, the server bounds.** Weapon damage, quickhack damage and RAM cost all come from the client because they are native calculations needing a live StatsSystem — `GetCost()` runs `CalculateStatModifiers` against the attacker's deck and perks and can include a RANDOM modifier. Quickhack damage MUST stay 0 in the rule table: Cyberpunk applies it through the ordinary hit pipeline, so a number there double-counts (the v0.3.104 bug). A TweakDBID is **CRC32** of the name + length in bits 32-39, not FNV — guarded by a static_assert against a value dumped from the game |
| Making players targetable | `code/assets/Tweaks/CyberpunkMP.tweak` + `Hackable.reds` | `objectActions` on the puppet records; hostile attitude at spawn | **`MaMuppet`/`WaMuppet` inherit from `Character.Panam`, NOT from `Character.Muppet`** — editing Muppet does nothing. Quickhack action names in the game's scripts are WRONG (`BaseBlindHack` not `BlindHack`, `MadnessHackBase` not `MadnessLvl3Hack`) — they were dumped live. Hostile attitude satisfies BOTH gates: `Att_Hostile` for `TSF_EnemyNPC` and the fourth route to `IsAggressive()`. The entity templates were never missing targeting components (16 `gameTargetingComponent`s, confirmed via WolvenKit CLI). Behind `--hackable-puppets` |
| Runtime inspection | `bin/x64/plugins/cyber_engine_tweaks/mods/nco_hackdump` (not in repo) | Dumps TweakDB data the game will not reveal statically | CET only honours `registerForEvent` from `init.lua`; a required module's registration is ignored. Mod globals are NOT reachable from the console — export by returning a table. `io` is sandboxed to the mod folder. **Lua output goes to `scripting.log`**, not `cyber_engine_tweaks.log` |
| World/asset editing | External tool, not in repo: [WolvenKit](https://github.com/WolvenKit/Wolvenkit/releases) | Editor + CLI for the game's own resource formats (`.ent`, `.mesh`, `.app`, world/sector nodes, TweakDB). Already used once to confirm the puppet templates' `gameTargetingComponent`s statically (see the targeting row above) — it is the tool for any future world-building, level-editing, or static-asset-inspection work, not just confirmation checks. **LIVE on the NAS as of 2026-08-29**: `tools/deploy/update-wolvenkit.sh` is wired into `truenas_admin@100.90.85.33`'s crontab (`0 * * * *`, self-throttled to ~72h internally — see the script's own header for why cron frequency and check cadence are deliberately decoupled) and the seed run succeeded — `~/wolvenkit-console/VERSION` reads `8.20.0`, `~/wolvenkit-console/current/` holds the full `WolvenKit.ConsoleLinux` extraction. Deployed by copying the file directly rather than through the tracked git pull, because that checkout is on `feat/world-state` (see the Deploy row below) and does not have this file yet — if `feat/world-state` is ever reset/rebuilt from git, this script and the cron line survive independently of it, but a fresh checkout elsewhere would need both re-applied by hand until this lands on that branch too. **LOCAL CLI, built from source, 2026-08-30 — the route to use on Cam's PC when you need to READ an asset now.** No installed WolvenKit exists on that machine; only `C:\Users\Cam\Downloads\WolvenKit-main.zip` (source, 89MB). Recipe: extract, then `dotnet build WolvenKit.CLI\WolvenKit.CLI.csproj -c Release`. It needs the .NET SDK and **the CLI targets `net10.0`**, so the exe lands at `WolvenKit.CLI\bin\Release\net10.0\WolvenKit.CLI.exe` — Cam's box has SDKs 6/8/9/10, so it builds; a box with only 9 will not. Decode with `convert serialize <file>`, which writes `<file>.json` beside it (`convert deserialize` goes back). JSON gotchas that cost time: entry names are at `hudEntryName.'$value'`, not `.hudEntryName`; a widget library item's tree is at `item.package.Data.File.RootChunk.rootWidget`; and `rootWidget` is often `{"HandleRefId": "N"}` pointing at a `"HandleId": "N"` defined elsewhere in the same package, so you cannot always walk it with plain property access — search the raw text for the id. **What it settled:** decoding `prototype_hud.inkhud` and `multiplayer_ui.inkwidget` proved the invisible-chat-box bug was NOT the asset — `multiplayer_ui` carries the most permissive `gameContextVisibility` of all 70 entries and is field-for-field identical to `new_phone` apart from `ignoreHudScaleOverride`, and the chat canvas matches its authored state exactly (1000x1000, `Fixed`, `Fill/Fill`, opacity 1). I was one step from "fixing" an asset that was never at fault. | **Check the releases page for the version matched to game patch 2.31** before using it on this project — WolvenKit versions track specific game patches, and a mismatched version can misread or corrupt resource formats it does not recognise. Read-only inspection (CLI dumps) is low-risk; anything that WRITES a resource file should be treated the same as an engine version pin — verify against 2.31 first. |
| Launcher | `code/launcher-lite/main.js` | Discord identity (membership: only 200/404 are verdicts), roles (10-min memo), manifest state machine, install lock + queue, Nexus manager, game detect (A–Z drives), footprint/uninstall | **CSS specificity**: base `button.action` (0,1,1) beats bare class rules — trio overrides must be `button.action.x`. Electron packaged: new source files MUST be added to package.json `build.files` (v0.3.97 shipped importing a file it didn't contain). **Uninstall is a two-layer mirror**: footprint in main.js AND `build/installer.nsh` — a new write location goes in BOTH. The `nxm://` class is cleared only when its command points at OUR exe (Vortex/MO2 write the same key; empirically tested both ways 2026-08-22) |
| Manifest kit | `code/launcher-lite/manifest.js` (+ selftest) | Signature verify vs pins, §2.1 availability states, install digest, ownership index, unmanaged classifier, tailnet check | Pure functions, Electron-free; run `node manifest.selftest.mjs` (82 checks) before shipping launcher changes |
| Ship tooling | `tools/Ship.ps1`, `tools/manifest/*.cjs` | Gate battery, staging, carry-forward, manifest generate/sign/verify-vs-pins, prerelease→verify→promote | Ship bumps package.json but never commits — carry the bump or the next ship collides with an existing tag and silently uploads into an old release |
| Deploy | `tools/deploy/update-server.sh` | NAS cron: player-count gate + server-relevant-path filter | **Deploys whatever branch the checkout is ON — production `/mnt/vol/NASa/CyberpunkMP` is on `feat/world-state`, not main** (verified 2026-08-22). The repo dir is the script's first ARG and defaults to `~/CyberpunkMP`, which is not where production lives — calling it without the arg fails with "no such directory". Two traps beyond that: it DEFERS while Players>0 (so a deploy can silently not happen), and it skips the rebuild when no server-relevant path changed — a docs-only commit logs "pulled, nothing changed" and leaves earlier unbuilt server code still unbuilt. Verify a deploy by checking the running binary for a symbol, never by reading the log |
| Coordination | `code/coord-api/`, `publish/assistant-updates.json` | The feed both Claude streams post to; dev-key handout | Personal key `~/.ncoa-coord-key`; posts as "zeldfep (Claude)" |
| Published surface | `publish/` | server.json (address, republished by workflow), modlist.json (curated Nexus list), roles.json (written by server), manifest-source.json (curated components), release-notes.md (EVERY release's body), fullinstall-base/ | All fetched from `releases/latest/download/<name>` — a launcher-only ship must carry mod assets forward or every launcher 404s (v0.3.1 lesson, automated since) |

---

## 3. PIPELINES — how anything reaches anyone

**Launcher/mod → players** (the ship): worktree from origin/main + `code/launcher-lite`
+ `publish/` checked out from feat → sync commit pushed to main AND the temp branch →
real `pnpm install` → `Ship.ps1 -Launcher` (gates: notes-mention-next-version, secrets,
node --check, DOM balance, deps-installed, smoke test) → creates PRERELEASE → uploads
with retry → verifies assets ON GitHub by name → promotes to latest → Discord announce
→ commit bump to main, delete temp branch, carry bump to feat. Mod ships additionally
build/stage the payload, hash-check FullInstall's DLL against the release DLL, and (with
a signing key) generate+sign+verify the manifest.

**Feat → live server**: currently Cam, by hand, from feat. The cron path (main → NAS,
10-min tick, defers while Players>0, rebuilds only when server-relevant paths changed)
still exists and still watches main. See ledger.

**Feat → test server**: manual — `git reset --hard origin/feat/world-state` in
`/mnt/vol/NASa/CyberpunkMP-authority`, `docker compose -p nco-authority build
--build-arg BUILD_JOBS=2` with an IMAGE_READY marker into `~/nco-authority-rebuild.log`,
then `up -d`. Probe: `docker exec nco-authority-tailscale wget -qO-
http://localhost:11778/api/v1/status/`.

**Test builds → devs**: GitHub pre-releases (`vX-worldstate-test.N`), full payload,
invisible to player launchers, installed via Settings → Test builds. One at a time;
delete superseded with `--cleanup-tag`. Protocol identifiers in the pre-release notes.

**Manifest** (once both keys exist): `publish/manifest-source.json` → generator (hashes
staged payload + prerequisite zips, derives install order, REFUSES cycles) → sign
(owner key, never repo/NAS) → verify against the launcher's own pins → release asset →
launcher state machine (valid/rollback/invalid/cached/absent) → launch args attest →
server checks at the door.

**Evidence back from the field**: every launcher POSTs session logs + trail to the
server (`/api/v1/logs/`) → NAS `logs/clients/<player>/`, newest 10 + trail. First stop
for any "it broke on my machine". The coordination feed is where both Claude streams
announce flag-days, ships, pulls, and diagnoses — check it before shipping or deploying.

---

## 4. IDENTITY & VERSION SURFACES (one line each)

- **One project version**: launcher `package.json` → tag `v0.3.NN`; only launcher ships move it; ships don't commit the bump (carry it).
- **Protocol**: the kIdentifier pair, per build; recorded in test-release notes and (once shipping) the manifest.
- **Mod build**: `BUILD_COMMIT` + `NCO_BUILD_VERSION` via BuildInfo.h (dev builds say 0.0.0-dev honestly).
- **Mod-on-disk**: `.nco-version` marker + settings `installedStamp` (assetId:size — the only identity that survives tag-clobbering).
- **Manifest**: `manifestVersion` date.serial, monotonic client-side, THE client-facing environment identity once live.
- **Server**: status API `ManifestVersion`/`Release` (empty = migration).
