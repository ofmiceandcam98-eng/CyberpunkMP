# Character Sync Phase - Plan of Record

Set by zeldfep 2026-08-18: after vehicles (shipped v0.3.72), character
fundamentals come before anything else - instant visibility, individual
identity, run/jump/weapon sync - then player interaction and trade, then
vehicles revisited. Produced from a three-subsystem code mapping; the b1
identity diagnostic is ALREADY ANSWERED from shipped session logs
(15-21 customization keys arrive and resolve => direction b4-B, the
template overrides them downstream).

# Character Sync Phase Plan

Protocol note that shapes everything below: netpack hashes the .proto files into the version check (`code/netpack/main.cpp:92 HashProtocol`, compared in `GameServer.cpp HandleAuthentication`), so **any proto edit is automatically a lockstep client+server bump**. Batch proto changes into as few bumps as possible — this plan needs exactly two.

Pipeline note (cross-cutting blocker): the test-build pipeline ships **DLL-only**. Workstreams marked **[PIPELINE-BLOCKED]** need redscript (.reds), TweakDB (.tweak), or archive (.ent) delivery. Unblocking this (extend the pipeline to ship the redscript/tweaks/archive folders alongside the DLL — they are plain files consumed by RED4ext/redscript/TweakXL/ArchiveXL at load) is itself **increment 0** of this phase, because targets (b) and (c) both dead-end without it.

---

## Target (a) — "see each other moments after connection"

Ground truth from the spawn map: character spawn is already same-second server-side and ~35ms wire-to-render. The *felt* latency is misread logs, the 30–40s save load with zero presence feedback, per-join server stalls, and reconnect/failure black holes. Dependency order:

**a1. Log clarity (no deps).** Add an unambiguous `puppet {id} spawned for {user}` line in `Level::HandleSpawnCharacterRequest` and on the Discord-disabled branch (`GameServer.cpp:630` path currently prints nothing); reword the vehicle line at `Level.cpp:739`. Files: `code/server/native/Game/Level.cpp`, `GameServer.cpp`. No proto bump. No redscript. Blast radius: log strings only. Live test: two joins, confirm every join prints one spawn line and vehicle entry prints a distinct line.

**a2. Async Discord verification (no deps).** Move `VerifyDiscordToken` (`GameServer.cpp:569/895`) off the game thread — verify on a worker, complete auth via a queued continuation. No proto bump, no redscript. Blast radius: medium (auth ordering; every packet handler currently assumes auth completes inline — audit the post-auth sends at `GameServer.cpp:533-630`). Live test: player A sprints in circles while player B joins; A's movement must not hitch; measure with a cold token (cache expired).

**a3. Reconnect hygiene (no deps).** Clear `m_remotePlayerId` in `NetworkWorldSystem::OnDisconnected` (`NetworkWorldSystem.cpp:961-993`) so a reconnect doesn't spray the dead session's entity id (`server drops at Level.cpp:556-560`). Optional: simple auto-reconnect in `NetworkService::OnDisconnected` (`NetworkService.cpp:58`). No proto bump, no redscript. Blast radius: small, client session lifecycle. Live test: restart the server container mid-session; both clients rejoin and see each other without "invalid entity" warnings in server log.

