# Stage 6C — reachability audit, respec spec, vehicle-shop disable, protocol inventory

**READ-ONLY DESIGN WORK. No hooks, no protocol changes, no runtime behaviour changed, no
netcode touched.** Completed 2026-09-05 (Cam stream), under the netcode freeze.

Everything here is server-agnostic and targets the replacement server.

---

## A. Quest / script money-reward reachability audit

### A.1 The money path, traced

Quest and script rewards reach money through one function:

```
rpgManager.script:2520   RPGManager.GiveReward(gi, rewardID, amount, target, moneyMultiplier)
  :2673                  rewardRecord.CurrencyPackage(currencyArr);
  :2693                  transSys.GiveItem(player, currencyItemID, quantity);
```

The reward record's **CurrencyPackage** decides whether a reward contains eddies at all.

### A.2 THE FINDING: most reward money is not from quests

Enumerating every caller of `GiveReward` outside `rpgManager.script` itself:

| Caller | Reward | Nature |
|---|---|---|
| `accessPointController.script:489` | `RPGActionRewards.Hacking` | world device |
| `accessPointGameController.script:310` | `RPGActionRewards.ProgramPartsAccessPoint` | world device |
| `doorController.script:1248` | `ExtractPartsDoor` | world device |
| `terminalController.script:670` | `ExtractPartsTerminal` | world device |
| `securityTurretController.script:475` | `ExtractPartsSecurityTurret` | world device |
| `surveillanceCameraController.script:648` | `ExtractPartsSecurityTurret` | world device |
| `ExplosivedevicePS.script:383` | `ExtractPartsSecurityTurret` | world device |
| `explosiveTriggerDevicePS.script:127` | `ExtractPartsSecurityTurret` | world device |
| `disposalDeviceController.script:15` | record-driven | world device |
| `baseDeviceActions.script:543` | record-driven | world device |
| `bountyManager.script:257` | bounty record | world / NPC |
| `player.script:4485` | `evt.rewardName` | generic event |
| `giveRewardEffector.script:20,24` | `m_reward` | **quest-driven** |

**Twelve of fourteen callers are world-device or NPC interactions.** Exactly one —
`giveRewardEffector` — is the quest-graph path.

This is a **new Cyberpunk fact** and it inverts the earlier assumption. "Quest/script rewards"
was one conservative row in the Stage 6B matrix; the reward surface is really dominated by
scrapping and hacking world objects, and those are reachable regardless of quest state.

### A.3 Does NCO's quest suppression stop any of it? No — and our own code says so

`Quests.reds` is explicit:

> *"It does not disable the quest engine, and cannot from here — quest phase graphs are native…
> So quests still technically progress in the background."*

And on the phone block, which is the strongest suppression NCO has:

> *"Suppressing the call cannot fix it — `PhoneSystem.OnTriggerCall` above already stops every
> call and the conversation still played, because the conversation is a scene the quest drives."*

**Quest progression is not blocked. Only its presentation is.** Notifications, objective popups
and calls are silenced; the graphs still run.

NCO's gamedef confirms the quest stack is loaded, not stripped:

```
mainQuests:      ep1\quest\cyberpunk2077_ep1_standalone.quest
                 ep1\quest\ep1.quest
                 ep1\quest\ep1_preorder.quest
world:           base\worlds\03_night_city\03_night_city.world
spawnPointTags:  #q000_spwn_start
```

### A.4 Classification

| # | Path | Class | Evidence |
|---|---|---|---|
| 1 | access point hack reward | **REACHABLE** | world device, proximity-triggered, no quest gate, no call |
| 2 | access point program parts | **REACHABLE** | as above |
| 3 | door scrap | **REACHABLE** | world device |
| 4 | terminal scrap | **REACHABLE** | world device |
| 5 | security turret scrap | **REACHABLE** | world device |
| 6 | surveillance camera scrap | **REACHABLE** | world device |
| 7 | explosive device scrap ×2 | **REACHABLE** | world device |
| 8 | disposal device | **REACHABLE** | world device |
| 9 | generic device action rewards | **REACHABLE** | `baseDeviceActions`, record-driven |
| 10 | bounty reward | **REACHABLE** | already Class C in Stage 6B; NPC-triggered, no quest gate |
| 11 | `player.script` reward event | **UNKNOWN** | generic; whatever raises `rewardName`. Not traced to a trigger |
| 12 | quest-graph rewards (`giveRewardEffector`) | **UNKNOWN** | engine runs; which graphs execute in NCO is not established |
| 13 | gig / fixer rewards | **UNKNOWN** | gigs *start* with a call, which is blocked — but the code says scenes still play, so "blocked call" ≠ "gig cannot complete" |
| 14 | main-story rewards | **UNKNOWN** | PL standalone start; prologue removed at gamedef level, rest not traced |

**Reachable: 10 · Unreachable: 0 · Unknown: 4**

**Zero paths could be proven unreachable.** Following the instruction not to guess, everything
not positively established stays UNKNOWN and remains a blocker.

