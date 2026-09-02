# Banking

**Status: design only. Nothing here is built.** Cam asked for a bank system on 2026-09-02,
and has a bank mod found but not yet implemented. This is the design for the half that
matters — the half the mod cannot provide.

This reverses an earlier decision. `docs/CHARACTER-LIFECYCLE.md` recorded named money types
as declined, on the grounds that Cam had ruled out banking. He has since asked for it, so the
reasoning below is what changes.

---

## Read this first: the money path is currently broken

**There is an open bug where money does not persist.** 84 eddies were picked up and every
capture afterwards read exactly `20000`. Nothing decayed — a gain never entered the record.
The candidate cause is a second restore re-applying the server's snapshot mid-session.

**Do not build banking on top of that.** A bank is a promise that a number is safe, and the
number is not currently safe. Fix persistence first, prove it with a deposit that survives a
reconnect, and only then add a second balance to lose.

That is the whole ordering recommendation and it is not negotiable by enthusiasm.

---

## The one constraint that shapes everything

`kIdentifier = FNV1a64(kProtocolString)`, and `kProtocolString` enumerates **every struct and
every field in the entire protocol**. Change one field anywhere and the identifier changes,
and every client that has not been rebuilt is refused with *"your mod is built against a
different protocol than this server"*.

**There is no such thing as a backwards-compatible protocol change here.** Every one is a
flag day.

Two consequences for this design:

1. **Get the money schema right in one go.** Adding `BANK` now and `CRYPTO` in a month is two
   flag days. Decide the full set of money types before the first one ships.
2. **Batch it.** Whatever else is waiting on a protocol change should ride the same release —
   the fix to `NotifyMoney.balance` below, at minimum.

---

## What is wrong with the current money model

```cpp
// CharacterRecord.h
int64_t Money{0};
```

```proto
message NotifyMoney {
    int32 balance = 1;     // <- int32, against an int64_t record
    string reason = 2;
}
```

**The record is 64-bit and the wire is 32-bit.** That is a latent truncation waiting for a
rich enough character, and it is free to fix while we are already spending a flag day. Fix it
in the same change.

---

## The money types

Two, and only two, decided now rather than discovered later.

| Type | Where it lives | May go negative | Lost on death |
|---|---|---|---|
| `CASH` | on the character | no | **yes — this is the point** |
| `BANK` | in the account | no | no |

**Names are permanent.** They become keys in the stored record. OPX//77's warning applies
exactly: *renaming a money type orphans every balance already stored under the old name*.
`CASH` and `BANK` are the names. Do not tidy them later.

**No overdraft.** Neither type may go below zero. A removal that would take a balance
negative is **refused, not truncated** — truncating turns "you cannot afford this" into "you
now have zero", which is a bug report about theft.

**Why cash must be losable.** If cash is safe, a bank is just a second number and nobody has
a reason to walk to an ATM. The bank earns its place only when carrying eddies is a risk
somebody can choose to take. That is a gameplay decision as much as a technical one, and it
is the reason to build this at all.

How much is lost on death, and to whom, is a separate tuning question — a fraction, a cap, or
to the killer versus to nowhere. Not decided here; the schema does not care.

---

## Where the balances live

On the server, on `CharacterRecord`. The client never decides a balance, and the bank mod is
**UI only** — it draws a screen and sends requests.

```
CharacterRecord
  Money      int64_t   ->  becomes CASH   (migration below)
  BankMoney  int64_t   ->  new, starts at 0
```

Two named fields rather than a map. OPX//77 uses a JSON column of arbitrary named types
because their operators define their own; ours are fixed at two, and two fields are simpler
to read, migrate and reason about than a map with two entries in it. If a third type is ever
genuinely wanted, that is the moment to reconsider — and it is a flag day either way.

**Migration.** Existing `Money` becomes `CASH` unchanged; `BankMoney` defaults to 0. Nobody
loses anything and nobody starts with a full account. `NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT`
already gives us the default for records written before the field existed.

---

## Operations

Four, all server-decided, all audited.

