# The Map

**What this is:** the call-back document — the ledger of everything open, in flight, or
deliberately deferred, followed by the geography of the code and the pipelines. When
something feels missed, this is where it should already be written down. Maintained by
whoever lands or finds things (both Claude streams included): landing an item removes it
from the ledger in the same commit; finding one adds it. A ledger that is not updated in
the landing commit is how items get missed twice.

Last full revision: 2026-08-22, after v0.3.106 (player combat).
Ledger updated 2026-08-28 after the overnight session (v0.3.112 / v0.3.113): the flecs
data race behind THE crash was found and fixed, character-overwrite and quit-save were
fixed, the invisible chat box was solved, and three new items were opened. See the
crash row, the new **Character identity** block, and the two new operational debts.

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
- **THE crash: 0xC0000005 in the remote-vehicle-mount path - cross-machine fingerprint
  (2026-08-23 log sweep, all 9 players).** 20 access violations + 8 breakpoint exits
  across 76 recorded launches, on five different machines, and every crash with a
  shipped log dies within seconds of the same sequence: `[Interpolation] movement for
  id N but no puppet is registered` (frozen-remote warning) -> `HandleVehicleEnterMessage:
  queueing mount` -> `OnVehicleReady` -> `DoMount ... (network copy)` /
  `MakeRemoteDriven: SetKinematic ... done` -> dead in 2-10s. rimtek's is explicit: the
  frozen-warned id IS the vehicle being mounted into. Working theory: the queued mount
  fires against an entity (vehicle or occupant puppet) that is not fully built, and an
  engine vcall on it faults. Second cluster: remote APPEARANCE apply (`AddItemToSlot
  failed` x106/evening for one player) and Cam's post-spawn cyberware/inventory restore
  (dead ~2s after 'queued 70 piece(s)') - possibly the same not-fully-built-entity
  **REPRODUCED ON DEMAND 2026-08-26, and the trigger is mundane: DISCONNECT WHILE IN A
  VEHICLE, THEN REJOIN.** Cam spawned a car at 04:45:14 (`Vehicle e3 spawned by character
  5000000da (driver seat)`) and quit at 04:45:52 while still in it. The car stayed in the
  server's live flecs world - `vehicles.json` was `[]`, so nothing was persisted and
  nothing on disk hinted at it. On his 04:51 rejoin the server replayed it, and the client
  log is the whole fingerprint end to end with no other player anywhere near:
  `HandleVehicleLoadMessage` (04:51:04.588) -> `OnVehicleReady` -> `MakeRemoteDriven:
  SetIsPlayerControlled / engine vcall / engineData set / SetKinematic / done`
  (04:51:05.857) -> dead at 04:51:07.5, i.e. 1.6s after `done`. So the not-fully-built
  entity is the OCCUPANT, and the specific case is an occupant that no longer exists at
  all - the driver disconnected. That also retires the "why only Cam" puzzle: he was the
  only one rejoining into a world holding his own orphaned car. Immediate operational
  relief is a server restart (clears the live world; `Loaded 0 vehicle(s)` on boot,
  players.json untouched), but every disconnect-in-a-car re-arms it for the next joiner.
  **PARTLY FIXED AND LIVE 2026-08-26 (0e00c77, verified in the running binary):** the
  server now clears every remaining vehicle when the player count hits zero
  (`Level::ClearAbandonedVehicles`, called from `GameServer::OnDisconnection`). Parking an
  ownerless car is still right while somebody is online to see it; with nobody online it is
  only a landmine for the next joiner. Server-only, so no protocol change, no flag day, and
  it protects players who have not updated. **STILL OPEN:** A disconnects in a car while B
  stays online, then C joins - the car legitimately survives and C can still hit it. That
  needs the CLIENT fault located, and note what the evidence rules out: `MakeRemoteDriven`
  already null-checks and Cam's log shows every step completing through `done`, so a
  readiness gate THERE is a confident no-op. The fault is 1.6s later and silent; every
  reachable path in InterpolationSystem (stub gate, `TimePoints.empty()` extrapolation,
  `ahead > maxExtrapolationMs`) is already guarded. Next step is step-logging across that
  window, the same bisection MakeRemoteDriven's own comment describes - now practical
  because the repro is deterministic.
  Fix must handle a driver id that resolves to nothing, not merely a slow-to-build one.
  Needs a dedicated session in VehicleSystem.cpp: gate DoMount/MakeRemoteDriven
  on entity readiness beyond OnVehicleReady, and treat a frozen-warned id as NOT ready.
  **Test-box flavor NARROWED by experiment (2026-08-23 night session, 10 crashes):**
  ghosts purged - still crashed; parked-vehicle replay absent - still crashed; creator
  flow bypassed entirely (Initialised+NameChosen flipped server-side, guard live,
  clean spawn 03:02:29) - still crashed in 25s. Creator EXONERATED. Last suspect
  standing: the spawn-time appearance/cyberware APPLY of a FEMALE character (Cam +
  phonix female, 12-13KB blobs, both crash-prone; every male player clean; matches
  the AddItemToSlot cluster). Decisive next experiment: a female character on
  zeldfep's machine - crash there too = female-apply convicted cross-machine, fix
  goes to the apply path (defer/chunk/gate on entity readiness). **THE FIX BRIEF is
  written: docs/CRASH-FIX-BRIEF.md** - ranked code pointers (readiness gate via
  Codeware's entity-status byte, the OnVehicleReady pattern to copy, the span
  lifetime hot candidate at AppearanceSystem.cpp:497, the restore-storm seam, the
  90s-autosave outlier explanation), diagnostics to add, validation plan.
  **ROOT CAUSE FOUND AND FIXED 2026-08-28 (559828f + 0fa2bb9). It was a DATA RACE in
  flecs, and the theories above were chasing the wrong layer.** The dump is the proof -
  two threads, same instruction, same millisecond, SAME `ecs_stack_t*` in RCX, DIFFERENT
  cursors in RDX:
  `thread 152696 RIP=..C548 RAX=0x13 RCX=..387C9CF0 RDX=..A8FC9200` /
  `thread 147288 RIP=..C548 RAX=0x13 RCX=..387C9CF0 RDX=..A8FC90D8`.
  Fault address `0x25` is `RAX + 0x12` - the `is_free` byte of `ecs_stack_cursor_t`, read
  from a cursor pointer that is the integer 19. `GetServerIdByEntity` answered with
  `query<EntityComponent>().find(...)`, and the hit hook calls it from the game's JOB
  SYSTEM: a thread census logged `FindEntity` arriving from SIXTEEN distinct threads in
  one 18-minute session, none of them the thread running `progress()`. Building a query
  takes a cursor from the stage's iterator stack, which flecs leaves unlocked by design.
  Cam's repro: *"crashed as soon as i shot my gun."*
  **Explains what nothing else did:** the small integers where pointers belong, the assert
  that never fired, poisoning finding nothing, survival ranging 2s-381s. It also covers the
  vehicle fingerprint above - `VehicleSystem::OnVehicleEnter` was a SECOND off-thread
  caller, caught by a tripwire within an hour of the first fix, so mount-path crashes and
  shooting crashes were one bug entering through two doors.
  **Fix:** `App::ServerIdRegistry` (client/App/World/ServerIdRegistry.h) - lock-free
  EntityID→serverId table, written on the main thread at the same four points that already
  maintain `PuppetRegistry`, read locklessly from anywhere. flecs is off the hook path
  entirely. Lock-free rather than mutexed for the reason `PuppetRegistry`'s own comment
  records: these hooks fire for every entity in the scene.
  **Tripwire left in for one release:** `App::FlecsThread::Assert(site)` logs
  `[Thread] <site> touched flecs OFF the flecs thread`, keyed on (call site, thread) -
  keying on thread alone hid the second caller. `VehicleSystem::OnVehicleReady` is the last
  unproven caller and carries its own labelled check; it MUTATES the world
  (emplace/add/remove), so if it reports off-thread the fix is to marshal the whole
  promotion to the main thread, not to swap the lookup.
  **Lesson, because it cost a day:** the race was "eliminated" early by reading code - no
  thread in NetworkService, handlers dispatched before `progress()` - and that argument was
  wrong. Two `spdlog::warn` lines settled in one session what a day of reasoning did not.
  **STILL OPEN from the original entry:** the disconnect-in-a-car / rejoin repro with a
  third party online has NOT been re-tested since. If it still crashes it is a separate
  readiness bug and the fix brief still applies.
