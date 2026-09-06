# The Map

**What this is:** the call-back document — the ledger of everything open, in flight, or
deliberately deferred, followed by the geography of the code and the pipelines. When
something feels missed, this is where it should already be written down. Maintained by
whoever lands or finds things (both Claude streams included): landing an item removes it
from the ledger in the same commit; finding one adds it. A ledger that is not updated in
the landing commit is how items get missed twice.

**The shared rulebook is CLAUDE.md at the repo root** - both streams load it (zeldfep's
stream by hand: its cwd is the parent, so it does not auto-load). A rule that matters to
both streams lives there or here, never only in one machine's memory.

**The map convention (crew decree 2026-09-03):** any commit touching this file is
announced on the coordination feed by whoever made it ("map updated: <what>"), and the
other stream re-reads the map before its next move instead of acting on a cached copy.
The NAS deploy announces map-touching pulls automatically as a backstop (posts as
"deploy (NAS)"), so a push alone still signals - but the manual post carries the WHY,
so do not lean on the backstop.

Last full revision: 2026-09-03 - the era catch-up, restyled back to this ledger format.
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

### Standing decrees (law, not open items - violating one is a bug by definition)
- **The server must be portable, and git is how it moves** (Cam, restated 2026-09-04
  ahead of the weekend migration): *"server build should be able to be transferred and
  build should be on git for quick deployment."* A deployment stands up by cloning the
  repo and building — never by copying a built artifact off the old box, and never from
  a step that lives only in somebody's shell history. Two consequences that bite:
  - **Anything the build needs is IN THE REPO.** A hand-seeded file on one machine is a
    deploy that cannot be reproduced — and worse, an untracked file the incoming commits
    are about to create *refuses the pull outright*. That has killed deploys three times
    (see the Deploy row). If you put a file on a box, commit it the same day.
  - **git carries the CODE and the machinery, never the STATE or the SECRETS.** Those are
    hand-carried, per `docs/MIGRATION.md` §2. The rule cuts both ways: a build step that
    only works because of an untracked local file breaks portability just as badly as a
    secret committed by accident.
  Sits with, and is the operational half of, zeldfep's replicable-instances rule:
  authoritative state must never live only in one process's memory or one box's disk.

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

- **The support rule** (2026-09-02, `5a68517`): **kSupport = 5 grants slots and NOTHING
  else** — Cam's rule verbatim. It used to resolve to kModerator (no support level existed
  and the slot rule needed something to test), which handed ticket staff the whole moderator
  toolkit as a side effect. Enforced structurally, not by memory: kSupport sits BELOW
  kModerator, so every `>= kModerator` check excludes support automatically — support cannot
  pick up a moderation power when one is added later. **kEventStaff = 15** (moderator powers
  + the event tools) exists for the same reason: lowering the spawn commands to kModerator
  would have given `/givecar`, `/npc`, `/time` and `/weather` to support too. Six commands
  moved kAdmin→kEventStaff: `/tp`, `/return`, `/givecar`, `/npc`, `/time`, `/weather`;
  `/ban`, `/unban`, `/rename`, `/quest`, `/fact`, `/setspawn`, `/setstart` stay admin. Slots
  go to `>= kSupport`, the widest staff test. Nothing already stored moved — player 0,
  moderator 10, admin 20, owner 30, exactly as the enum's spacing comment promised. 30
  checks pass, including the four that matter: support fails `>= moderator`, `>= event
  staff` and `>= admin`, and still gets four slots.

### Landed 2026-09-04 (Cam stream) — the "advertised but never built" class
- **`/kill` never downed anyone, and the reason was a missing inch.** Server tracked
  `LifeState`, sent `NotifyCombatState.life_state`, the DLL stored `m_downed` and exposed
  `IsDowned()` to script — and **nothing anywhere called `IsDowned()`**. Every layer
  reported success and the player kept standing. Presentation half written in
  `Combat.reds` (`MpApplyDownedState`, edge-triggered, applies
  `BaseStatusEffect.Defeated`, deliberately NOT `BlockAllMenu`). `/kill` now downs in
  place rather than teleporting, so it enters the medical loop instead of skipping it.
- **`/respawn` did not exist.** The bleedout path had told players "Use /respawn when you
  are ready" since medical shipped. Anyone who bled out was stuck dead with no route
  back. It survived because `/kill` was the only way down and `/kill` never downed
  anyone — **the two gaps hid each other.** Implemented (from `kDead` only, jail wins).
- **`/inventory` did not exist** either, and `/trade item` pointed at it. Found by the new
  check below, not by a person. Lists ids + quantity (ids, not names — the 2.31 name
  helper returns empty strings).
- **NEW VERIFY CHECK — "advertised commands".** Every `/command` inside a `Tell()` must
  have a dispatch block. The compiler cannot see inside a string literal, and neither can
  a review looking at the command being changed. Knows about `||` alias chains, the
  `ChatChannel` table, and `line ==` sub-verb dispatch — all three were false positives
  first, and a gate that cries wolf gets ignored.
- **`/help` listed 4 commands out of 48.** Phone, trading, medical, vehicles and character
  all shipped unlisted. Now topic-based and permission-aware.
- **Selector panel drew off-screen** — and `d96e5ce` had fixed exactly that on
  `test/character-selector` on 21 Aug and was never merged. Cherry-picked. *Check for an
  existing fix on a side branch before writing a new one.*

- **A SYMLINKED PLUGIN DLL SILENTLY KILLS THE WHOLE MOD.** Cam's game came up with a
  completely stock main menu, nothing crashed, and it looked like every menu change had
  been reverted. It had not: RED4ext loaded the plugin, the plugin resolved its OWN path
  to find `assets/redscript` beside itself, and through a symlink "beside itself" is the
  LINK TARGET — the build tree. It threw during `Load`, RED4ext unloaded it, and since the
  plugin is what registers the mod's scripts with redscript, all 46 `.reds` sat on disk
  and never compiled. **A dead mod that leaves a working game is the nastiest failure
  shape there is** — no crash, and the symptom reads as a revert.
  - It broke because `code/server/admin/xmake.lua` deletes and recreates
    `build/windows/x64/release/assets` on EVERY build to stage the admin panel — the same
    directory name the mod's assets used. The mod's link was displaced to `assets$D`.
    **Raised with zeldfep on the feed; his infra, not changed unilaterally.**
  - **`tools/DevInstall.ps1` is now the dev live-update path** and it COPIES rather than
    links, so the mod resolves its assets in the game folder like every tester's install.
    It refuses to run while the game is open, mirrors `.reds` from SOURCE (deleting stale
    ones — they compile as one unit, so a leftover can abort everything), and verifies the
    installed DLL is not a reparse point.
  - **First diagnostic for "the mod did nothing": `red4ext/logs/red4ext-*.log`.** A plugin
    that fails during `Load` says so there and nowhere else.

### Landed 2026-09-04 (zeldfep stream) — the launcher is open source
- **THE RED SMARTSCREEN SCREEN IS UNSIGNED CODE, NOT MALWARE — and the obvious fix does not
  work any more.** Players get "Windows protected your PC" on install. No antivirus is
  involved and Defender has never quarantined anything; SmartScreen is reporting that it does
  not recognise the publisher. Cause, verified: **we have no Authenticode signing at all** -
  no `certificateFile`, `certificateSubjectName`, `signtool` or `CSC_LINK` anywhere, so both
  `NightCityOnline-Setup.exe` and the portable exe ship unsigned.
  **Do not confuse this with our manifest signing.** The ed25519 key `882c415a` proves to OUR
  LAUNCHER that OUR PAYLOAD is ours; Windows has never heard of it. Two unrelated trust
  systems, and "first signed manifest aboard" does NOT mean the binary is signed.
  **Buying an EV certificate no longer fixes it** - Microsoft dropped instant SmartScreen
  reputation for EV in 2024, so EV, OV and Azure all accrue reputation the same slow way.
  Worse, **reputation binds to the SPECIFIC CERTIFICATE with no transfer path** - not between
  CAs, not even across a routine renewal of the same company's cert. **Consequence, and it is
  the rule to remember: pick the final signing identity ONCE, then start the clock.** Signing
  under a throwaway identity burns whatever reputation it earns.
- **`code/launcher-lite` is now also public and MIT: `github.com/NightCityOnline/launcher`.**
  Split out with history intact (219 commits; zeldfep 134, Cameron 78, ofmiceandcam98-eng 4,
  Felipe Retana 2). Verified before publishing: it does not exist upstream at all (404 against
  `tiltedphoques/CyberpunkMP` - their launcher is a different thing at `code/launcher`), it is
  self-contained, no Tilted Phoques source is copied into it, and its full path history
  contains no secret. **Why it had to be its own repo:** the free signing route is SignPath
  Foundation, which requires an OSI-approved licence across ALL components - `LICENSE.md` is
  Tilted Phoques' custom licence with a non-compete, GitHub reads it as `NOASSERTION`, and we
  cannot relicense code we do not own. The launcher we DO own.
  **MIT over Apache-2.0** because every dependency is already MIT, patent exposure on an
  Electron launcher is nil, Apache's NOTICE and state-your-changes duties are friction for no
  benefit here, and Tilted Phoques used MIT for their own carve-outs.
  **THE MONOREPO IS UNCHANGED AND THIS IS DELIBERATE** - `code/launcher-lite` is still here
  and `Ship.ps1` still builds from it. Publishing the mirror and migrating the pipeline are
  two operations; doing both at once would have broken shipping. **The public repo is a
  point-in-time split, not a live mirror** - it drifts until someone re-runs
  `git subtree split -P code/launcher-lite` and pushes, and SignPath builds from the public
  source, so the two must be in sync at application time.
  **Open:** the SignPath application itself (deferred by Cam until the current build is done).
  Honest risk - they also require no proprietary component, and a launcher whose job is
  installing non-open-source software is a fair thing for them to refuse. If they do, the
  fallback is a paid certificate, which needs the LLC that does not exist yet: Azure's
  organisation validation wants three years of verifiable history, so a new entity means a
  commercial CA at roughly 200-500/yr plus a hardware token. Publisher name will be
  **OfficialCutProductions**; nothing is signed until that identity exists, on purpose.

- **DECISION 2026-09-04: the phone ClientRpc pair is APPROVED, and it is NOT a flag day —
  verified against the code, not accepted on argument.** Cam's stream asked before building
  contacts + text delivery into the vanilla phone, because it is new traffic in a migration
  week. Answer: build it. The three legs were checked: (1) `kIdentifier` derives from the
  `.proto` text ALONE (`netpack/main.cpp:368-369` — `kProtocolString = HashProtocol(...)`,
  `kIdentifier = FNV1a64` of it), and an RPC pair is a REGISTRATION rather than proto text, so
  the identifier does not move and the door checks at `GameServer.cpp:819/831` refuse nobody;
  (2) ids are negotiated PER CONNECTION — `GameServer.cpp:1096-1102` serializes the full
  `(id, klass, function)` mapping and sends it BEFORE `AuthenticationResponse`; (3) an old
  client fails safe — `GetRpcHandler` returns null for a function it lacks and `Call()`
  refuses rather than misdispatching.
  **TWO CONDITIONS, and the first is the whole point of the ask: LAND IT, DO NOT DEPLOY IT
  UNTIL THE MIGRATION IS VERIFIED.** `feat/world-state` is what the NAS cron deploys; a server
  rebuild landing mid-migration is how a clean move becomes an evening. If it must sit on the
  branch before then, the cron gets PAUSED deliberately rather than both streams assuming the
  other did it. **Second: the contacts snapshot must tolerate arriving before its
  definitions** — definitions land at auth, the injection point is the spawn path, and spawn
  is also where a character SWITCH lands; an unresolved id makes the first snapshot vanish
  silently, which will read as "contacts are empty for the first character you pick" and get
  misdiagnosed as a `MessageStore` bug.