| Operation | Rule |
|---|---|
| `deposit(amount)` | `CASH -= n`, `BANK += n`. Refused if cash is short. Requires being at an ATM or terminal. |
| `withdraw(amount)` | `BANK -= n`, `CASH += n`. Refused if the account is short. Same location requirement. |
| `transfer(toCharacterId, amount)` | `BANK -= n` on the sender, `BANK += n` on the recipient. |
| `balance()` | A read. Both figures. |

**Every one of these is a request, not an answer.** The client sends; the server decides and
replies. The mod must not show a new balance until the server has confirmed it — an optimistic
UI that shows money the server refused is how a player learns to distrust the bank.

### Transfers are why the readable id shipped

`transfer` takes a **character id**, and somebody types it. That is exactly why ids became
`AEC-MJ6P` with a check symbol instead of sixteen hex characters: with hex, a mistyped id is
another valid id, and the money goes to a stranger with nothing on screen reporting an error.

The check symbol catches every single-character typo and every adjacent transposition, so a
mistyped id fails at the parse rather than resolving to somebody else's account.

**Transfers must work on the store, not on live players.** The recipient is very often
offline. `PlayerStore::FindCharacterById` already normalises through `ParseCharacterId`, which
is the lookup this needs.

### Atomicity

A transfer touches two records. It must be all-or-nothing:

1. Resolve and validate both characters, and the amount, **before** touching either balance.
2. Apply both sides together.
3. Persist, then notify.

A crash between the two sides is the one failure that creates or destroys money. Whatever the
final implementation, the debit and the credit belong in the same function with no early
return between them, and the save happens after both.

### Audit every movement

`AuditLog::Record` already exists. Every deposit, withdrawal and transfer gets a line: who,
to whom, how much, and the resulting balances.

RP servers get "he took my eddies" disputes, and the only thing that settles one is a log
written before anybody complained. This is not optional bookkeeping — it is the feature that
makes the bank trustworthy to argue about.

---

## Validation, in the order it must happen

Each of these is a refusal an attacker will try. Every one is checked on the server.

- **The amount is a positive integer.** Zero is a no-op worth refusing so it shows up as a
  mistake rather than a silent success. Negative is a withdrawal disguised as a deposit.
- **The amount is not absurd.** A sane per-transaction ceiling, because an overflow is a much
  worse bug than a rejected transfer.
- **The source has the funds.** Refused, never clamped.
- **The recipient exists**, and is not the sender. Transferring to yourself is a no-op that
  will otherwise be used to test for the existence of a bug.
- **The player is where they must be.** Deposits and withdrawals at an ATM; transfers wherever
  the design allows. Distance re-derived on the server from the player's replicated position,
  never trusted from the client — the same rule the elevator design states plainly.
- **Rate limit.** Per character, per window. A transfer loop is the cheapest possible way to
  hammer the store.

**Refusals answer stable codes**, not English sentences: `insufficient_funds`, `no_such_character`,
`self_transfer`, `not_at_terminal`, `rate_limited`, `bad_amount`. The wording belongs at the
UI. This is one of the things worth taking from OPX//77 regardless of banking.

---

## What the mod does and does not do

Cam has a bank mod found but not implemented. Whatever it is, the division is:

**The mod may:** draw the ATM or terminal, take input, show balances the server has confirmed,
and play the interaction.

**The mod may not:** decide a balance, hold authoritative state, or be trusted about where the
player is standing. If the mod is a Nexus content mod, the **helper rule** in `docs/MAP.md`
also applies — *content mods are never load-bearing*. Banking must work on a modless install,
which means the commands must exist as chat commands even if the pretty terminal does not.

That is the test to apply when the mod is evaluated: **does the bank still work if the mod is
not installed?** If no, the integration is wrong.

---

## Suggested order of work

1. **Fix money persistence.** Nothing below is safe until a gain survives a reconnect.
2. **Decide the death rule for cash** — it is what makes the bank worth having.
3. **One protocol change**, carrying: the `int32` → `int64` fix, the money types, and the bank
   request/response messages. One flag day.
4. **Server: balances, operations, validation, audit.** Chat commands first — they need no UI
   and they prove the whole path.
5. **Client: the terminal.** Last, because by then it is only a screen.
