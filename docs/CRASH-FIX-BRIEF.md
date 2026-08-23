# THE crash — fix brief (2026-08-23)

Handoff from zeldfep's session to whoever writes the fix. Everything below is either
proven by live experiment tonight or verified by reading the code at the cited line.
The map's ledger has the running history; this is the surgical version.

## The verdict, by elimination

Ten crashes on Cam's machine in one night (0xC0000005, twice 0x80000003), 10–25s
after spawn (two outliers at 92s and 114s — explained below). Eliminated by
controlled experiment, one variable per round:

- **Garbage NPC replay** (7 `/npc` entries, five with the literal record
  `Character.<record>`): purged server-side → still crashed.
- **Parked-vehicle replay**: absent after a server restart cleared world state →
  still crashed.
- **Character-creator flow**: bypassed entirely (Initialised + NameChosen flipped
  server-side, the `/character new confirm` guard live and verified refusing) →
  clean spawn at 03:02:29, crashed in 25 seconds.

What remains, present in every crash and absent from every clean session: **the
spawn-time apply of a FEMALE character's appearance/cyberware**. Cam and phonix
(the two female-character players, 12–13KB appearance blobs) both crash-prone;
every male-character player (5–9KB blobs) exits clean. phonix's sweep-era logs show
`AddItemToSlot failed` ×106 in one evening. Machine-dependence is timing: same
bytes, different streaming speed.

Every crash log cuts within ~2s of one of two lines:
`[Interpolation] idle controller (re)entered for puppet X - attaching multi controller`
or the `[Appearance] read N equipment item(s)` poll, shortly after
`[script] cyberware: queued N piece(s)` / `restore DONE`.

Evidence lives on the NAS: `logs/clients/noremacxxi/CyberpunkMP_2026-08-23_*.log`
(esp. 00-45-31, 00-49-48, 01-05-55, 01-15-20) and the nco-authority docker log
02:52–03:07.

## The unified mechanism

There is **no entity-readiness gate anywhere on the puppet path**, and three
independent systems fire engine vcalls into the assembly window:

1. **Appearance apply fires at `Entity/Attached`** — but Attached ≠ assembled:
   slots, garment components and appearance parts are still streaming.
2. **The interpolation idle-hook attaches a controller to any registered puppet** —
   and the registry is populated BEFORE the entity assembles, by design.
3. **The restore storm (items, money, cyberware equips) is released when a player
   HANDLE exists** — sufficient for gives, not for equips.

The vehicle mount-path crashes on live are the same class with a different trigger.

## Code pointers, ranked

### Tier 1 — the readiness gate (fixes the class, not a symptom)

- **`vendor/Codeware/src/Red/Entity.hpp:57`** — the primitive the whole fix needs:
  the engine entity's lifecycle status byte at offset 0x156
  (Initializing=1, Attaching=3, **Attached=4**, Detaching=5, Uninitializing=6).
  Copy the `Core::OffsetPtr` into `code/client/Game/Entity.h` (same pattern already
  used at lines 9–10) and every vcall site can ask the engine directly.
- **`code/assets/redscript/World/VehicleSystem.reds:42` → `VehicleSystem.cpp:359`
  (`OnVehicleReady`)** — the pattern to copy: script `Entity/Attached` callback
  forwards to native, which drains a pending queue. Better yet:
  **`AppearanceSystem.reds:63–64` ALREADY registers `Entity/Attached` for tag
  `CyberpunkMP.Puppet`** — the puppet readiness event fires in script today; it
  just doesn't gate anything. Add native `OnPuppetReady(entityID)` mirroring
  `VehicleSystem.h:79`, set a ready-bit in PuppetRegistry, and require it wherever
  the puppet is touched.
- **`code/client/App/World/InterpolationSystem.cpp:666`** — the attach vcall. Only
  guards today: `GetEntity(id)` non-null (line 638 — returns entities in ANY
  lifecycle state) and a move component existing (655). Line **674** is written
  proof the attach is expected to run mid-spawn: a fallback stores the controller
  on SpawningComponent when no EntityComponent exists yet. Gate on status ==
  Attached; requeue otherwise.
- **`code/client/App/World/NetworkWorldSystem.cpp:125`** — PuppetRegistry::Add runs
  "BEFORE the entity finishes assembling" (its own comment). Registry membership is
  the idle-hook's ONLY identity gate (`InterpolationSystem.cpp:587`). Needs the
  parallel ready-bit.
- **`code/client/App/World/NetworkWorldSystem.cpp:2278`** — the 200ms promotion
  poll promotes SpawningComponent → EntityComponent on `GetEntity()` non-null
  alone. Everything downstream that treats EntityComponent as "ready" inherits the
  lie. Promote on status == Attached, or drive promotion from the Entity/Attached
  callback.

### Tier 2 — the appearance apply (the female-biased trigger)

- **`code/assets/redscript/World/AppearanceSystem.reds:228`** — `OnEntityAttached`
  immediately calls native `ApplyAppearance` (line 233). No readiness beyond
  Attached, no retry if it fails: an undressed puppet stays undressed forever.
- **`code/client/App/World/AppearanceSystem.cpp:150` (`AddItems`)** — per-item
  `GiveItem` (234) / `AddItemToSlot` (237) vcalls with zero readiness checks;
  failures logged at **240** (the ×106 line — currently logs NOTHING identifying:
  add entity id, TweakDBID, slot) and dropped. Each success spawns garment
  part-entities onto a still-assembling puppet — the async part attach is a
  textbook delayed 0xC0000005.