- **EACCES shell-fallback destroys crash telemetry** (phonix, every launch): when spawn
  falls back to the cmd/start shell, the trail's 'game exited with code' reports the
  SHELL's exit (0, after 1-4s) - his two suspected crashes recorded nothing. Fix: when
  the fallback path is used, do not log the wrapper's exit as the game's; poll the real
  process or say 'exit unknown (shell fallback)'.
- **Ghost remotes on join: SOLVED (2026-08-23) - they are /npc entries with garbage
  records.** The test server's npcs.json holds 7 NPCs; FIVE carry the literal string
  `Character.<record>` (the usage line's placeholder, typed verbatim and persisted),
  plus a made-up `Character.Steve_urgelles` and a bare `Character.Panam`. Count
  matches the client logs exactly: six bail `entity id is not dynamic`, the seventh
  (entity 9e03d1, 0-byte appearance) half-spawns and got an interpolation controller
  attach SECONDS before Cam's solo 0xC0000005 - garbage NPCs are now suspect #1 in
  the test-server flavor of THE crash. Live has NO npcs.json, so live ghosts (if the
  sweep's were live) have another source. Immediate cure: `/npc clear` (admin, in
  game). Code fix owed: /npc must refuse records containing `<`/`>` (placeholder
  paste) - the base-game-records warning added 2026-08-23 does not stop this.
- **Parked persisted vehicles get MakeRemoteDriven on every join** (found in the same
  logs): HandleVehicleLoadMessage -> OnVehicleReady -> MakeRemoteDriven runs for
  Cam's parked, unoccupied Archer Hella (vehicles.json) on BOTH machines - a parked
  replayed car must not be made kinematic/player-controlled; MakeRemoteDriven belongs
  to occupied vehicles only. Client-code fix in VehicleSystem.cpp, pairs with the
  readiness gating THE-crash entry already calls for.
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
  80) but misbehaves on vehicles — LAB BUG, investigate before trusting it. NEXT:
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

