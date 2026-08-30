# The Map

**What this is:** the call-back document — the ledger of everything open, in flight, or
deliberately deferred, followed by the geography of the code and the pipelines. When
something feels missed, this is where it should already be written down. Maintained by
whoever lands or finds things (both Claude streams included): landing an item removes it
from the ledger in the same commit; finding one adds it. A ledger that is not updated in
the landing commit is how items get missed twice.

Last full revision: 2026-08-22, after v0.3.106 (player combat).
Partial pass 2026-08-29 (Copilot, VM checkout): reconciled THE crash entry against
what actually shipped (v0.3.111-113) and the commits between 3bf2446 and d5d506f -
nobody had touched this file across that whole run, which is exactly the "missed
twice" failure mode this doc warns about. Did NOT re-audit combat/vehicle-damage/
manifest/modlist sections below - those are as of 2026-08-26 still.

---

## 1. THE LEDGER

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
| World/asset editing | External tool, not in repo: [WolvenKit](https://github.com/WolvenKit/Wolvenkit/releases) | Editor + CLI for the game's own resource formats (`.ent`, `.mesh`, `.app`, world/sector nodes, TweakDB). Already used once to confirm the puppet templates' `gameTargetingComponent`s statically (see the targeting row above) — it is the tool for any future world-building, level-editing, or static-asset-inspection work, not just confirmation checks. **LIVE on the NAS as of 2026-08-29**: `tools/deploy/update-wolvenkit.sh` is wired into `truenas_admin@100.90.85.33`'s crontab (`0 * * * *`, self-throttled to ~72h internally — see the script's own header for why cron frequency and check cadence are deliberately decoupled) and the seed run succeeded — `~/wolvenkit-console/VERSION` reads `8.20.0`, `~/wolvenkit-console/current/` holds the full `WolvenKit.ConsoleLinux` extraction. Deployed by copying the file directly rather than through the tracked git pull, because that checkout is on `feat/world-state` (see the Deploy row below) and does not have this file yet — if `feat/world-state` is ever reset/rebuilt from git, this script and the cron line survive independently of it, but a fresh checkout elsewhere would need both re-applied by hand until this lands on that branch too. | **Check the releases page for the version matched to game patch 2.31** before using it on this project — WolvenKit versions track specific game patches, and a mismatched version can misread or corrupt resource formats it does not recognise. Read-only inspection (CLI dumps) is low-risk; anything that WRITES a resource file should be treated the same as an engine version pin — verify against 2.31 first. |
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