### A.5 The blocker that would resolve most of this

**Whether `ExtractParts*` reward records contain currency at all.** They may grant only crafting
components, in which case rows 3–9 are not money sources and the matrix shrinks substantially.

That answer lives in **TweakDB**, not in script — `GiveReward` grants whatever the record's
`CurrencyPackage` holds, and the record is data. It cannot be read from this repository.

**Recommended next step (read-only, ~30 min):** inspect these records with the WolvenKit console
— it is already the established tool here and settled the Dogtown fact question:

```
RPGActionRewards.Hacking
RPGActionRewards.ProgramPartsAccessPoint
RPGActionRewards.ExtractPartsDoor
RPGActionRewards.ExtractPartsTerminal
RPGActionRewards.ExtractPartsSecurityTurret
```

For each: does `CurrencyPackage` contain `Items.money`? If no, rows 3–9 leave the money matrix
entirely.

### A.6 Effect on the money-authority matrix

Stage 6B listed "quest/script rewards" as one row. It becomes:

- **one row removed** — "quest/script rewards" as a single unit was the wrong shape;
- **one row added, REACHABLE** — *world device scrap/hack rewards* (`NEEDS WORLD SYNC`:
  device identity, one-shot per device, proximity), pending §A.5;
- **one row kept, UNKNOWN** — *quest-graph rewards*, a blocker until graph execution in NCO is
  established.

The workload did not shrink. It got **more precise**, and the precision moved work from
"unknowable quest surface" into "enumerable device surface", which is tractable — the same shape
as access points, which the matrix already covers.

---

## B. `RespecIntent` — the first post-swap money authority target

Semantic design only. **No packet layout.**

### B.1 Why respec is the right first target — now proven, not assumed

```
playerDevelopmentSystem.script:2020
    GetTotalRespecCost() : Int32
        basePrice       = TweakDB Price.RespecBase
        singlePerkPrice = TweakDB Price.RespecSinglePerk
        cost = basePrice + singlePerkPrice * (GetSpentPerkPoints() + GetSpentTraitPoints())

:1969  RemoveAllPerks(free)
           RemoveItem(m_owner, MarketSystem.Money(), respecCost)
```

**The cost is a pure function of two TweakDB constants and the character's spent points.** No
vendor, no world object, no NPC, no proximity, no stock, no reputation modifier.

And NCO's `CharacterRecord` already persists `Perks`, `PerkPoints`, `Attributes` and
`Proficiencies` — **the server already holds the inputs.** That is what makes this the one money
source the server can validate today with no new world model.

### B.2 The intent

```
RespecIntent
    (no client-supplied cost, no client-supplied balance, no client-supplied point counts)
```

The client is saying *"this character is respeccing"*. Nothing else it could say is trusted.

**Authoritative inputs** — all server-side:

| Input | Source |
|---|---|
| character identity | authenticated session, never the message |
| spent perk points | server's persisted `Perks` |
| spent trait points | server's persisted progression |
| `Price.RespecBase` | server-side TweakDB constant snapshot |
| `Price.RespecSinglePerk` | server-side TweakDB constant snapshot |
| current balance | server's authoritative `Money` |

**Server calculates:** `cost = base + perPerk × (spentPerks + spentTraits)`.

**Server validates:** the character exists and the session owns it; there is something to respec
(cost > base implies spent points, and a respec with nothing spent should be refused or free by
policy — **decide explicitly**); `balance >= cost`; revision headroom.

**The client must NEVER decide:** the cost, the number of spent points, the resulting balance, or
whether it could afford it. Vanilla's `CheckPlayerRespecCost()` reads the *local* balance — that
check is a UI affordance and must never be the authority.

### B.3 Idempotency, concurrency, persistence, reconciliation

**Replay** — a respec is destructive and not naturally idempotent: a retry after the perks are
already cleared would compute a *lower* cost (fewer spent points) and charge again. So it needs
an idempotency key, and the replay must return the **original result**, mutate nothing, and
re-send the authoritative balance.

**Concurrency** — one active session per CharacterID (§3.16 of the handoff) removes the
two-sessions race. Within a session, serialise per character: cost depends on spent points, and
two in-flight respecs would both compute against the pre-respec state.

**Persistence** — copy → mutate candidate → validate → persist atomically → then record the
result. Never record success before persistence.

**Revision** — money changed, so `MoneyRevision` advances **exactly once**, at the transaction
boundary, after the debit succeeds. Perk changes alone would not advance it.

**Reconciliation** — the server sends the authoritative balance and the client applies it through
the game's money item. The mechanism already exists and works in both directions
(`ApplyServerMoney`); the new server needs the same shape, not a second implementation.

**Ordering caution:** the perk reset and the debit are one logical transaction, but the *perk*
half executes in the client's game. If the debit is authoritative and the reset is local, a
failure between them leaves a charged player with their perks. **The new server must decide
which side owns progression before this ships** — this is the one place respec is less trivial
than it looks, and it is worth knowing now rather than at implementation.

---