### Character identity: the template must stop being people's identity (opened 2026-08-28)
The through-line behind three of these: **Phantom Veronica** — the template save, a female
V at level 34 — supplies the WORLD (doors, quests, story position) and must never supply
anybody's identity. Cam's rule: *"you should ONLY use the world around her, not her herself
as a character."* Seeing her in-game IS the failure signal.

- **You spawn as Phantom Veronica. FIX WRITTEN, NEVER RELEASED, UNTESTED (e795a52).**
  The server has always decoded the stored appearance and handed it to every OTHER client —
  which is why remotes looked right and the only person who never saw their own character
  was the person playing it. It never sent it back to the owner, and `SetCharacterStatus`
  only recorded flags. Server now sends it (`Level.cpp`, 32 additive lines) on
  `NotifyAppearanceUpdate` addressed to the player's own id — **deliberately NOT new
  `SpawnCharacterResponse` fields**: netpack prefixes each message with a presence bitfield
  sized to its optional fields, that one reads 6 bits, and two more would make it 8 and
  misalign every field after them on any client not rebuilt in lockstep. `.proto` files are
  byte-for-byte unchanged and old clients are unaffected.
  Client side took three wrong turns worth recording: (1) `InitializeState` is creator-only
  — the engine refuses it in gameplay and the game itself calls it from only two pregame
  menus; (2) the RTTI functions live on `gameuiICharacterCustomizationSystem`, the INTERFACE
  — the concrete class has **0 functions**, so `Red::CallVirtual` never found them and
  returned false without calling anything; (3) `GetCustomizationState()` reads
  `(ccSystem + 0x78)` which IS null in play, but `equipmentSystem.script:4992` calls
  `GetState()` during ordinary gameplay to compute a hair suffix — **the state is reachable,
  through the RTTI accessor, not that offset.** Current code asks the way the game asks.
  Staged logging brackets every step (`GetState BEGIN/COMPLETE`, `Deserialize ccstate`,
  `ReFinalizeState`) and a failure names its stage. One-shot state machine + stale-update
  rejection keyed on the remote player id (reassigned every spawn, so it doubles as a
  revision token with nothing added to the wire).
  **NEXT: it has never been in a release, so the launcher cannot deliver it. Test build,
  then confirm ON SCREEN — `COMPLETE` in the log is not a character with the right face.**

- **Veronica's inventory lands on the player. FIX WRITTEN, UNTESTED (d926ca9).**
  The strip runs, the kit lands correctly (5 items, €$20,000, no chrome) — and ~88 seconds
  later 124 stacks and 14 cyberware appear, and the 90s autosave writes them to the server
  as the character's real inventory. **Not a vanilla engine grant** — a fresh start has none
  of that, and the counts vary by session (409/60, then 124/14) where a fixed grant would
  not. It is her save data arriving late.
  Fix is `MpStarterSettlement` (Inventory.reds): waits for the stack count to grow past the
  kit, waits for it to stop moving, cleans ONCE, disarms permanently. Detects rather than
  sleeping — 88s is one machine's observation, not a contract.
  **HARD CONSTRAINT, do not "improve" this into a recurring strip:** Cam's rule is *"any new
  weapon, clothing, cyberware, money or any item a person grabs or buys stays on them."*
  A cleanup on a timer / on inventory change / on reconnect would delete a gun somebody just
  bought. It arms ONLY inside `ShouldEquipRestored()`, which is true only for a starter kit,
  which is true only at character creation.
  **NEXT: make a character, watch for `settlement: armed` → `INITIALIZED`, then buy
  something, reconnect, confirm it survived.**

- **Money does not persist. NOT STARTED.** 84 eddies picked up; every subsequent capture
  read exactly `20000`. Nothing decayed — a gain never entered the record. Candidate: a
  second restore ran mid-session (`restore DONE: money 20000 -> 20000`) and re-applied the
  server's snapshot, which would also violate the items-must-stay rule. **Instrument every
  boundary before touching it** — capture → send → record → save → reload → apply.