### Needs a live session (built, never validated with humans)
- **Pause menu unpause is BEST-EFFORT, 2026-09-04.** The menu opens again (an inline
  `UnpauseGame()` in `OnInitialize` was killing it — the `SetMenuModeEvent` is queued and
  consumed a frame later, so the unpause landed before the layer read it). The unpause is
  now deferred 0.25s. **Unconfirmed: whether `DelaySystem` ticks at all while the game is
  paused.** If it does not, the callback never fires — menu works, world still pauses.
  That is the safe failure mode, but it needs one look: open the pause menu and watch
  whether NPCs behind it keep moving.
- **`/tp spawn` is deliberately NOT staff-gated** — flagged to Cam, reversible in one
  line. A player who has fallen out of the world cannot play at all and the alternative is
  waiting for a moderator; it moves only the caller, to a published location.
- **Start-point origin guard is SERVER code and the test box has no cron.** Until that box
  rebuilds, only zeldfep's client-side recovery guard is protecting new arrivals.
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

- **Vehicle damage: native path PROVEN from the game's own sources; probe RUN 2026-08-26
  on Cam's install - the reported value is EXACT, the server validates against it, never
  recomputes it.** Sources at `<game>\tools\redmod\scripts`; every claim below has a
  file+line, none inferred. Vehicle health is a stat pool (`gamedataStatPoolType.Health`
  on the vehicle's own EntityID, owned by StatPoolsSystem): read
  `GetStatPoolValue(id, Health, false)`, set `RequestSettingStatPoolValue(...)`, observe
  `RequestRegisteringListener(id, Health, listener)` → `OnStatPoolValueChanged(old, new,
  percToPoints)` (vehicleComponent.script :79, :4543, :4545, :6304). Damage arrives as
  `gameHitEvent`; `VehicleObject` already overrides `DamagePipelineFinalized`
  (vehicles.script:1123) - the game announcing it finished calculating. Attacker:
  `evt.attackData.GetInstigator()` (in use at vehicles.script:1137); weapon:
  `attackData.GetWeapon()` → WeaponObject → `GetWeaponRecord()` (attackData.script:219 -
  GetWeaponRecord is NOT on AttackData, which cost a compile); amount:
  `evt.attackComputed.GetTotalAttackValue(Health)`, the same call Combat.reds:97 already
  uses for players. Destruction: `gameVehicleDestructionEvent` (hitEvents.script:51),
  destroyed = Health custom limit forced to 0.0, stages via `EvaluateDamageLevel` →
  `m_damageLevel` 0-3. No CET, no WolvenKit, no native work for the core loop; bullets,
  explosions, collisions and environment ALL converge on the one Health pool - one
  listener catches every source, collision damage only needs blame assignment.
  Measured: one shotgun blast at a parked street car = **14 hits inside ONE millisecond**
  (18:32:10.997-.998), each ~8 damage; health 1050.319946 → 932.885620 (spent 117.434326)
  vs sum of the 14 reports 117.434414 - agreement to 0.000088, float noise. Also
  confirmed live: the attacker resolves (`by PLAYER`), a street car has ~1050 HP, and
  DamagePipelineFinalized fires BEFORE the pool settles - all 14 read the same "health
  before"; the probe's 0.15s delayed readback caught the settled value.
  **Protocol consequence, a real design input:** damage arrives PER HIT, not per shot -
  one trigger pull can be 14 events in a millisecond, so the wire must batch or tolerate
  bursts; 14 packets per shotgun blast is not acceptable. **Probe trap, recorded because
  it nearly turned correct data into a wrong conclusion:** the first verdict compared ONE
  hit against the WHOLE burst's delta and printed MISMATCH fourteen times - data perfect,
  comparison wrong (a single hit is expected to be far smaller than the total); verdict
  logic since corrected to say BURST. Still open, needs two humans (the probe settles
  it): does a REMOTE vehicle (kinematic, physics suppressed by MakeRemoteDriven) receive
  the damage pipeline at all, and on whose machine - decides whether the shooter or the
  owner reports damage.

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

- **The 2026-09-02 batch — SHIPPED `v0.3.114-worldstate-test.19 (the consolidated build - test.15-18 and the old .19 are deleted; byte-identical to v0.3.114, pairs with both servers)`, NONE of it played.**
  feat/world-state @ `5a68517`; `main` untouched at `304b492`. Aboard: voice fix, phone
  calls in the real in-game phone, texting/contacts/blocking, character slots + selector +
  soft delete, vehicle seat validation, money confiscation fix (the destructive half of the
  money bug — see known-bugs), player trading, downed/medical, the new staff permission
  ladder. The test build is a pre-release, dev-role only, installed from Settings → DEV →
  Test builds; payload verified to carry the new redscript. **The test checklist lives in
  the `v0.3.114-worldstate-test.19` RELEASE NOTES on GitHub** - durable, public, and it
  travels with the build it tests. (It was previously a chat-session artifact link, which
  only resolved for one account.)
  Genuinely open, in order:
  1. **Two-player tests entirely unrun** — trading, calls, medical revival; vehicle seat
     validation needs FOUR bodies. All of it is offline-tested only, and it waits on the
     server being current with this code.
  2. **Does a runtime-built `CName` render on the phone?**
     `PhoneCallInformation.contactName` is a CName, not a String — calls will present
     either way, but the caller's NAME may come back blank. Compiling cannot answer it;
     only a live call can.

- **Trading (`f313a61`) — built, not shipped, zero two-player runs.** Governing rule:
  **trading never CREATES value.** The commit is `PlayerStore::ApplyTrade`
  (`code/server/native/PlayerStore.h`), NOT a trade system beside it — PlayerStore already
  owns money and possessions, and a second thing that could move them would be a second
  authority. `TradeStore` (`TradeStore.h`) decides *whether*; PlayerStore decides that it
  happens *completely*. Atomic, and honestly which kind: both records copied, both mutated,
  validated on the copies, assigned back, then ONE flush — no ordering shows a partial
  exchange. NOT durable-transactional (the store is a JSON file; a write-ahead journal
  belongs with the move to a database, which is where the replicable-instance rule takes it
  anyway). **Reservations ARE the offers** — no separate table, deliberately: a table and a
  set of offers are two representations of one fact and they drift, and a drifted
  reservation is money promised twice. **`/pay` now checks AVAILABLE money, not owned** —
  that single line is what makes reservations mean anything, because otherwise 10,000 could
  fund an 8,000 trade AND an 8,000 phone transfer. Confirmations reset on every edit, both
  sides — carrying one across is how somebody accepts a deal they never saw. **`Committing`
  is not cancellable** — by disconnect, death, timeout or distance; it either moved both
  sides or neither. Honest limit: inventory is `(TweakDBID, quantity)` stacks with NO unique
  item instance ids, so a weapon's mods/attachments/condition are not modelled and it trades
  as its base record — faking instance identity would be worse than not having it.

- **Medical revival (`70eac6e`) — built, not shipped, zero two-player runs.** A LAYER, not a
  system: `HealthComponent` (`Components/HealthComponent.h`) already held `LifeState` and
  lethal damage already downed rather than killed — added bleedout → death, stabilise,
  revive. Fields live ON `HealthComponent` — two components describing "how alive is this
  person" would disagree, and the disagreement would decide whether somebody could be
  revived. **The timer is a DEADLINE, not a countdown**: `DownedAt` is a timestamp, so a
  skipped or doubled tick delays an outcome without changing it. **Bleedout starts once** —
  shooting somebody already down does not restart it, or an attacker could hold a victim
  permanently un-revivable. **Stabilised means the bleeding STOPPED**, not "more time" —
  otherwise a medic who did everything right still watches their patient die on a hidden
  clock. Revive requires stabilise first, or stabilisation is pointless.
  **`kTreatPermission` is `kPlayer` on purpose** — a medics-only rule on a server with no
  medics means everyone who goes down dies; one constant to raise when roles exist.

- **Vehicle seats (`eff82e0`) — built, needs FOUR humans in one car.** Why a fifth person
  fitted in a four-seat car: occupancy was checked, seat IDENTITY never was — any invented
  64-bit id matched no attachment, looked like an empty seat, and was accepted (four already
  fitted; a fifth only had to name a position nobody had claimed). **The check is identity,
  NOT a count, deliberately** — the server cannot see game data and does not know how many
  seats a Mackinaw has; counting would mean refusing seats that exist or inventing ones that
  do not. Which seats a car HAS stays with the game's mount system; which seats EXIST is
  `code/server/native/VehicleSeats.h`. Seat ids are FNV1a64 CName hashes, verified offline —
  `FNV1a64("seat_front_left")` reproduces the `0xb000b1d029d0cea0` constant `Level.cpp`
  always carried — so they are derived at compile time from names and all four pasted
  literals are gone. Failure mode to know: in `NextOccupant` a mistyped literal would not
  crash — it would just never match, and a disconnecting driver would hand the car to the
  WRONG passenger. `/vehseats` (moderator+) prints the server's own view: every occupied
  vehicle, who is in which seat, and who is simulating it.

- **Player-to-player calls (`7be0a20`) — built, NOT shipped; server-only, no protocol
  change.** Cam's rule, stricter than the brief that prompted it: *"player to player calls
  are the only calls that can come through"* — every game-originated call stays blocked.
  Commands: `/call`, `/answer`, `/decline`, `/hangup`, `/calls`; ring timeout 30s. 35 checks
  pass against the real `CallStore.h` (`code/server/native/CallStore.h`), covering the
  brief's matrix and the "one player's two characters share nothing" case.
  **THE SONGBIRD GATE IS NOT MODIFIED, AND THAT IS THE DESIGN — do not "improve" this by
  relaxing `PhoneSystem.OnTriggerCall`.** `OnTriggerCall` takes a `questTriggerCallRequest`,
  the QUEST system's request type, and the quest system is the only thing that builds one;
  its `isPlayerTriggered` field means *"the player triggered this QUEST call"* (ringing a
  fixer back from the journal), NOT "a multiplayer player started a call". Gating on it
  would let a class of story calls back in and would make the Songbird block depend on a
  field the mod never sets and cannot audit. A player call never becomes a
  `questTriggerCallRequest` — it arrives as a chat command, lives in `CallStore`, and is
  presented by the mod; the two kinds share no field and no entry point, so the origin is
  unambiguous BY CONSTRUCTION rather than by inspection — "player calls work AND the
  prologue stays blocked" is a property of the shape, not a test that has to pass. Verified,
  not asserted: `Quests.reds` is unmodified, and every mention of `PhoneSystem` /
  `OnTriggerCall` / `questTriggerCallRequest` / `isPlayerTriggered` across all changed files
  is a COMMENT — no line of code in the feature references any of them. The brief's "do not
  build a parallel system, route through PhoneSystem" was DECLINED deliberately: routing
  through `OnTriggerCall` means forging a `questTriggerCallRequest`, after which the gate
  can no longer tell the two apart; that instruction and the brief's own acceptance criteria
  are in tension, and the criteria win.
  **Voice is what makes it a call**: proximity voice already existed; a connected call also
  routes the speaker's frames to the other party regardless of distance, checked against the
  listener's ACTIVE character — resolved once per frame, not per listener, because that loop
  runs for everybody online, 50×/second per speaker. **Sessions in memory, history on
  disk**: a call is a conversation, not property — a restart mid-call should mean the call
  ended, which an empty session list already means; held in a `std::list` because
  `Active()`/`Find()`/`Expired()` hand out pointers, and a vector would reallocate and
  dangle them intermittently, only on a busy server. **A blocked call reads exactly like an
  unanswered one** — a refusal that differs is a refusal that tells somebody they were
  blocked. **No call id is accepted from the client** — `/answer`, `/decline`, `/hangup`
  resolve the sender's own active call, so one player cannot hang up another's. **Switching
  characters ends the outgoing character's call** before the switch — belt and braces today
  (the puppet check already refuses an in-world switch), but it is the line that stops a
  call surviving if that rule is ever relaxed. **The remaining piece is presentation**:
  surfaced through chat, not the phone UI — a real incoming-call panel is a presentation
  layer over the same session state; build it against `CallStore`, never as a second call
  implementation.

- **Digital life phase 1 — messaging, contact names, blocking — BUILT, NOT SHIPPED.** Phase 1
  of ChatGPT's 2026-09-02 "character digital life" brief. **Server-only, no protocol change**,
  so unlike the slot work it can ship on its own. Commands: `/text`, `/texts`, `/read`,
  `/contactname`, `/delcontact`, `/block`, `/unblock`, `/blocked`.
  - **Read the brief against the code before building more of it.** Most of its Phase 1 and
    half of Phase 2 already existed: `CharacterId`, unique per-character `PhoneNumber`,
    per-character `Contacts`, `/number`, `/addcontact`, `/contacts`, `/pay` (already
    server-authoritative, atomic, audit-logged — brief §10 describes what is built). Two of
    its instructions were REJECTED, deliberately: `char_8f31a92c`-style 16-hex character ids
    (the shape Cam rejected as "too big", and it has no check symbol) and 10-digit phone
    numbers (ours are already unique strings; changing the digit count breaks every number
    handed out). Its central rule — the CHARACTER owns the digital identity, not the account —
    was already how `CharacterRecord` is built.
  - **`MessageStore` (`code/server/native/MessageStore.h`), addressed by `CharacterId`, never
    by account.** Its own store, NOT a field on `CharacterRecord`, for a reason that matters:
    `SaveCharacter` REPLACES a character wholesale from what the client reported, so a message
    arriving between a client's read and its write would vanish silently — same class of bug
    as the money thrash.
  - **Offline is the normal case.** Written to disk first, delivered second; `Delivered` is
    stored per message, never assumed — a recipient who was away, crashed, or dropped the
    push gets it on next arrival. `MarkDelivered` runs AFTER every line is handed to the
    connection: showing a message twice is a blemish, losing one is the bug.
  - **Conversations are keyed on the SORTED pair** — A-texts-B and B-texts-A are one thread.
    Unsorted, they are two threads each holding half of what was said and both people see the
    other ignoring them — a bug that reads as a UI problem for a week. Repaired on load too,
    so a hand edit cannot create it.
  - **Contacts gained a name (`Contact` struct) with a two-way-safe migration**, verified by a
    standalone test: an existing bare-string list loads, and an unnamed contact still WRITES
    as a bare string — only accounts that actually save a name change shape, and a rollback
    keeps working for everyone else. Deleting a contact never touches message history (falls
    out of messages being their own store, not hanging off the phone book).
  - **Blocking is per character, silent, and aimed at a number.** Blocked messages are
    accepted and DROPPED, never refused (a refusal is a signal — "delivery failed for this
    number, sent for that one" tells somebody they were blocked) and never stored (storing
    and never delivering would dump the backlog on whoever later unblocks).
  - **`/addcontact` compared Discord ids, which the slot work made wrong** — it would tell a
    player their own second character's number was "your own number" and refuse it. Now
    compared by character. LOOK FOR MORE: every account-keyed phone lookup is suspect now
    that an account can hold four characters.
  - Delivery-on-arrival hooks the spawn path, which is also where a character SWITCH lands —
    switching hands over the new character's inbox and none of the old one's.
  - **NOT built, and why:** calls (brief §12–13) need `PhoneSystem.OnTriggerCall`, which is
    currently blocking EVERY call — see [[project-cyberpunkmp-phone-calls]] and the
    player-to-player-calls entry before touching it. Money attachments (§9–11) are BLOCKED on
    the money persistence bug — building transfers on a balance that does not survive a
    session is how duplication bugs are born. Social, email, computers, files, contracts,
    taxi and media (§18–28, §41–55) are later phases with no foundation dependency on this
    one.

- **Slots, soft delete and the character selector (`9a70e1d`) — BUILT, NOT SHIPPED.** Branch
  only, no release, `main` untouched. **FLAG DAY when it ships**: `CharacterSummary` gains
  `slot`/`is_active`, `AuthenticationResponse` gains `character_slots`, plus two new client
  messages — every un-rebuilt client is refused. Batch anything else waiting on a protocol
  change with it.
  - **Most of the server model was already here, unused**: `Characters` is a vector,
    `ActiveSlot` exists, `RetiredCharacters` exists, and `RetireCharacter` already took a
    slot and already did a SOFT delete — the row moves rather than being erased, so an id is
    never reissued and a deletion can be undone by hand. What was missing was everything
    above it, and the fact that `SendCharacterList` collapsed the roster to the active
    character wrapped in a list of one.
  - **One slot for a player, four for admin and above** (`PlayerStore::SlotsForLevel`). A
    PERMISSION: the server decides and the client is told — an allowance the client computes
    is one it can raise. Lowering it never deletes anything: an over-limit account keeps
    every character and simply cannot make another.
  - **Lifetime row ceiling of 60, separate from the slot count** — necessary BECAUSE deletion
    is soft: create-and-delete-and-create writes a new row every time.
  - **Slots are NOT contiguous and NOT an identity.** Retiring the character in slot 1 of
    three leaves 0 and 2 occupied. The panel draws by walking slots and looking each up in
    the roster, NEVER by walking the roster — the other way silently renumbers. Anything
    keyed on a slot number is a bug waiting for a deletion.
  - **`SelectSlot` refuses an empty slot** rather than falling back to slot 0 — "you asked
    for a character that is not there, so here is a different one" is how somebody ends up
    playing, and then saving over, a character they did not choose.
  - Roster accessors bounds-check, and `GetRosterSlot` answers **-1** for a bad index, NOT 0
    — zero is a real slot, and a caller reading it as one would offer to delete the slot-0
    character when asked about a row that is not there.
  - **Why the selector was off, fixed at the cause**: nothing on that screen asked the server
    who the account was — the note that replaced those lines said the panel would sit
    "signing in… forever against a connection nobody opened", and it was right. On the
    branch a **CHARACTERS** menu entry opens the connection deliberately, polls until the
    roster lands, and builds the panel only once the answer is here — it either shows the
    roster or is not on screen.
  - **SHIPPED builds still have the selector OFF**: `MainMenu.reds:331-342` disables the
    trash can and the panel (both only make sense once the menu asks the server who the
    account is, and shipped builds no longer do). Delete-a-character is BUILT on shipped
    code too — `DeleteCharacter()` native, the `OnMultiplayerDeleteCharacter` handler, and
    the confirm flow all exist and still compile; **uncommenting `MainMenu.reds:338-340` is
    all it takes to bring the entry back**. The requested four-admin-slots feature needed the
    selector alive first; the plumbing predates the branch — `AuthenticationResponse`
    deliberately carries a LIST of `CharacterSummary` ("a list of length one costs nothing
    today and is the whole difference later") and `CharacterRecord` already had `Slot` +
    `CharacterId` — and the branch supplied the missing client panel that draws four slots
    and says which are in use.

- **Character lifecycle state + presence-bit names (`a0346ce`) — BUILT, NOT SHIPPED, on Cam's
  instruction** (build, don't ship). `feat/world-state` only: no release, deliberately not
  pushed to `main` — `main` is the deploy.
  - **A character lifecycle STATE replaces `m_characterLive`.** The boolean stood in for five
    states, and the missing one is what cost a character: at the moment of the overwrite Cam
    had a live connection, a valid record, and a body in the world that was neither.
    `Connected → Selected → Restoring → Live → Detached`; `Live` is the ONLY state in which
    writing to the stored record is legal. Every transition logs; `Live → Detached` logs as a
    WARNING — that session would have read `Live -> Detached (world detach)` seventy seconds
    before the save. A refused save names the state it refused in.
  - **The wire's presence width is now said out loud**: netpack emits `kXPresenceBits` beside
    every generated struct (39 of them). The bitfield is sized to a message's OPTIONAL field
    count, and a reader expecting a different width misreads every field after it — none of
    which is visible in the `.proto`. NOT a guard (the protocol identifier already refuses a
    mismatched client): a NAME, so a diff of the generated header shows `4 -> 6` rather than
    showing nothing.
  - **Correction it caught immediately**: `SpawnCharacterResponse` reads **4** presence bits,
    not 6. The 6 was hand-derived once while two extra WIP fields were still applied, then
    repeated as the baseline in a commit message, a design doc and an artifact — the
    conclusion never changed, the number was wrong three times. That is the whole argument
    for naming a value rather than re-deriving it.
  - **NOT built, deliberately: "placement is not a teleport".** Its own entry in
    `docs/CHARACTER-LIFECYCLE.md` says it is an experiment and nobody should fix it
    speculatively — building it because it appeared on a list would be exactly that. It stays
    a question: does 2.31 offer a placement path that preloads streaming, and is
    `TeleportLocalPlayer` skipping it?

- **Appearance restore: the bytes were spent before the attempt — FIXED (fault B of "I am
  not the character I made"), NOT VERIFIED LIVE.** On a fresh connect the server sends the
  appearance while the player is still on the MAIN MENU (`MpLoadOwnCharacterSave` has not
  loaded a save yet). 2026-09-01 log, in order: `server sent 9206 bytes` → `restore BEGIN` →
  `FAILED: GetState returned nothing` → only then `[OwnSave] loading 'AutoSave-12'`. Old code
  did `const auto bytes = std::move(m_restoreAppearance)` BEFORE trying, so that doomed first
  attempt consumed the only copy — the world then attached with nothing left to apply and the
  player looked like whatever save had loaded. Now: ask whether a live customization state
  exists BEFORE spending the bytes, keep them when it does not, clear only after a commit is
  accepted. Cheap live check: `appearance held - no live customization state yet` once, then
  a real BEGIN after the world attaches. Fault A — `OwnSave` picking a save by file order
  instead of identity — is FIXED 2026-09-04 by always loading the template and letting the
  server own identity; see the "I am not the character I made" entry for why the named-save
  plan was dropped. **COMPILE-CHECKED 2026-09-04 against the real 2.31 install on this box.**
  Still unproven live: whether always starting from the template leaves a player visibly
  Veronica when the appearance restore does not land — that is one join away from an answer.

- **"Nobody could see anybody" — never the puppet system. FIXED `ec2858d`, awaiting a live
  run.** `NetworkWorldSystem::Spawn` was never called ONCE in twenty-one session logs — no
  `[Spawn]` line of any kind, not even the "queued" or "CreatePuppet failed" branches — so
  stop looking at `PuppetDriver`, remote animation, or the appearance path: none of them ever
  ran. Since `f66a40a` (2026-08-24) the client re-derives each load's cell from its position
  and drops the load when its answer differs from the server's (`[World] dropped map-invalid
  character load …`) — that rejected EVERY character and vehicle load for six days. Movement
  and appearance are separate messages and kept arriving, which is why the player list showed
  people, `/tp` moved them server-side, and the client logged `movement for id … but no
  puppet is registered under it` — it looked exactly like a spawn bug and was not one. Two
  faults, both server-side: the grid was declared twice (`Level.cpp` bins by `sCellSize =
  6000`, `GameServer.cpp` advertised `60000` — ten times out), and `ToCell` used a
  float-to-int cast (truncates toward zero) while the client uses `std::floor` — those agree
  only for positive coordinates and Night City is mostly negative (at `x = -1769` the server
  sent cell `0`, the client expected `-1`). Fix is server-side ONLY — no client rebuild, no
  player update: `kCellSize`/`kLoadRadius`/`kUnloadRadius` now live on `Level` as the
  contract they are, `GameServer` advertises those constants, `ToCell` floors. NEVER put a
  literal back in either place — the client is entitled to compute what the server computes.
  Verify with: `[Spawn] remote id … - ccstate N bytes` appearing and `dropped map-invalid`
  stopping; if the drops stop and players are still invisible that is a DIFFERENT bug — the
  puppet spawn itself — and the `[Spawn]` line will name it. Production deploys from this
  branch, but the cron defers while anyone is online.

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

- **Seat/occupancy state — already built and already true, do not rebuild or "fix" again
  (audited with `eff82e0`):** `sit_id` on the wire for enter/exit/notify;
  `AttachmentComponent{Parent, SlotId}` as real server seat state; one-per-seat enforced and
  REFUSED rather than reassigned; stale seat-swap exits recognised; late joiners get a
  `NotifyVehicleEnter` per occupant (§18); a disconnecting driver hands authority to a
  passenger and the car survives (§16); `ReleaseVehicleIfEmpty` counts EVERY attachment and
  PARKS rather than destroys (§17 — already correct; the destruction it used to do was a
  workaround for a different bug — entering a car spawned a fresh entity every time, seven
  copies once stacked in one road — prevented at the other end now).

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

### The cell grid does not actually cull anything (measured 2026-09-04)
- **`kCellSize = 6000` is larger than Night City, so the spatial partition is inert as a
  RELEVANCE filter — everyone is always in everyone's radius.** Measured against the live
  server's own stored state (`config/players.json`, `vehicles.json`, `startpoint.json`,
  `respawn.json` — 11 real positions): `x` spans **-1759.7 .. 672.8**, `y` spans
  **-1956.5 .. -1261.0**, `z` spans **27.9 .. 69.7**. At `kCellSize = 6000` every one of
  those falls into **two cells — (-1,-1) and (0,-1)**. `kLoadRadius = 3` then covers a 7×7
  block, 42,000 units on a side, against a world whose observed extent is ~2,400 × 700.
- **Consequence, and it is a design input rather than a bug report:** every player receives
  every other player's and every vehicle's updates regardless of distance, so bandwidth
  scales with the square of the player count and has no falloff to lean on. The
  per-connection interpolation-delay work and any future relevance filtering are therefore
  worth MORE than they look, not less — there is currently nothing else reducing what a far
  player costs.
- **It also explains why the cell-size bug was catastrophic rather than merely inefficient.**
  The grid gates LOADING, so a wrong cell dropped loads entirely ("nobody could see
  anybody", six days) while never delivering the culling the size was chosen for. The
  `std::floor` fix and the single-source constants remain correct and necessary; what is
  wrong is the SIZE, and nothing today depends on that size being large.
- **Honest limit on the measurement:** 11 stored positions from one server, not a survey of
  the map. It is last-known player positions, parked vehicles and spawn points, so it does
  not prove the whole city fits in two cells — but Night City is roughly 5 km across and the
  cell is 6,000 units, so the whole world is a handful of cells either way. Anyone wanting
  certainty can widen the sample from `logs/clients/` movement traces.
- **NOT CHANGED. Do not "fix" this by shrinking `kCellSize` casually** — it is a wire
  contract the client re-derives (`GameServer` advertises it, the client checks its own
  answer against the server's and DROPS mismatched loads), so a change is a flag-day-shaped
  event even though the identifier does not move. Decide the number deliberately, change it
  on both sides in one commit, and expect the drop-loads path to be the thing that bites.

### Known bugs, diagnosed, unfixed
- ~~`RpcService::Call` derefs null on a default-constructed slot~~ **LANDED 2026-09-04 (Cam
  stream), all three parts, and the read turned up a worse one than was reported.** zeldfep's
  inference was right: the guard `if (rpc.Id.Klass != 0 && !pContext)` lets through the one
  shape it needed to catch, because an unwritten slot has `Klass == 0` AND a null handler.
  Now an unconditional `if (!pContext)`.
  **The worse bug underneath it:** `HandleRpcDefinitions` sized the table by
  `client_definitions.size()` — a COUNT — and wrote to it by `rpc.get_id()` — an ID. Those
  agree only while ids are dense from zero, so any id ≥ count was an out-of-bounds WRITE, not
  a null read. Heap corruption in a handler driven by whatever the server sends. Now sized by
  highest id + 1. Same root as the reported bug (count-vs-id), strictly worse consequence.
  Refusals also log once per id per connection now (cleared when definitions arrive), so a
  snapshot pushed to an older client cannot spam a line per push per player.
  **This was the blocker on the phone RPC** — the operation that could leave a gap is adding
  registrations, which is what the phone pair does. That path is now safe to grow.

- **THE OBSERVER CRASH: cause found 2026-09-04, and it was REPETITION, not memory.**
  zeldfep died (exit `0x80000003`) with his log ending mid-apply, one line after
  `Scheduling change`. The session held **15 remote appearance applies in 16 minutes,
  every one with an IDENTICAL ccstate** (hash `78a96dec...`); seven were exactly 90s
  apart - the possessions autosave. The UAF Cam fixed in `c346f5a` was already in that
  build, so the memory was sound: what killed it was asking the engine to synchronise an
  appearance the puppet was ALREADY wearing, over and over. Trigger: clothing and
  customization share one message, and a visual item count that flips on its own (4 items
  -> 5 -> 4; a holstered weapon does it) makes the server see a real change and broadcast
  it. FIXED client-side (`fbb7b33`): the scheduled ccstate hash is remembered per entity;
  a matching update applies clothing and leaves customization alone. **VEHICLES ARE
  EXONERATED** - the 90s cadence ran for ten minutes before the first mount, and the
  `[Interpolation] movement for id N but no puppet is registered - this is a frozen
  remote player` warning is MISLABELLED: those ids were VEHICLES (each followed by
  `OnVehicleReady: mounting queued character ... into vehicle N`). Rename that warning.
- **WRONG CHARACTER = the real root cause, still open (ledger fault A).** Cam picked
  MALE; every other client renders him FEMALE. Proof in zeldfep's log: `remote state
  produced 24 customization key(s), male=0`, applying `t2_formal_04_q000_corpo_&Female`
  and friends - the world template's default corpo V in PROLOGUE clothes, not the
  character he made. His stored blob also flip-flops 10232 -> 6484 -> 10232 bytes, i.e.
  different appearances competing, not one being resent. The server's sync is INNOCENT
  and working (it logged 6 real changes, not 15, and its unchanged-guard holds); it is
  faithfully broadcasting a bad capture. **HALF-ADDRESSED 2026-09-04 and the other half got
  MORE URGENT, deliberately - read this before concluding the fix made things worse.**
  `OwnSave` no longer picks a save at all: the template loads every time and identity comes
  from the server (see fault A). That removes the *random* wrong character - a probe's
  leftover Corpo can no longer win a file-order race. **It does NOT remove the template's own
  identity, and it makes every machine start from it.** The body at load is now Phantom
  Veronica for EVERYBODY until the server's appearance restore lands over the top, where
  before a player with their own save might have started closer to themselves by luck.
  That trade is intentional: one known starting state that the restore must beat is a bug
  with one cause, where two competing sources is a coin flip nobody can reproduce. **The
  consequence is that the appearance restore is now the single thing standing between a
  player and being Veronica, so its priority goes UP, not down.**
  Still worth building, and unchanged by any of this: the client reports the gender of the
  state it captured and the server refuses a capture contradicting the character record's
  `IsMale`. Needs a wire field, so it is a flag-day - batch it with the slots work. It would
  have caught this in the first second instead of after a night of crashes.

- **THE crash: SOLVED 2026-08-26/27 (`0da3c9b`, `559828f`, `0fa2bb9`) - never entity
  readiness; a genuine data race, two threads inside flecs' `flecs_stack_restore_cursor`
  on the same stack allocator at the same instant.** flecs' stack allocator is
  deliberately unlocked (documented per-stage, single-threaded); the game's job system
  called into our flecs world off the main thread from THREE places: the combat hit hook
  (every bullet), `VehicleSystem::OnVehicleEnter`, and `OnVehicleReady` - all three
  wrongly assumed main-thread "because they are RTTI methods called from redscript". A
  redscript caller says NOTHING about which thread the engine's job system dispatches on;
  that assumption cost a full day before someone measured instead of read. Fix: flecs
  removed from every off-thread path (lock-free `ServerIdRegistry`, modelled on
  `PuppetRegistry`) rather than locking the allocator - a lock would cost frame rate on
  the every-bullet hit hook. Fixed alongside, unrelated (`98b12fa`): an `OnAdd` observer
  adding a component during its own dispatch, which flecs' own source warns against.
  Vehicle-mount and weapon-fire crashes were the SAME bug - v0.3.112 release notes say so
  directly ("Same cause behind the crash when getting on a bike or into a car"), and
  `0fa2bb9`'s follow-up session recorded ZERO 0xC0000005 on a session that used to
  produce them within seconds. **Red herrings, both closed - real observations, wrong
  mechanism:** the entity-readiness theory (docs/CRASH-FIX-BRIEF.md - historical from
  here, do not resume its Tier 1/Tier 3 work) and the female-appearance correlation
  (incl. remote APPEARANCE apply, `AddItemToSlot failed` x106/evening for one player) -
  coincidental exposure to the race, not a female-specific code path. Historical
  signature, kept for anyone re-deriving it: dies within seconds of `[Interpolation]
  movement for id N but no puppet is registered` → `HandleVehicleEnterMessage: queueing
  mount` → `OnVehicleReady` → `DoMount` / `MakeRemoteDriven: SetKinematic ... done`;
  reproduced on demand 2026-08-26 via the mundane trigger - disconnect while driving,
  then rejoin: the car survives in the live flecs world with no persistence trail, gets
  replayed to the rejoiner, dead 1.6s after MakeRemoteDriven finishes. **Kept from the
  investigation, true regardless of cause:** `Level::ClearAbandonedVehicles` clears every
  vehicle server-side at zero players (`0e00c77`), and the CRASH-FIX-BRIEF fixes Copilot
  implemented 2026-08-24 (the AppearanceSystem span-lifetime UB, the CMPReader
  silent-corruption fix) were real bugs, just not THE crash. **STILL OPEN - a DESIGN
  question now, not a crash:** A disconnects while driving, B stays online, C joins
  later - the car legitimately survives (parking on zero-players doesn't cover it) and
  hands C a car with no valid driver. Post-race-fix behaviour is UNMEASURED; worth one
  session confirming it merely looks odd rather than crashes, before spending time on it
  as a crash.

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

- **Parked persisted vehicles + MakeRemoteDriven: two positions contradict - needs a
  human call, not another edit.** This entry originally claimed a parked replayed car
  must NOT be made kinematic (MakeRemoteDriven belongs to occupied vehicles only).
  Current `VehicleSystem.cpp::OnVehicleReady` argues the opposite ON PURPOSE, in its own
  comment: physics-off (`MakeRemoteDriven`) runs for EVERY network-spawned vehicle
  unconditionally, occupied or not, specifically so a parked copy can never simulate
  physics against the local world's own copy of the same car. Left untouched (Copilot,
  2026-08-24): one claim is reasoning about a live bug and the other is the fix already
  applied, and which is which is not recorded. Check `git log` on that function before
  changing it either direction - one of the two claims is stale.

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

- **Character face/body loads from the LOCAL save, not the server - Cam actively fixing,
  2026-08-28 (v0.3.113's notes still say "known/unfixed": stale).** Landed since that
  release, in order: `54d9a85` - the server now sends the owner their OWN stored
  appearance (previously it only ever went to everyone else) and the client attempts the
  local apply. `78713d7` - `InitializeState` was never even reached: every customization
  method lives on `gameuiICharacterCustomizationSystem`, the INTERFACE, not the concrete
  class the game hands back (empty function list); dispatch now resolves against the
  interface explicitly. `e795a52` - overturned "a live customization state requires the
  creator": `equipmentSystem.script:4992` calls `GetState()` during ordinary gameplay,
  so `GetLiveCustomizationState()` resolves the same way, no `InitializeState` at all.
  **Only the visual outcome is open** - whether deserializing into that live state and
  calling `ReFinalizeState` actually rebuilds the body mid-session; a pure visual check
  nothing else substitutes for. Cam: "Phantom Veronica on screen is the only test that
  counts." One live join answers it; the dispatch bug and the InitializeState/live-state
  question are BOTH closed - do not restart the investigation from scratch.
  **RULED OUT BY CAM 2026-08-30: the mod-spawned-puppet approach.** The Phase 1
  experiment (`2356232`, `LocalPuppet.reds`, the `-mod-local-puppet` flag and its
  `IsModLocalPuppet*` natives) asked whether a mod-spawned puppet could BECOME the local
  player, as an alternative to changing the vanilla V's body in place. Cam: "we wont be
  doing the mod-spawned puppet thing." Do not revive it, and do not treat it as the
  fallback when the `GetState` path is tested - the remaining route is the one already
  written: write the stored ccstate into the live customization state and re-finalize.
  The experiment code is dead weight (off unless `-mod-local-puppet` is passed, harms
  nothing shipping) but should be removed rather than left looking like an open option.
  Reasoning it was built on: the "Local Puppet Migration" artifact.

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

- **Money does not persist — but the restore no longer CONFISCATES earned money (`f53e4df`,
  untested live).** The 2026-08-28 observation stands: 84 eddies picked up; every subsequent
  capture read exactly `20000` — nothing decayed, a gain never entered the record.
  Candidate: a second restore ran mid-session (`restore DONE: money 20000 -> 20000`)
  re-applying the server's snapshot, which would also violate the items-must-stay rule. The
  DESTRUCTIVE half is fixed: `owed = stored - held`, and when negative the restore REMOVED
  the difference — negative means the player holds more than the server last heard, which
  after creation is the normal state of anyone who has earned anything; that is the exact
  shape of *"I found 84 eddies and they didn't save"* (the money arrived, the capture had
  not run, the next spawn took it back). The removal could not simply go — it is the only
  thing that strips the world template's balance, because `MpSettleStarterLoadout`
  deliberately never touches money — so it is **gated to the character's first spawn**, the
  one-shot-at-creation strip the inventory rule allows. No new wire field: the roster
  already carries `spawned_before` per character and marks the active one, both already
  exposed to script — the server's own answer. **This does NOT make money persist** — a
  stale capture is still stale; it stops the bug being destructive while that is settled (a
  balance that fails to save can be repaired from the ledger; eddies taken off a player
  every spawn cannot even be noticed). The four `[MONEY]` boundaries on the chain capture →
  send → record → save → reload → apply have STILL never produced data — they postdate every
  session in the logs. One session with the current build settles it.
  The instrument-before-touching demand (capture -> send -> record -> save -> reload ->
  apply) is answered: four `[MONEY]` boundaries are instrumented, but they postdate every
  session in the logs and have NEVER once produced data. They distinguish "never
  recorded" from "recorded and overwritten", which need OPPOSITE fixes - the first live
  session with them running decides which fix to write.

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

- **"I am not the character I made" — root cause found 2026-09-02: TWO independent faults
  stacking, one still open.** Cam's report: *"the character we created would not be the
  character we play as, it is also not phantom veronica"* — a third person entirely.
  - **(A) FIXED 2026-09-04 by REMOVING THE CHOICE — not by the plan of record, and the
    reason matters. COMPILE-CHECKED 2026-09-04 (`CheckScripts.ps1`, real 2.31 install).** `OwnSave` loaded "the newest save that
    is not `MultiplayerStart`", which is not an identity, it is an accident of file order; on
    2026-09-01 it took `AutoSave-12`, a throwaway female Corpo from a probe two days earlier,
    and ANY newer save would have won — another test character, a singleplayer session,
    anything.
    **The planned fix was DROPPED deliberately.** It was "name a save per character
    (`ManualSave(saveName)`) and load by matching the name", and it needs the mod to WRITE
    saves. `343b912` closed that door hours earlier on purpose: `SaveLocksManager` is held for
    the whole launcher session because a local save is a second copy of a server-owned
    character, and *save with the money, spend it, load, spend it again* is the exploit that
    follows. Naming saves per character would have punched a hole in a rule Cam asked for
    personally, to solve a problem with a cheaper answer.
    **What landed instead: the template is loaded ALWAYS, matched BY NAME.** Identity comes
    from the server, which was always the design — `HasCharacter()`, `GetCharacterName()` and
    the appearance restore all run before the load does. No file order, no newest-save race,
    nothing to name. `MpLoadOwnCharacterSave` is renamed `MpLoadMultiplayerWorld` because the
    old name now describes the opposite of what it does.
    **Honest costs, both recorded rather than discovered later:** a returning player's own
    singleplayer world progress no longer enters a session (nothing consulted it — the server
    owns position, possessions, money and world facts — and it could not have advanced anyway
    with saving locked), and **the template's Phantom Veronica identity bleed is NOT fixed
    here** — `MpStarterSettlement` and the appearance restore are the two fixes in flight for
    that. What changed is that the bleed now comes from ONE known source on every machine
    instead of whichever save a player happened to have: a bug you can reproduce rather than a
    coin flip. Fallback if the template is missing from the list logs LOUDLY and takes the
    newest save, because a player still needs a world.
  - **(B) FIXED**: the stacked second fault — a failed appearance restore destroying its own
    input — see the appearance-restore entry.

- **Appearance restore's contaminated-blob loop (older notes, still true)**: the restore runs
  on characters it promised to skip — log says `NEW character - possessions discarded, restore
  will run empty` then immediately `Deserialize ccstate COMPLETE - body is female`, applying
  the stored blob over the character the player just made. The stored blob IS the contaminated
  one (9141 bytes, female) and is re-uploaded on every autosave and every disconnect (`sent
  9141 bytes of appearance to the server`), so the bad record keeps refreshing itself. The
  commit is inert — why nobody is visibly Veronica: `ReFinalizeState`/`FinalizeState` are
  refused in gameplay, `InitializeOptionsFromFinalizedState` is accepted and does not rebuild
  the body — the restore "succeeds", changes nothing, and still gates saving
  (`MaySaveCharacter` refuses on a FAILED restore). Cam, from the field: "im not the character
  i just made, im some man but not my male character." Do NOT re-attempt the creator-only
  commit functions (`InitializeState`/`ReFinalizeState`/`FinalizeState`) — all three are
  measured and refused.

### Manifest system: built, waiting on two keys and one deploy
- **Cam's signing key: MINTED 2026-08-30 (`node tools/manifest/keygen.cjs`), deliberately
  PAUSED — do not half-resume.** Public half
  `ed25519-public:l9q5uBPf2IRZr1wyVzRCDIvF6LQdMl9r86VQUpyx89c=:38d98b61`, NOT yet pinned in
  `MANIFEST_PUBKEYS` (main.js, next to zeldfep's `882c415a`, which is pinned and
  unaffected); secret parked at `~/.nco-manifest-key.paused`, moved OFF
  `~/.nco-manifest-key` (the path Ship.ps1 reads) on purpose. Key renamed back WITHOUT the
  pin kills every future ship at Ship.ps1:908's verify; signing before players hold the pin
  reads as BAD SIGNATURE, not "unsigned" — main.js:867 refuses Ready and never falls back —
  locking every player out. While parked, -Mod ships warn and go manifest-less (migration
  state, by design): v0.3.107 through v0.3.113 ALL ship without `server-manifest.json`, so
  players get no manifest verification yet — not new, and the key fixes it permanently once
  resumed in order.

- **FIRST MANIFEST SHIPPED (2026-09-04, v0.3.114)**: `server-manifest.json` + `.sig`
  are on the release, signed by zeldfep's key (`882c415a` - pinned in every launcher
  since v0.3.97, so verification is immediate; Cam's key stays PAUSED per its entry).
  Launchers now verify instead of "manifest absent - legacy path". Server-side arming
  (copy the manifest into each server's `config/`) is now ACTIONABLE - do it at a
  quiet moment AFTER most players are on v0.3.114, since the digest gate refuses
  mismatched installs at the door.
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

### Migration (server + Claude, weekend of 2026-09-05)
- **`docs/MIGRATION.md` is the checklist** - written 2026-09-04 from a survey of the live
  box, not from memory. The rule it exists to state: **git carries the code and the deploy
  machinery and NONE of the state or secrets.** Hand-carry list, per deployment:
  `config/` wholesale (players.json is the big one - characters, money, positions,
  contacts; plus the `discord-bot-token` SECRET and server.json's admin password),
  `coord-data/` on the live box (updates.jsonl is the real feed record, participants.json
  holds every bearer KEY), `.env` (TS_AUTHKEY + admin password), and the cron lines.
- **GAP FOUND AND CLOSED: the test deployment's `docker-compose.override.yml` was
  UNTRACKED** - three lines (container names, tailnet hostname, image tag) that are the
  only thing making a second deployment separate rather than a collision, reproducible
  from nowhere. Template now committed at `docs/deploy/docker-compose.authority.yml`;
  the live file still belongs beside its deployment.
- **Tailscale node identity does NOT move.** New hardware = new tailnet nodes, so the
  published address changes: update `publish/server.json` (every launcher fetches it from
  releases/latest) and re-issue the invite. The launcher checkup's server-target row is
  the fastest way to confirm players followed.
- **The signing key is the irreplaceable one.** `~/.nco-manifest-key` (keyid `882c415a`)
  is pinned in every launcher since v0.3.97; losing it stops signed releases until a new
  key is pinned and shipped two releases apart (see the manifest entry).
- **Claude migration needs nothing from a machine.** Both streams' durable context is in
  git - `CLAUDE.md`, this map, MANIFEST-ARCHITECTURE.md, CRASH-FIX-BRIEF.md, MIGRATION.md.
  A stream's memory directory is a CACHE of what those already say; carry it if convenient,
  never as a source of truth.

### Tailnet ACLs: the invite is public ON PURPOSE, and ACLs are what make that safe (2026-09-06)
- **The invite in `publish/server.json` cannot be gated, and that is structural.** The game
  server is reachable only over the tailnet, so a player who has not joined yet cannot reach
  ANY gate we could put in front of the invite — including the coord API, which is itself on
  the tailnet. Serving it behind a role check would lock out exactly the people it is for.
  *(This entry exists because that fix was proposed, half-shipped in `2a648bf`, and reverted
  in `d4b2172` once the reasoning was checked. Do not re-propose it.)*
- **So secrecy is not the control. Scope is.** BEFORE: the tailnet ran the Tailscale default,
  `{"src":["*"],"dst":["*"],"ip":["*"]}` — anyone who found the invite could reach the NAS,
  both game servers on every port, and every member's personal machine.
- **AFTER (applied, validated, and previewed):** owner and admins keep everything; everyone
  else reaches `nco-live` + `nco-test` on **11778 and 11780 only**. Confirmed with the API's
  own preview for a plain member — four destinations, nothing else. 11780 is included because
  the launcher lets dev-role users fetch their key from it, and that endpoint is bearer-key
  gated on its own.
- **Hosts are named, not raw IPs, in the policy** (`nco-live`, `nco-test`) so a re-registered
  sidecar is a one-line edit rather than a hunt.
- **Rollback is off-tailnet**: `api.tailscale.com` and the admin console are public, so a bad
  policy can always be reverted even if it locks the tailnet. That is why this was safe to
  apply directly.
- **HOW PLAYERS ACTUALLY REACH THE SERVER: a multi-use DEVICE SHARE, not tailnet membership.
  This was undocumented and it nearly cost every player their access during the migration.**
  - The server node is shared with a multi-use invite (`POST /api/v2/device/<id>/device-invites`,
    body is an ARRAY - an object returns `cannot unmarshal object into Go value of type
    []controlapi.deviceInviteRequest`). People accept it and become **shared users of another
    tailnet**, NOT members of this one. On 2026-09-06 the old live node carried 10 invites, 8
    accepted: kozziofficial, coreyh2197, **ofmiceandcam98-eng (Cam)**, Phonix96, darwin.809,
    rimtek.ds, mrplasticface, minecraftian876. The old test node carried 4.
  - **ACCEPTANCES DO NOT TRANSFER.** They are bound to a device id, so new hardware means new
    nodes means everyone re-accepts. New multi-use shares were created on both new nodes.
  - **THE TRAP, and it is the reason this entry exists:** shared users are `autogroup:shared`,
    NOT `autogroup:member`. The first ACL draft granted only `autogroup:admin` +
    `autogroup:member`, which would have cut off all eight - including Cam, whose assistant
    stream reaches the coord API this way. The previous policy was the Tailscale default
    `{"src":["*"]}`, which covered them invisibly. Caught before anyone reconnected.
  - **Tailscale's ACL preview CANNOT verify this.** `acl/preview?type=user` returns no matches
    for a shared-in user even under a policy that grants them, because they are not in this
    tailnet's user list. So the autogroup cannot be proven correct from the API. The applied
    policy therefore ALSO names all eight logins explicitly - belt and braces, so a wrong guess
    about the autogroup cannot lock anyone out. **Remove the explicit names only after somebody
    has actually connected and proved `autogroup:shared` works.**
  - Naming note: the new nodes are `nco-server-1` and `nco-test-server-1` in MagicDNS, because
    the retired nodes still hold `nco-server` and `nco-test-server`. Deleting the old devices
    frees the names.
- **DECIDED 2026-09-06 (zeldfep), NOT BUILT: "Join the server's network" must be gated on
  DISCORD ROLE.** The invite button lives in the launcher's TOOLS panel and today opens for
  anyone who clicks — `ipcMain.handle('tailscale:invite')` has no check of any kind. It should
  hand out an invite only to someone whose Discord role says they belong.
  - **The launcher already holds everything needed.** It completes Discord OAuth before the
    tailnet is ever required (the trail logs `token present, name <player>` at launch), and it
    already resolves roles for the dev panel. So the check is a role test on an identity that
    is in hand, not new plumbing.
  - **Client-side alone is NOT the fix, and this is the trap to avoid:** `server.json` is
    served from `releases/latest/download` with no authentication, so anyone can read the
    invite out of it whatever the button does. A UI check is a courtesy, not a control.
  - **The honest architecture is two halves.** (1) The launcher checks the role before
    offering the button — stops the accidental case. (2) The invite stops being a static field
    in a public file and is issued per-request by an endpoint that verifies the Discord token,
    which must live OFF the tailnet, because someone who needs an invite cannot reach anything
    on it. That endpoint is the piece that does not exist yet and needs public hosting.
  - **Until both exist, scope is the control, not secrecy** — see the ACL entry above. A
    leaked invite buys reaching the game servers on two ports, nothing else.
- **STILL OPEN — the permanent fix for public distribution.** Rotation is the only thing
  limiting a leaked invite today, and it is manual. The durable answer is a PUBLIC endpoint
  (not on the tailnet) that checks the Discord token the launcher already holds before handing
  out an invite — the launcher's Discord sign-in happens *before* the tailnet is needed, which
  is what makes it the one gate that can work. Needs somewhere public to host it, which this
  project does not currently have. Until then: keep the invite single-seat and rotate it when
  consumed; the launcher picks up the new one on its next start with no ship.

### Operational debts
- **"Built and pushed" is NOT "deployed" - three surfaces, each of which bit once on
  2026-08-28.** Every time, a correct fix looked broken because the thing under test was not
  the thing that was built, and each cost a full test round-trip with Cam. Verify the artifact
  contains the specific change, **by string, not by timestamp**, before asking anyone to test.
  1. **Redscript does not ship with the build.** `xmake install -o distrib Client` prints
     "install ok!" and **leaves edited `.reds` at their previous contents**. Force-copy, then
     check the deployed file. Never mirror/`/PURGE` - `distrib` legitimately carries
     `World\CharacterProfile.reds` and `World\KiroshiScanner.reds`, which are not in `code`.
     `Ship.ps1` is safe (line 343 force-copies, 346-348 verify), so releases were never
     affected - only the manual path. **`tools\DevInstall.ps1` is the dev install path and it
     COPIES** - it mirrors `.reds` from source and verifies arrival, which is what closes this
     surface. The older symlink-the-DLL setup this entry used to describe is RETIRED and must
     not be recreated: see the symlinked-plugin entry above and CONTRIBUTING.md.
  2. **Installing a release replaces the dev build.** A fix built minutes earlier is gone and
     the game logs behaviour from code no longer in the tree. Re-run `DevInstall.ps1` after
     any launcher install.
  3. **A fix that exists only in git reaches nobody.** The appearance fix (`e795a52`) was
     committed, pushed to main, and verified in the local build - then v0.3.113 shipped
     without it, because the release had already been cut. The launcher can only deliver what
     is in a release.

- **Manifest is signed on the RELEASE but not armed on any SERVER.** The old "no signing key"
  debt is CLOSED - the key exists, is pinned, and v0.3.114 shipped `server-manifest.json` +
  `.sig` (see the manifest section). What remains is the server half, and it is MEASURED, not
  assumed: the live server's status endpoint answers `"ManifestVersion": "", "Release": ""`
  (checked 2026-09-04), so no deployment has been given a copy of the manifest and the
  digest gate is not running for anyone. Absent file = checks disabled, by design, so
  nothing is broken - it is simply not on yet. Arming is a copy into each server's `config/`,
  and the map's advice stands: do it once most players are on v0.3.114, because the gate
  refuses mismatched installs at the door.

- **Server list: DIAGNOSED AND FIXED 2026-09-06. It was also a REMOTE KILL SWITCH.**
  - Symptom: `Server could not reach the server list! Could not establish connection`, every
    60s, both deployments — live for hours, and 1s after boot on a clean rebuild of the test
    box, which is what proved it systemic rather than a stale binary.
  - Cause, VERIFIED: `ServerListSystem.cpp` hardcoded
    `https://cyberpunk.skyrim-together.com` — upstream Tilted Phoques' master server.
    **That subdomain has NO DNS RECORD** (`getent hosts` from the NAS *and* from inside
    `cyberpunkmp-server`); parent `skyrim-together.com` still resolves to Cloudflare, so it
    was RETIRED, not broken. Nothing was ever wrong on our side.
  - **The real find: `if (response->status == 403) GServer->Kill();`** — a third party we
    forked away from could shut down every server this project runs. Inert only because the
    DNS is gone; a re-pointed, re-registered or squatted subdomain kills every deployment at
    once, and during a migration that reads as the migration failing.
  - Fixed: endpoint is `Config::ServerListEndpoint`, **default EMPTY = do not announce**
    (checked before the thread spawns, so no detached thread per minute and one info line
    instead of an error forever). A 403 now sets `m_refused` and STOPS announcing — a list
    refusing us is a reason to leave that list, never to disconnect people who are playing.
    `m_refused`/`m_announcedDisabled` are `std::atomic` because the announce runs on a
    detached thread (this project has already lost a day to a "cannot happen" data race).
  - Discovery never depended on it: `publish/server.json`, fetched from `releases/latest`.
  - **Do not re-point this at a public list without deciding what a 403 should mean.**

- **Live server runs feat-built code while `main` lags** — the cron half is FIXED: the NAS
  cron now pulls `origin/feat/world-state` (remote `ofmiceandcam98-eng`, confirmed live
  2026-09-03 via `~/nco-update.log`), so deploys and cron finally agree; the old "cron
  tracks main" note is dead. The remaining debt is `main` itself: untouched at `304b492`
  while feat sits at `5a68517` (2026-09-02). Merge feat→main at a stable point; until then,
  anyone "correcting" the production checkout back to main rolls the live server BACKWARD.

- **Nexus SSO application** still unanswered (publish/nexus-sso-request.md) — manual
  API-key paste remains the sign-in path.

- **Server password has wire+server support but no launcher UI** (settings.serverPassword
  is settable by hand only).

- **Puppet record flip** (mannequin → Character.Jackie/WaPanam story rigs) still waits
  on live validation via `-puppet-record` / `/npc`.

- **reason 4 disconnects** are abrupt game closes, not crashes — cosmetic, but mapping
  GNS close reasons better would stop them reading as failures in every log review.

---

- **Server code is only ever checked against MSVC — the Linux/GCC build is locally
  unverifiable, so portability breaks are invisible until the NAS rebuild.** The server
  runs GCC in a container; the dev machine has no GCC, no WSL distro, and Docker is not
  running (and Cam does not want to use it). MSVC supplies headers transitively that
  libstdc++ does not. 2026-09-02 lesson: four new files called `std::snprintf` with no
  `<cstdio>` and `ChatSystem.cpp` used `std::map` with no `<map>` — fixed in `dcab568`,
  but NOT confirmed to be the actual build failure: `CharacterRecord.h` has done exactly
  the same since 2026-08-20 and builds fine, so the transitive include is evidently
  available somehow. The real error is still a guess; anyone with a Linux box settles it
  in one command. The NAS rebuild launched 2026-09-03 (when the deploy unblocked) IS the
  missing GCC check for this code — its verdict lands in `~/nco-update.log` as
  `deployed`/`BUILD FAILED` with the real compiler error either way. Read that before
  guessing further.

- **World-template plan (Cam, 2026-08-28), not started**: phantom Veronica propagates WORLD
  state and nothing else — doors she opened stay open for everyone (housing and vehicles
  excluded), quests she finished count as finished for everyone, then quests off entirely;
  all gated on Dogtown being open. Mechanism already exists: `WorldFact` on
  `SpawnCharacterResponse.facts`. First pieces landed: `06af8b5` (open Dogtown on a fresh
  deployment), `51756fc` (stop quest calls at `PhoneSystem`).

- **v0.3.114 SHIPPED 2026-09-04 (zeldfep's machine, full -Mod ship).** Carried: the
  world-contract connection fix (`f2d2a55`), the post-flag-day protocol (the 09-03/04
  server deploys had moved both boxes past `6ac90d1`'s protocol-message removal, so
  EVERY v0.3.113 client was refused at the door until this ship - live-verified by
  zeldfep's own denial popup), mod-folder auto-derivation, install/remove trail
  tracing, character slots + selector, phone calls, the new menu. Ship ran on a
  game-less machine: CYBERPUNKMP_GAME_DIR stub satisfies the assert, the scc check
  soft-skips, and distrib\launcher\mod\Rpc must be assembled by hand (extracted from
  the previous payload; 9/10 stubs byte-match the repo, RedTypes.cs is the generated
  aggregate).
  `Ship.ps1 -Mod` does NOT cut a new version — it republishes mod assets into whatever
  release is already `latest`, so THREE different `ModPayload.zip` builds now exist under the
  tag `v0.3.113` (28 Aug, and two on 30 Aug). Players still update correctly because the
  launcher compares the ASSET ID, not the version string, but `.nco-version` reads the same
  number for three builds — which will mislead the first bug report that quotes it. Cutting
  v0.3.114 needs a `## What changed - v0.3.114` section in `publish\release-notes.md` and a
  FULL ship (which also republishes the 103 MB installer). Held on the same 2026-08-30 budget
  call as the signing-key pause.

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
| Scripts | `code/assets/redscript/` | MainMenu (join arming), Death.reds (immortality + floor + menu backstop), Combat.reds (hit hook, weapon poll, quickhack requests), Hackable.reds, Difficulty.reds (pins Very Hard for every connected player — `d5d506f`, `77971ee`), World/*.reds | redscript is ONE compilation unit — one broken file boots the game with no scripts at all. **No hex literals** (`0xFF...` is a parse error that kills every script). Match integer widths exactly — `GetMagazineAmmoCount` returns Uint32, and mixing it with Int32 is NO_MATCHING_OVERLOAD. Difficulty.reds reads the difficulty index BY NAME rather than hardcoding 3 — keep it that way |
| Combat | `code/server/native/Game/Level.cpp` (handlers) + `Components/{Health,Weapon,Quickhack}Component.h` + `code/assets/redscript/Combat.reds` | Detect → validate → broadcast → apply. Server owns health, magazine, RAM pool | **The game computes, the server bounds.** Weapon damage, quickhack damage and RAM cost all come from the client because they are native calculations needing a live StatsSystem — `GetCost()` runs `CalculateStatModifiers` against the attacker's deck and perks and can include a RANDOM modifier. Quickhack damage MUST stay 0 in the rule table: Cyberpunk applies it through the ordinary hit pipeline, so a number there double-counts (the v0.3.104 bug). A TweakDBID is **CRC32** of the name + length in bits 32-39, not FNV — guarded by a static_assert against a value dumped from the game |
| Making players targetable | `code/assets/Tweaks/CyberpunkMP.tweak` + `Hackable.reds` | `objectActions` on the puppet records; hostile attitude at spawn | **`MaMuppet`/`WaMuppet` inherit from `Character.Panam`, NOT from `Character.Muppet`** — editing Muppet does nothing. Quickhack action names in the game's scripts are WRONG (`BaseBlindHack` not `BlindHack`, `MadnessHackBase` not `MadnessLvl3Hack`) — they were dumped live. Hostile attitude satisfies BOTH gates: `Att_Hostile` for `TSF_EnemyNPC` and the fourth route to `IsAggressive()`. The entity templates were never missing targeting components (16 `gameTargetingComponent`s, confirmed via WolvenKit CLI). Behind `--hackable-puppets` |
| Runtime inspection | `bin/x64/plugins/cyber_engine_tweaks/mods/nco_hackdump` (not in repo) | Dumps TweakDB data the game will not reveal statically | CET only honours `registerForEvent` from `init.lua`; a required module's registration is ignored. Mod globals are NOT reachable from the console — export by returning a table. `io` is sandboxed to the mod folder. **Lua output goes to `scripting.log`**, not `cyber_engine_tweaks.log` |
| World/asset editing | External tool, not in repo: [WolvenKit](https://github.com/WolvenKit/Wolvenkit/releases) | Editor + CLI for the game's own resource formats (`.ent`, `.mesh`, `.app`, world/sector nodes, TweakDB) — the tool for any world-building, level-editing, or static-asset-inspection work, not just confirmation checks (already confirmed the puppet templates' `gameTargetingComponent`s statically — see the targeting row). **NAS updater LIVE 2026-08-29**: `tools/deploy/update-wolvenkit.sh` in `truenas_admin@100.90.85.33`'s crontab (`0 * * * *`, self-throttled to ~72h internally — cron frequency and check cadence are deliberately decoupled, see the script's own header); seed run succeeded — `~/wolvenkit-console/VERSION` reads `8.20.0`, `~/wolvenkit-console/current/` holds the full `WolvenKit.ConsoleLinux` extraction. The script was hand-seeded on the box first (live wiring `ee12df2`) and is NOW TRACKED (`dcc67eb`) — the leftover untracked copy refused the NAS pull for hours until shelved to `~/update-wolvenkit.sh.shelved-20260903` (2026-09-03); the cron LINE still lives only in the crontab, so a rebuilt box needs it re-added by hand. **Local CLI, built from source 2026-08-30 — the route on Cam's PC to READ an asset now**: no installed WolvenKit there, only `C:\Users\Cam\Downloads\WolvenKit-main.zip` (source, 89MB); extract, then `dotnet build WolvenKit.CLI\WolvenKit.CLI.csproj -c Release` — needs the .NET SDK, and **the CLI targets `net10.0`**, so the exe lands at `WolvenKit.CLI\bin\Release\net10.0\WolvenKit.CLI.exe` (Cam's box has SDKs 6/8/9/10, so it builds; a box with only 9 will not). Decode with `convert serialize <file>` (writes `<file>.json` beside it; `convert deserialize` goes back). Proven use: decoding `prototype_hud.inkhud` + `multiplayer_ui.inkwidget` exonerated the asset in the invisible-chat-box bug BEFORE anyone "fixed" a file that was never at fault (see the SOLVED chat-box entry) — `multiplayer_ui` is field-for-field identical to `new_phone` apart from `ignoreHudScaleOverride`, and the chat canvas matches its authored state exactly (1000x1000, `Fixed`, `Fill/Fill`, opacity 1) | JSON traps that cost time: entry names are at `hudEntryName.'$value'`, NOT `.hudEntryName`; a widget library item's tree is at `item.package.Data.File.RootChunk.rootWidget`; and `rootWidget` is often `{"HandleRefId": "N"}` pointing at a `"HandleId": "N"` defined elsewhere in the same package — plain property walks fail, search the raw text for the id. **Version pin**: WolvenKit versions track specific game patches — check the releases page for the version matched to 2.31 before use; a mismatch can misread or corrupt resource formats it does not recognise. Read-only inspection (CLI dumps) is low-risk; anything that WRITES a resource file is engine-pin-grade — verify against 2.31 first |
| Launcher | `code/launcher-lite/main.js` | Discord identity (membership: only 200/404 are verdicts), roles (10-min memo), manifest state machine, install lock + queue, Nexus manager, game detect (A–Z drives), footprint/uninstall | **CSS specificity**: base `button.action` (0,1,1) beats bare class rules — trio overrides must be `button.action.x`. Electron packaged: new source files MUST be added to package.json `build.files` (v0.3.97 shipped importing a file it didn't contain). **Uninstall is a two-layer mirror**: footprint in main.js AND `build/installer.nsh` — a new write location goes in BOTH. The `nxm://` class is cleared only when its command points at OUR exe (Vortex/MO2 write the same key; empirically tested both ways 2026-08-22) |
| Manifest kit | `code/launcher-lite/manifest.js` (+ selftest) | Signature verify vs pins, §2.1 availability states, install digest, ownership index, unmanaged classifier, tailnet check | Pure functions, Electron-free; run `node manifest.selftest.mjs` (82 checks) before shipping launcher changes |
| Ship tooling | `tools/Ship.ps1`, `tools/manifest/*.cjs` | Gate battery, staging, carry-forward, manifest generate/sign/verify-vs-pins, prerelease→verify→promote | Ship bumps package.json but never commits — carry the bump or the next ship collides with an existing tag and silently uploads into an old release. `Ship.ps1:574` copies `distrib\launcher\mod\assets` wholesale and never cleans it — anything left in `assets\Archives\` ships to every player, so keep probes and experiments out of `distrib` |
| Deploy | `tools/deploy/update-server.sh` | NAS cron: player-count gate + server-relevant-path filter + untracked-file shelving | **Deploys whatever branch the checkout is ON — production `/mnt/vol/projects/CyberpunkMP` is on `feat/world-state`, not main** (verified 2026-08-22). The repo dir is the script's first ARG and defaults to `~/CyberpunkMP`, which is not where production lives — calling it without the arg fails with "no such directory". Two traps beyond that: it DEFERS while Players>0 (so a deploy can silently not happen), and it skips the rebuild when no server-relevant path changed — a docs-only commit logs "pulled, nothing changed" and leaves earlier unbuilt server code still unbuilt. Verify a deploy by checking the running binary for a symbol, never by reading the log. **Untracked files kill pulls**: git refuses to overwrite an untracked file with an incoming one — three kills so far: the two coord-api publish files, then `tools/deploy/update-wolvenkit.sh` (hand-seeded on the box as the WolvenKit updater's live wiring, `ee12df2`, later committed to the repo, `dcc67eb`). Fingerprint in `~/nco-update.log`: `updating 3cde271 -> <new tip>` then `pull failed` every 10 minutes for hours. Diagnosed 2026-09-03 (zeldfep stream, from the NAS shell) — the remote (`origin` = ofmiceandcam98-eng, tracking `origin/feat/world-state`) and the fetch were both correct; neither suspected candidate was the cause. The byte-identical local copy was shelved to `~/update-wolvenkit.sh.shelved-20260903`, pull unblocked, checkout at the tip. HARDENED against the class, not the instance: the script now shelves ANY untracked file the incoming commits are about to create, with a loud log line naming it. Tracked local modifications still fail the pull ON PURPOSE — that is real divergence and deserves a human; do not "fix" it. Last trap: after a MANUAL pull the cron sees LOCAL==REMOTE and skips the rebuild — launch the rebuild yourself (why the 2026-09-03 rebuild was launched immediately by hand) |
| Coordination | `code/coord-api/`, `publish/assistant-updates.json` | The feed both Claude streams post to; dev-key handout | Personal key `~/.ncoa-coord-key`; posts as "zeldfep (Claude)" |
| Published surface | `publish/` | server.json (address, republished by workflow), modlist.json (curated Nexus list), roles.json (written by server), manifest-source.json (curated components), release-notes.md (EVERY release's body), fullinstall-base/ | All fetched from `releases/latest/download/<name>` — a launcher-only ship must carry mod assets forward or every launcher 404s (v0.3.1 lesson, automated since) |
| Ship-gate verify | `tools/Verify.ps1` + `tools/tests/` | The pre-ship check battery; every check maps to a failure that has actually happened here, never a category of bug in the abstract: **BOM** (`Set-Content -Encoding UTF8` on PS 5.1 writes one — breaks redscript with "syntax error at 1:1", which names nothing) · **natives vs RTTI** (a native with no `RTTI_METHOD` fails at LOAD, taking every script in the mod down) · **duplicate dispatch** (the `/call` bug: an older deprecation stub at ChatSystem.cpp:2785 matched first and returned, so `/call 555-014-372` answered "use your phone" and rang nobody while the new player-to-player call dispatch at ChatSystem.cpp:3656 sat unreachable dead code — fix is merge into one block or rename one command, deciding which behaviour is wanted FIRST since the live one is whichever wins at 2785; comparison is by INDENTATION, because a nested branch in a compound `if` is not a duplicate) · **requests all handled** (`CreateCharacterRequest`: declared, took a oneof slot, never sent or handled) · **stores + ticks wired** (a store never `Load()`ed silently holds nothing) · **unit tests** (103 checks in `tools/tests/`: seats, calls, trading, permissions, contact migration) | Every failure prints three fields — **what** it costs, **where** it is, **fix** — because two assistants work this codebase from separate sessions and a bare `FAIL` costs whichever one picks it up a fresh investigation of something the check already knew. Self-tested: a bogus native was planted, the failure rendered all three fields, the file was restored to a clean run. **Tests stay IN THE REPO** — the first set was written in a scratchpad and wiped by temp cleanup, turning "the tests passed" into somebody's word rather than something anyone could re-run; do not move them out |
| Permissions | `code/server/native/PermissionLevel.h` (ladder) + `Config.h` (role-name→level) + `ChatSystem.cpp` (command gates) | Staff ladder kPlayer 0 / kSupport 5 / kModerator 10 / kEventStaff 15 / kAdmin 20 / kOwner 30; Discord staff set since 2026-09-02 (Cam replaced every moderator/admin role): dev, SENIOR MODERATOR, EVENT STAFF, MODERATOR, support | **Levels resolve from the LOWERCASED role NAME, and a name not in the list lands on kPlayer silently.** When the roles changed, `senior moderator` and `event staff` granted nothing at all — and `staff` matched while `event staff` did not, a near-miss that reads as the role simply not working rather than a missing string (`5a68517` added both). **If a role stops working, look here first** |
| Voice | `code/client/App/Voice/` (VoiceAudioManager, VoiceClient) + launcher device picker (main.js) + `Settings.cpp` arg filter | Capture/encode/route/playback; device choice travels as `--voicein=` launch arg | **"It was never broken — the microphone was never opened" (`0ca11ba`).** Two sessions of logs said `mic NOT CAPTURING / speakers ok - encoded 0` two hundred times — `encoded 0` means nothing was ever captured, so codec, network, routing and playback were all innocent; the same WASAPI sequence run standalone on Cam's machine worked perfectly (Apollo Solo, 48kHz, **10 channels**, 147,360 frames, peak 0.98), so the failure had to be before `Start()`. Cause: `voiceInputDevice = "communications"` — `default` and `communications` are the two sentinel ids **Chromium's `enumerateDevices()`** returns for system defaults; the launcher's picker is a web page, the launch-arg code filtered only `default`, and `--voicein=communications` reached the mod, which treats any non-empty id as a literal Windows endpoint id (`{0.0.1.00000000}.{guid}`). **The real lesson is the asymmetry, not the sentinel**: the OUTPUT branch falls back to the default when a lookup fails and the CAPTURE branch does not — the same bad value was survivable on one side and fatal on the other. Why nobody could see it: `StartCapture` returns when the THREAD IS SPAWNED, not when a device opens (it cannot wait on a driver without blocking the connect path), so every failure happened after the caller was told `true` and logged `[Voice] running`, and every error went into a string nothing printed. Fixed independently of the sentinel: `SetError` logs at the point of failure, the stats line prints the reason, a successful open logs the device format, and `[Voice] running` no longer claims what it cannot know. Both sentinels now read as "follow Windows" on BOTH sides and the launcher stops emitting them. A genuinely unplugged saved device still does NOT silently switch the microphone — that decision stands, it just says so out loud now |
| Clean start & quest gating | `code/assets/Archives/packed/archive/pc/mod/zz_NightCityOnline_CleanStart.archive` (8 KB, second archive — ArchiveXL is handed the whole directory) | The multiplayer game start. A start is a `.gamedef` — root quests + spawn tag + world, and the game ships 824 of them. Ours overrides `ep1\quest\ep1_standalone.gamedef` (THREE independent root quests, only one of which is the story) with `cyberpunk2077_ep1_standalone.quest` (base Night City, prologue skipped) + `ep1.quest` (CDPR's non-standalone EP1 root: sets `ep1_installed=1`, enters `ep1.questphase` at `Base`, no story) + `ep1_preorder.quest` — nothing authored. Shipped `c1518c4` 2026-08-30. Proven pre-ship: fresh character with empty quest log, not one EP1 fact (`ep1_standalone=0 ep1_active=0 q301_active=0` against 1/1/1 in the control), no Songbird call; `ManualSave-132` had a completely empty fact list. CONFIRMED WORKING 2026-08-30, shipped build, Cam's machine — spawn signature `ep1_installed=1` (only `ep1.quest` sets it — proof the EP1 world entry took), `ep1_standalone=0 q301_active=0 q301_done=0`, no Songbird, empty quest log, Dogtown still reachable: Cam's brief ("we should just spawn in the world, no opener, no songbird dialogue, no quests, nothing") reached without completing, skipping or failing a single quest. Wider field testing still owed | A spawn tag is content OWNED by its quest — the first probe kept `#q301_spwn_ep1standalone_opener` while deleting the quest that defines it and players FELL THROUGH THE MAP; now `#q000_spwn_start` (the base game's own — a holding room, not a place; the server moves new arrivals on connect, `/setstart`); every shipped gamedef pairs its spawn with its own quest. Level 15 came from the story root — clean-start characters are level 1 with no base prologue quests marked done, so the server must grant progression (it already grants the starter kit and eddies). The forced Songbird prologue was a CHARACTER-START problem, not a phone problem: `MainMenu.reds` fires `SpawnEvent(n"OnNewGame")` and `preGameScenarios.script:308` routes to the EP1 branch whenever Phantom Liberty is installed — every pre-clean-start character was a PL standalone start (level 15, `ep1_standalone=1`, `q301_active=1`); `51756fc` genuinely stopped every phone call (zero presented in a full session) and the conversation still happened, because a quest drives the scene — suppression cannot stop a running quest, prevention is the only route. Dogtown is SOLVED, stop looking for a quest: the gate is quest fact `ep1_side_content >= 1`, a pause condition in `ep1\openworld\combat_zone_gate\combat_zone_gate.questphase` that references NO q301 fact at all (confirmed in game 2026-08-29 — Cam drove AND walked through the border with `q301_done=0`); a live server takes `/fact ep1_side_content 1` once, `06af8b5` seeds it for fresh deployments — do NOT complete q301 to open Dogtown |
| Chat / admin commands | `code/server/native/Systems/ChatSystem.cpp` | Slash commands; `/rename <character-id> <new name>` for admins is IMPLEMENTED (ChatSystem.cpp:1513) | `/rename` is `kAdmin`-gated, keyed on the CHARACTER id, and deliberately does NOT clear `NameChosen` — it repairs a name rather than handing out a fresh naming attempt; do not "fix" it to reset the flag |

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
`/mnt/vol/projects/CyberpunkMP-authority`, `docker compose -p nco-authority build
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

- **Verify gate — `.\tools\Verify.ps1`** (2026-09-03): every ship is gated on it —
`Ship.ps1` AND `ShipTestBuild.ps1` both run it and refuse to publish if it fails.
`.\tools\Verify.ps1` runs everything; `.\tools\Verify.ps1 -SkipTests` runs the static
checks only (no compiler needed). `-SkipVerify` is an escape hatch for a FALSE POSITIVE
only — every use of it is a bug in Verify to fix, never a route around the gate. Born of
an audit of one day's work that found two bugs that compiled perfectly, were reported as
working, and would have shipped (the `/call` double dispatch and the never-handled
`CreateCharacterRequest` — both are now permanent checks; details in the ship-gate verify
row of the code map). Every run names its own blind spots: anything needing the game
running or two players, and the Linux build — no GCC on this machine, so server
portability stays unverifiable locally.

- **Feat → live server**: the NAS cron — 10-min tick logged in `~/nco-update.log`, pulls
  `origin/feat/world-state` into the production checkout and deploys whatever branch that
  checkout is on (confirmed live 2026-09-03); defers while Players>0, rebuilds only when
  server-relevant paths changed. Cam can also deploy by hand from feat. The old "cron
  watches main" note is dead — see the Deploy row's gotchas before trusting any deploy, and
  the ledger for the main-lag debt.

## 4. IDENTITY & VERSION SURFACES (one line each)

- **One project version**: launcher `package.json` → tag `v0.3.NN`; only launcher ships move
  it; ships don't commit the bump (carry it).
- **Protocol**: the kIdentifier pair, per build; recorded in test-release notes and (once
  shipping) the manifest.
- **Mod build**: `BUILD_COMMIT` + `NCO_BUILD_VERSION` via BuildInfo.h (dev builds say
  0.0.0-dev honestly).
- **Mod-on-disk**: `.nco-version` marker + settings `installedStamp` (assetId:size — the
  only identity that survives tag-clobbering).
- **Manifest**: `manifestVersion` date.serial, monotonic client-side, THE client-facing
  environment identity once live.
- **Server**: status API `ManifestVersion`/`Release` (empty = migration).


## 5. THE GAME INSTALL AS A TOOL (surveyed 2026-09-04, zeldfep's box)

What a machine with the real game can answer that a build-only box cannot, and what is
STILL missing here. Recorded because "does this box have the game" turned out to be the
wrong question — the useful one is "which of these does it have".

| Capability | Needs | Status on this box |
|---|---|---|
| Compile redscript (`CheckScripts.ps1`) | `engine\tools\scc.exe`, from the **redscript prerequisite** | **YES** — `OK - redscript compiles` |
| Live-install a build (`DevInstall.ps1`) | `red4ext\plugins\zzzCyberpunkMP` | **YES** |
| Read VANILLA SCRIPT SOURCES | **REDmod DLC** → `<game>\tools\redmod\scripts` | **NO — not installed** |
| Read map/world geometry, `.ent`, `gameHitShapeBVH` | WolvenKit (CLI buildable from source) | **NO — not installed** |
| Verify prerequisite versions against the pins | the install itself | **YES** |

- **REDmod is the notable gap and it is free on Steam.** Every file+line citation the map
  leans on — the vehicle-damage audit (`vehicleComponent.script:79/:4543/:6304`,
  `vehicles.script:1123`, `attackData.script:219`), the phone corrections
  (`phoneSystem.script:9`, `newHudPhoneGameController.script:512`,
  `messengerUtils.script:89`), `singleplayerMenu.script:1012`, `saveLocksManager.script:29`
  — comes from those sources. **They cannot be verified or extended on this box.** Anyone
  about to do that class of work (read the game's own source rather than guess) should
  install REDmod first; it is the difference between "answered from the sources" and
  "answered from a runtime dump", which is the distinction that has decided several of the
  entries above.
- **Prerequisite versions match their pins EXACTLY**, verified against
  `publish/manifest-source.json`: RED4ext **1.29.1**, Codeware **1.18.0**, ArchiveXL
  **1.26.0**, TweakXL **1.11.1**. Also present: `input_loader`, `zzzCyberpunkMP`.
- **The game has not been RUN since the mod was installed** — `red4ext\logs` is empty. So
  the install is unproven, and the first launch is the test. First diagnostic if the mod
  appears to do nothing is that log, per the symlinked-plugin entry.
- **A live reproduction of ledger fault A is sitting on this machine**, which is worth
  keeping rather than tidying away. 23 save folders: `MultiplayerStart`, `ManualSave-0/1`,
  and twenty AutoSaves — **including `AutoSave-12`, the exact save that produced the wrong
  character on 2026-09-01**. Every one carries a timestamp inside the same two seconds
  (07:28:41–43, restored as a batch), so under the OLD "newest save that is not the
  template" rule which character you became was decided by sub-second tie-breaking. The two
  newest are `AutoSave-13` and `AutoSave-12`. That is the coin flip made visible, and it is
  why the fix removes the choice rather than sharpening it.