## C. Vanilla vehicle-shop disable spec

**Design only. Do NOT disable on the outgoing server.**

### C.1 Entry points

| Path | Money call |
|---|---|
| `cyberpunk/devices/UI/internet/vehicleShopGameController.script` | `:433 RemoveItem(player, MarketSystem.Money(), amount)`; `:422` reads local balance |
| `cyberpunk/devices/UI/computer/computerYaibaShowroom.script` | same pattern, showroom computer |

### C.2 Why it must not coexist

NCO owns vehicle ownership (`VehicleStore`, `VehicleRecord`, `/sellcar` + `/buycar` with
server-held price, lock tokens and ownership transfer). The vanilla path spends **vanilla eddies**
and grants a **vanilla vehicle the server does not model**. Two ownership systems, two authority
rules, one balance.

Under money authority that becomes concrete: a player buys a car through the internet, the
server never sees it, and the next authoritative money resync **takes the eddies back while they
keep the car** — or refuses the purchase invisibly.

### C.3 Recommended behaviour

**Prevent the purchase at the point of purchase**, not by hiding the UI — a hidden entry point
that still functions is the shape of exploits.

- Player-facing: a clear refusal ("vehicle purchases go through the server — see `/vehicles`"),
  not a silent no-op. A dead button generates support tickets.
- **Owned vanilla vehicles already on a save:** do not confiscate. Cam's standing rule is that
  what a player has must not vanish. Either leave them as local-only (unsynced, not multiplayer
  vehicles) or offer a one-time import into NCO ownership — **needs a decision**, and it is a
  migration question, not a disable question.
- **Interaction with NCO ownership:** the vanilla path must never create a multiplayer-owned
  vehicle, and must never spend multiplayer-authoritative money.

### C.4 Requirement recorded for the replacement

> The replacement server's client integration must prevent the vanilla vehicle-purchase path
> from creating multiplayer-owned vehicles or spending multiplayer-authoritative money.

---

## D. Branch-only protocol feature inventory

`feat/world-state` already diverges from published `fork/main`:

```
fork/main:    client 0x88b2f6b5cbefc91c   server 0xb2f2bf7363a7f337
HEAD:         client 0xc67c52a1b6c5f096   server 0xa14513f4653e80f
```

134 added lines, from three commits — `9af9e8d` (character slots), `1d5aec2` (phone calls),
`8156ebb` (`/call` fix). **This session introduced none of them.**

### D.1 What exists, as requirements

| Feature | Current shape | Requirement to carry | Do NOT carry |
|---|---|---|---|
| **Character slots** | `SelectCharacterRequest{slot}`; `slot`, `is_active`, `character_slots` on the character list | multiple characters per account; exactly one active; the server decides which | `slot` as an **integer index**. An index is positional identity — it breaks on deletion and reorder. Use CharacterID |
| **Phone calls** | `CallRequest{number}`, `CallControlRequest{call_id, action}`, `NotifyCall{call_id, state, display_name, number, incoming}` | server-owned call state; one message per transition; a stale phone must be recoverable | `action` and `state` as bare `uint32`. Untyped, and §1.9 of the handoff proves the wire does not range-validate. Use validated enums with a refusing default |
| **`/call` reachability fix** | removed an unreachable message | a declared message with no handler is a trap — the audit must fail on it | — |

### D.2 The rule

**`feat/world-state` protocol ≠ published protocol, and neither is the desired new protocol.**
Treat both as historical input. Extract requirements; redesign shapes.

---

## E. Requirements worth carrying / not carrying

**Carry** (added to the handoff): deterministic server-side cost calculation as the pattern for
every money source; device rewards as an enumerable world-sync surface; one-shot semantics per
world object; TweakDB constants as server-side inputs the server must snapshot rather than ask
the client for.

**Do not carry:** positional `slot` identity; untyped `uint32` state/action fields; the
assumption that quest suppression stops quest rewards; "quest/script rewards" as a single
undifferentiated row.

---

## F. New Cyberpunk facts discovered

1. **Reward money is overwhelmingly device-driven, not quest-driven** — 12 of 14 `GiveReward`
   callers are world devices or NPCs (§A.2).
2. **The respec cost is a closed-form function of two TweakDB constants and spent points**
   (§B.1), and NCO already persists the inputs.
3. **NCO's quest suppression does not stop quest progression or rewards** — stated in our own
   code, and the phone block explicitly does not prevent quest scenes (§A.3).
4. **Whether `ExtractParts*` rewards contain currency at all is a TweakDB question** that
   would materially shrink the matrix, and it is unanswered (§A.5).

---

## Confirmations

| | |
|---|---|
| outgoing runtime code unchanged | ✅ — this document and doc edits only |
| production `.proto` unchanged | ✅ — `git status code/protocol/` clean; identifiers unmoved |
| no netcode touched | ✅ |
| migration inactive | ✅ |
| selector parked | ✅ |
| inventory authority unchanged | ✅ |
| nothing pushed | ✅ |

**No netcode implementation until the server swap is complete.**