- **Requested, not started:** `/rename` for admins (key on `CharacterId`, NOT the Discord
  account — Discord names change; needed because the invisible chat box made people type
  names blind, producing "JulianJulian Vale"); lock difficulty to hardest and remove in-game
  switching; four character slots for admins with used/empty shown; delete a character.
  Note the plumbing is half there already: `AuthenticationResponse` carries a *list* of
  `CharacterSummary`, and `CharacterRecord` already has `Slot` and `CharacterId`.

### Landed 2026-08-28 (kept brief; details in the rows above)
- **Character overwrite — FIXED (fbdff4a).** Cam lost a character to this. He connected, was
  restored correctly (13 stacks, €$20,000), pressed join from the main menu — which detaches
  the world and rebuilds it from the LOCAL save — and 70s later the disconnect save captured
  the template and sent it as him: `server sent 13 stack(s)` became `409 stack(s), 872
  eddies`. `m_restorePending` could not catch it; it only covers the window before the first
  restore. Now `m_characterLive`, set when a restore completes and cleared by ANY world
  detach, gates all four save paths through one rule (`MaySaveCharacter()`).
- **Quitting saves — FIXED (019a4cc).** The guard above created a second bug: the only exit
  save lived in `Disconnect()`, and quit-to-desktop / quit-to-menu tear the world down on the
  engine's schedule, so the guard (correctly) refused and the session was lost. The save now
  runs at the TOP of `OnBeforeWorldDetach` — the last instant the body is still yours.
- **The invisible chat box — SOLVED (96da4bf, shipped v0.3.113).** Never a visibility problem.
  A native parent-chain walk (`LogWidgetAncestry`, added because `inkWidget` in 2.31 has
  `Reparent` and no `GetParentWidget`, so script can only walk DOWN) showed every ancestor
  visible with opacity 1 — and the `hud` canvas at `pos=(0,0)`, then `(47,1688)` thirteen
  seconds later, right after an `OnVehicleEnter`. `(0,0)` is how the asset authors it, and the
  ONLY things that ever moved it were the `to_vehicle`/`from_vehicle` animations. Nothing
  played one at startup, so the whole custom HUD drew in the top-left corner until you got
  into a car. Now a `from_vehicle` plays when the connection comes up.
  **Not cosmetic:** the name prompt fires at spawn into a box that was not on screen, and a
  name is chosen ONCE — that is where "JulianJulian Vale" came from.
  Eliminated with evidence along the way, so nobody re-treads it: the widget matches its
  authored state exactly, and `multiplayer_ui` in `prototype_hud.inkhud` carries the MOST
  permissive context list of all 70 entries and is otherwise field-for-field identical to
  `new_phone`. (Decoded with a WolvenKit CLI built from source — `dotnet build
  WolvenKit.CLI`, then `convert serialize` — worth knowing that route exists.)
- **Crash instruments removed (58c80f0)** now the race is understood: the `0xDD` free-poisoning
  (memset of every freed flecs block) and the per-frame mutex+set census in `progress()`.
  Kept: the `FlecsThread` tripwires, which cost one relaxed load off the per-frame path.

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
- **"Built and pushed" is NOT "deployed" — three surfaces, and each one bit once on
  2026-08-28.** Every time, a correct fix looked broken because the thing under test was not
  the thing that was built. Verify the artifact contains the specific change, by string, not
  by timestamp, before asking anyone to test.
  1. **Redscript does not ship with the build.** The DLL is a symlink into
     `build\windows\x64\release\`, so it goes live the instant it links. Redscript does not:
     the plugin's `assets` folder is a JUNCTION to `distrib\launcher\mod\assets`, and
     **`xmake install -o distrib Client` prints "install ok!" and leaves edited `.reds` at
     their previous contents.** Force-copy
     (`Copy-Item "code\assets\redscript\*" ... -Recurse -Force`) and then check the deployed
     file. Never mirror/`/PURGE`: `distrib` legitimately carries `World\CharacterProfile.reds`
     and `World\KiroshiScanner.reds`, which are not in `code`. `Ship.ps1` is safe — line 343
     force-copies and 346-348 verify, so releases were never affected.
  2. **The launcher writes THROUGH the symlink.** Installing a release replaces the dev build
     with the release binary, so a fix built minutes earlier silently disappears and the game
     logs behaviour from code no longer in the tree. Rebuild after any launcher install.
  3. **A fix that exists only in git reaches nobody.** The appearance fix was committed,
     pushed to main and verified in the local build — then v0.3.113 shipped without it,
     because the release had already been cut. The launcher can only deliver what is in a
     release.
- **No manifest signing key** (`~/.nco-manifest-key`), so releases ship without
  `server-manifest.json` and players get no manifest verification. NOT new — v0.3.107 through
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