- **`AppearanceSystem.cpp:497–554`** — **hot candidate**: `Span<...Key>` built over
  a STACK-LOCAL DynArray is handed to `ScheduleSynchronizedAppearanceChanges`, a
  raw engine "schedule" call. If the engine defers reading the spans until the
  change executes (seconds later on a streaming puppet), it reads freed stack.
  Female states produce MORE keys (line 453–468: `breast`, `lifted_feet`,
  `flat_feet` are wa-only groups) — bigger, differently-reused allocations. Fix:
  move `keys` into the completion-lambda capture (stateHandle/object already live
  there), or prove the engine copies synchronously.
- **`AppearanceSystem.cpp:336–343`** — unchecked `reinterpret_cast` of
  persistentState to `PuppetPS*` + raw write at `unk72`. Verify RTTI type first;
  the puppet record is launch-configurable and the female path builds from a
  different template.
- **`AppearanceSystem.cpp:214`** — `GetItemAppearanceName` resolves &Female/&Male
  suffixes by asking the half-built entity itself, once per item.
- **`code/client/Game/Utils.h:147` (`CMPReader::Serialize`)** — silent under-run
  (destination left as uninitialized memory), `IsOK()` hardcoded true, hardcoded
  save-version. A mis-parsed 12–13KB female blob yields garbage customization
  arrays that the appearance job walks later. Zero-fill + real status + abort the
  apply when `offset != bytes.size()`.
- **`code/client/App/World/VehicleSystem.cpp:463` (`SetExitGrace`)** — the one
  existing "component rebuild in progress — do not attach" window (4s after
  vehicle exit, honored by the idle hook at `InterpolationSystem.cpp:604`).
  Appearance application is a confirmed rebuild trigger and sets NO grace. Reuse
  the mechanism around ApplyAppearance/ReapplyAppearance/AddItems.

### Tier 3 — the restore storm and the 90s outliers

- **`code/client/App/World/NetworkWorldSystem.cpp:313`** — the "still waiting to
  restore" gate waits on player-system non-null + player handle non-null ONLY.
  Fine for gives; not for cyberware EQUIP.
- **`NetworkWorldSystem.cpp:337`** — `m_restorePending = false` BEFORE the script
  restore even runs, releasing `PollEquipmentChanges` (834) and the 90s
  possessions autosave (2242) while the equip queue is still draining.
  **The 92s/114s outlier crashes line up with the 90s autosave's FIRST firing**
  (`SaveCharacterAppearance` serializes the full CC state + walks inventory during
  the churn). Keep the flag true until restore has LANDED (script calls a native
  RestoreDone after the last equip settles).
- **`code/assets/redscript/World/NetworkWorldSystem.reds:265`
  (`RestorePossessions`)** — the natural seam: split gives/money/stats (current
  gate is fine) from cyberware equips (needs the strict gate + DelayCallback).
- **`AppearanceSystem.reds:119` (`GetPlayerItems`)** — the 1s visual-slot poll that
  many crash logs die inside, racing the equip burst. Gate it on restore-landed.

### Tier 4 — the vehicle siblings (fix while in there)

- **`VehicleSystem.cpp:369`** — `OnVehicleReady` runs `MakeRemoteDriven` on EVERY
  attached vehicle, including parked unoccupied replays (separately confirmed
  wrong). Only when a mount is queued/arrives; parked copies get physics-off at
  most.
- **`VehicleSystem.cpp:567` (`DoMount`)** — character gate is `hash != 0`, which
  SpawningComponent satisfies. Require the character's status == Attached; requeue
  keyed on the character like `m_pendingMounts` keys on the vehicle. Add
  `IsDefined(character)` in `VehicleSystem.reds:84`.
- **`code/client/Game/Animation/MultiMovementController.cpp:146` (`Attach`)** —
  dereferences `movable.owner->placedComponent->localTransform` with no null
  checks (GetDeltaTransform at line 42 got them after a previous crash; Attach
  never did). **Cheapest possible last-line defense** — null-guard it regardless
  of everything above. Same for `m_pState` in `Tick`/`GetAnimationParameters`
  (lines 30, 169) and the raw `m_pComponent` pointer (header line 69 — make it a
  WeakHandle like `AnimationDriver.h:22`).
- **`InterpolationSystem.cpp:664`** — the attach lambda floods: several queued per
  attach window, each allocating and attaching a fresh controller with no
  main-thread dedup. One pending-attach flag per id.

## Diagnostics to add with the fix (cheap, decisive next time)

- `AddItemToSlot failed` (cpp:240): add entity id, item TweakDBID, slot.
- The attach log line (`InterpolationSystem.cpp:618`): add the entity status byte
  and a rebuild-source tag.

## Validation

The repro is on tap: Cam + his female character on the test server = death within
25s, ten for ten. After the fix: he spawns, stands two minutes, then the combat
checklist finally runs. Cross-machine confirmation if wanted: a female character on
zeldfep's machine (offered, not yet run). The `-sync-trace` flag and the checkup
are both live for instrumentation.

## Already fixed — do not re-chase

`/character new` guard (live-verified), ghost NPCs purged (the `/npc`
placeholder-record REFUSAL is still owed in ChatSystem), payload extract
replace-not-merge (v0.3.108), protocol denial machinery (live-verified), Cam's
launcher settings wipe (v0.3.108 checkup surfaces the server target now).