**a4. Spawn-failure resilience (batch into proto bump #1).** Every early-return in `NetworkWorldSystem::Spawn` (`cpp:66,82,104,118`) permanently loses a player. Add client→server `RequestCharacterReload{id}` and re-send `NotifyCharacterLoad`; call it from the failure paths and from `OnAfterWorldDetach` (fixes the "loaded a different save, everyone invisible forever" hole — re-request all actors on world re-attach). Files: `code/protocol/client.proto`, `server.proto`, `Level.cpp` (re-serialize via existing `Level::Serialize`), `NetworkWorldSystem.cpp`. **Proto bump: yes.** No redscript. Blast radius: small-medium. Live test: player A loads a different save while connected, world re-attaches, player B reappears.

**a5. "X is loading in" presence (batch into proto bump #1, redscript for the ideal version).** Minimum viable, DLL-only: server broadcasts a `NotifyPlayerJoining{username}` (or just a chat line via existing ChatSystem) at `PlayerManager::Create` — but that still only fires post-save-load, since `Connect()` is gated on the HUD ink controller (`MultiplayerGameController.reds:102-106`). Full fix — dial the socket from the main menu before loading the save — is a `MainMenu.reds`/`MultiplayerGameController.reds` restructure: **[PIPELINE-BLOCKED]** and highest-risk item in (a); defer to late phase. Live test: joiner clicks MULTIPLAYER, the in-world player sees "X is loading in" within ~2s, then the puppet within ~1s of load completion.

**a6. Kill the 200ms promotion poll (fold into c6 controller rework).** Promote `SpawningComponent→EntityComponent` from the Entity/Attached callback instead of the 0.2s flecs poll (`NetworkWorldSystem.cpp:935-956`). No proto, no redscript. Do it inside the animation-controller rework since that's the same attach-lifecycle code.

---

## Target (b) — "showing up as individuals"

**b1. Diagnostic session FIRST (zero code, gates everything).** The single undiagnosed fork: `"[Appearance] remote state produced {} customization key(s)"` at `AppearanceSystem.cpp:479` has never been read live. Run a two-player session, grep both client logs (they auto-ship to `/mnt/vol/NASa/CyberpunkMP/logs/clients/<player>/`). **0 keys** ⇒ the ccstate fails to resolve options outside the creator context ⇒ fix the deserialize/resolve path (b4-A). **n keys** ⇒ the player-template's self-customization overrides them downstream ⇒ fix the template (b4-B). Also confirm both players were on the SAME stack (the authority stack has an empty character store and different behavior — pin all identity testing to one stack).

**b2. `is_male` on the wire + capture fallback (proto bump #1).** Add `is_male` to `NotifyCharacterLoad` (`server.proto:22-38`) sourced from `CharacterRecord.IsMale`, stop re-deriving gender from the blob in `NetworkWorldSystem.cpp:70-97`. Same change: ship-and-use the orphaned `default_male.ccstate` fallback when a player has no stored blob, and add a retry for the silent first-spawn capture failure (`Level.cpp:522-546` / `NetworkWorldSystem.cpp:492-511` — retry `capture_only` on next mirror-open instead of giving up). Files: both protos, `Level.cpp`, `NetworkWorldSystem.cpp`. **Proto bump: yes** (batch with a4). No redscript. Blast radius: small. Live test: female-V player joins with empty blob → spawns as WaMuppet on the other client.

**b3. Re-enable muppet head/arms equips + strip Panam leftovers (DLL-only).** Uncomment the `Items.MuppetMaHead/WaHead/MuppetArms` pushes (`AppearanceSystem.cpp:336-343`; records already exist in the tweak) so face keys have a head to act on, and strip/hide default Panam gear in slots the remote equipment list doesn't cover (`AddItems`, `AppearanceSystem.cpp:136-233`). No proto, no redscript (tweak records already ship). Blast radius: small, but watch the "makes wa faces disappear" history — test both genders. Live test: two players with different outfits; no Panam gear visible on either puppet.

**b4. The identity fix proper (direction chosen by b1).**
- **b4-A (0 keys):** fix resolution — likely constructing the `CharacterCustomizationState` through the proper `ICharacterCustomizationState` init path so option groups resolve outside the creator; possibly re-enable the commented-out all-inclusive `character_customization`/`character_creation` group queries (`AppearanceSystem.cpp:436-459`) and the morph/apply callback (`:492-540`). DLL-only. Blast radius: medium — the code's own comment warns of spawn crashes; land behind the existing [Identity] fingerprint logging.
- **b4-B (n keys overridden):** neutralize the template self-customization — rebuild `player_ma/wa_tpp_cutscene.ent` off an NPC/generic rig instead of the player's own TPP template, or excise the self-customization component from the .ent. **[PIPELINE-BLOCKED]** (archive) and blast radius: **large** — the .ent is also the anim-import carrier for (c), so coordinate with c2 as ONE template rework.
- Either way, finish with the morph pass: replace the empty completion lambda so MorphHead/ApplyBody run (`AppearanceSystem.cpp:492-540`). Live test: two players with deliberately extreme, different faces (bald vs long hair, different skin tone) stand face to face; each sees the OTHER's face, verified against the [Identity] hashes in both logs.

**b5. Mid-session appearance/equipment updates (proto bump #2, shared with c5).** New `client::UpdateEquipmentRequest{items}` → `server::NotifyEquipmentUpdate{id, items}`; send from the existing 1Hz `PollAppearanceChanges` watch when the paperdoll changes; persist equipment in `CharacterRecord` so wardrobes survive reconnects. Files: both protos, `ChatSystem.cpp`/`Level.cpp`, `AppearanceComponent.h`, `NetworkWorldSystem.cpp`, `AppearanceSystem.cpp` (re-run AddItems diff on notify). **Proto bump: yes.** Redscript: reuses existing `GetPlayerItems` — no new .reds needed. Blast radius: medium. Live test: player A changes jacket mid-session; B sees it within ~2s.

---

## Target (c) — "run and jump and change weapons"

**c1. NaN guard + threshold retune (DLL-only, ship immediately).** Guard `fmodf(t, 0)` in `Walking.cpp:32`/`Sprinting.cpp:34` (duration 0 ⇒ clamp time 0); retune `kWalkSpeed 3.0→~1.2` and add a jog band around the real speeds (walk ~1.8, jog ~5.5, sprint ~7.5-9) in `States/Base.h:9-10`. No proto, no redscript. Blast radius: tiny. Live test: on the wa puppet (which HAS anims), a walking player must animate instead of gliding.

**c2. Male template anim merge — THE unlock (b4-B partner). [PIPELINE-BLOCKED, archive].** Replicate the wa hand-merge onto `player_ma_tpp_cutscene.ent`: full NPC locomotion set (crowd walk/jog/sprint), weapon locomotion (rifle/handgun/katana/mantis), exploration (jump/vault material). Currently ma imports ONLY ui_male + sportbike anims — nothing in c1/c3/c4 can ever render on a male puppet without this. Blast radius: large (asset), but isolated to the two .ent files; do it in the same rework as b4-B. Live test: male puppet walks/jogs/sprints visibly.

**c3. State-machine completion (DLL-only).** Add `Jogging` (LS_Jog/jog_0 — enum and anims already exist), write `AnimationData.speed` in every state (it is never set — the graph's blend param is permanently 0, `AnimationData.h:20`), and emit MTA_Start/Stop transitions instead of only MTA_Move/MTA_None. Files: `States/*`, `AnimationData.h`, `Base.cpp`. No proto, no redscript. Blast radius: small. Live test: jog vs sprint visibly distinct; starts/stops don't snap.

**c4. Movement flags on the wire (proto bump #2) + Jumping/Falling states.** Extend `MoveEntityRequest`/`NotifyEntityMove` with a compact input-state bitfield (jumping, airborne, sprint-intent, crouch) + vertical velocity — scalar 3D speed can never express a jump. Capture in `UpdatePlayerLocation` (`NetworkWorldSystem.cpp:700-825` — query the local player's locomotion state), relay through `MovementComponent`, buffer in `InterpolationSystem`, dispatch the already-scaffolded `Jump` event (`States/Base.h:18-20,47` — declared, never constructed) into new Jumping/Falling states. Anim availability depends on c2's exploration sets; the abandoned `AnimFeature_NPCExploration`/`ApplyFeature` scaffolding (`MultiMovementController.cpp:7,11`) is the intended route — finish it. **Proto bump: yes** (batch with b5). No redscript. Blast radius: medium (touches capture, server relay, interpolation, states). Live test: A bunny-hops in front of B; B sees actual jump arcs with a jump pose, no idle-float.

**c5. Weapon sync (proto bump #2, batched).** Three pieces: (1) capture the ACTIVE/held weapon — `GetPlayerItems` never queries it; hook draw/holster/swap either natively or via a small .reds addition **(flag: if redscript is needed here it's [PIPELINE-BLOCKED]** — prefer a native hook on the equip transaction to stay DLL-only); (2) `NotifyWeaponChange{id, weapon_tdbid, drawn}` on the wire (batch into bump #2 — the emote RPC path proves the pattern); (3) apply on remote: AddItemToSlot into the weapon slot, unforce the permanent `holstered_default` arms (`AppearanceSystem.cpp:458`), and select the weapon-specific locomotion styles the wa template already ships. Blast radius: medium. Live test: A cycles pistol→katana→holstered; B sees each weapon in-hand and holstered within ~1s, with matching locomotion.

**c6. Controller rework — retire the IdleController hijack (fixes the vehicle crash class).** The current design hooks `IdleController_SetAnimation` on the anim thread and swaps in `MultiMovementController` (`InterpolationSystem.cpp:269-326`); the engine tears it off on vehicle mount (proven live 2026-08-18) and recovery depends on the engine spontaneously making another IdleController. The animation rework is the natural moment to replace this: stop replacing the movement controller at all and instead drive the puppet purely through `AnimationDriver`'s anim-feature writes (`AnimFeature_Locomotion`/`CrowdLocomotion`/`NPCExploration` via RawFunc 2132808949/316482401) attached to the `AnimationControllerComponent` — which the engine does NOT tear off on mount — with position driven by teleport/interpolation on the transform directly. This deletes the attach-race (a6), the vehicle detach guards (`MultiMovementController.cpp:36-79`), and the "first motion lags spawn" hook dependency in one move. DLL-only. Blast radius: **large** — it replaces the core of the render path; do it LAST in the phase, after c1-c5 have proven the state machine and wire data, and keep the old path behind a launch flag for A/B. Live test: two players mount/dismount vehicles repeatedly; on-foot animation resumes instantly every time, zero crashes; regression-test the whole c-suite.

---

## Phase order — shippable increments, smallest verifiable first

| # | Increment | Contents | Proto bump | Pipeline/redscript |
|---|-----------|----------|-----------|-------------------|
| 0 | **Diagnostics + trivia** | b1 diagnostic session (read the key-count line, pin one stack), a1 log clarity, c1 NaN guard + thresholds | no | no |
| 1 | **Server hygiene** | a2 async Discord, a3 reconnect id clear | no | no |
| 2 | **Pipeline unblock** | extend test-build to ship redscript/tweak/archive alongside the DLL | no | is the pipeline |
| 3 | **Proto bump #1 — identity floor** | b2 is_male + ccstate fallback + capture retry, a4 reload-request, a5 minimal "joining" notify | **yes (one bump)** | no |
| 4 | **Locomotion on what exists** | c3 Jogging + speed param + transitions, b3 head/arms equips + Panam strip | no | no |
| 5 | **Template rework** | c2 ma anim merge + b4-B template neutralization (or b4-A DLL fix if diagnostic said 0 keys) + morph pass | no | **yes (archive)** — needs #2 |
| 6 | **Proto bump #2 — dynamics** | c4 movement flags + jump states, b5 equipment updates, c5 weapon sync | **yes (one bump)** | maybe (weapon capture hook) |
| 7 | **Controller rework** | c6 feature-driven driver replaces IdleController hijack, a6 poll removal | no | no |
| 8 | **Early-connect presence** | a5 full version: dial from main menu | no | **yes (redscript)** |

Each increment is independently live-testable by the standing two-player protocol: both clients on the SAME stack, grep `[Identity]`/`[Appearance]` lines in `/mnt/vol/NASa/CyberpunkMP/logs/clients/<player>/` plus the server log, with increment-specific pass criteria as listed per workstream above.

Key files (absolute): `C:/Users/vboxuser/Documents/Projects/CyberpunkMP/code/protocol/client.proto`, `.../code/protocol/server.proto`, `.../code/netpack/main.cpp`, `.../code/server/native/Game/Level.cpp`, `.../code/server/native/GameServer.cpp`, `.../code/client/App/World/NetworkWorldSystem.cpp`, `.../code/client/App/World/AppearanceSystem.cpp`, `.../code/client/App/World/InterpolationSystem.cpp`, `.../code/client/Game/Animation/States/` (Base/Idling/Walking/Sprinting), `.../code/client/Game/Animation/MultiMovementController.cpp`, `.../code/assets/Archives/source/archive/mods/cyberpunkmp/player_ma_tpp_cutscene.ent`, `.../code/assets/Tweaks/CyberpunkMP.tweak`, `.../code/assets/redscript/World/AppearanceSystem.reds`.
