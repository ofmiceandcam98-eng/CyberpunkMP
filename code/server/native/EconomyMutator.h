#pragma once

/**
 * The one place server-authoritative money and inventory are allowed to change.
 *
 * WHAT THIS IS, AND WHAT IT DELIBERATELY IS NOT
 *
 * These are INTERNAL SERVER FUNCTIONS. There is no network message that reaches them, and
 * there must never be one: a client saying "credit me 50,000" is the whole problem this
 * phase exists to remove. They are called only after an existing server system has already
 * established a legitimate reason - the vehicle sale has validated the sale, trade has
 * validated ownership and distance, the starter kit has decided the reward.
 *
 * NARROW ON PURPOSE. This layer knows arithmetic and inventory invariants. It does NOT know
 * vehicle prices, trade distance, recipes, loot tables or starter-kit eligibility, and it
 * must not learn them - each of those belongs to the system that owns that decision. The
 * split is: the caller decides WHETHER, this decides whether the resulting numbers are
 * possible, and performs them safely if so.
 *
 * ALL OR NOTHING, EVERY TIME. A failed operation leaves the record byte-identical. There is
 * no partial debit, no partial removal, and deliberately no clamping - a debit larger than
 * the balance FAILS rather than zeroing it, and a credit past the ceiling FAILS rather than
 * saturating. Clamping silently changes the transaction the two sides agreed to, and "you
 * received less than we said" is a support ticket nobody can reconstruct.
 *
 * WHY IT EXISTS BEFORE THE AUTHORITY CUTOVER. Every legitimate mutation routed through here
 * is one that can later increment EconomyRevision in ONE place, be audited in one place, and
 * be reasoned about in one place. Scattering that logic across call sites and then trying to
 * add revisions to it afterwards is the version of this that does not work.
 */

#include <cstdint>
#include <limits>

#include "CharacterRecord.h"
#include "EconomyMigration.h"   // kMaxPlausibleMoney - one ceiling, defined once

namespace Economy
{
/**
 * Why an operation did or did not happen.
 *
 * A bool would answer "did it work" and lose "why", which is exactly what the caller needs
 * to tell the player something useful - "you cannot afford that" and "you do not have that
 * many" are different sentences.
 */
enum class Result
{
    Success,
    InvalidAmount,         // zero or negative where the operation requires positive
    InsufficientFunds,
    InvalidItem,           // id 0 - not an item
    InsufficientQuantity,
    Overflow,              // the result would exceed what the type or the ceiling allows

    // A migrated record already at UINT64_MAX. Produced by CanAdvanceRevision and
    // AdvanceRevision; refusing is deliberate - see the overflow policy at the bottom.
    RevisionExhausted,
};

inline const char* Describe(Result aResult)
{
    switch (aResult)
    {
    case Result::Success:              return "ok";
    case Result::InvalidAmount:        return "invalid amount";
    case Result::InsufficientFunds:    return "insufficient funds";
    case Result::InvalidItem:          return "invalid item";
    case Result::InsufficientQuantity: return "insufficient quantity";
    case Result::Overflow:             return "overflow";
    case Result::RevisionExhausted:    return "economy revision exhausted";
    }

    return "?";
}

// ---------------------------------------------------------------------------------------
// Money
// ---------------------------------------------------------------------------------------

/**
 * Take money. Fails rather than going negative.
 */
inline Result Debit(CharacterRecord& aRecord, int64_t aAmount)
{
    if (aAmount <= 0)
        return Result::InvalidAmount;

    if (aRecord.Money < aAmount)
        return Result::InsufficientFunds;

    aRecord.Money -= aAmount;
    return Result::Success;
}

/**
 * Give money. Fails rather than exceeding the ceiling or wrapping.
 *
 * Two separate guards, and both are needed: the int64 overflow check stops the arithmetic
 * itself being undefined, and the ceiling check stops a balance the rest of the server
 * already refuses to accept. The ceiling is EconomyMigration::kMaxPlausibleMoney - the same
 * constant the live save path and the migration both use, because three different ideas of
 * "too much money" is how a value becomes legal in one place and impossible in another.
 */
inline Result Credit(CharacterRecord& aRecord, int64_t aAmount)
{
    if (aAmount <= 0)
        return Result::InvalidAmount;

    if (aRecord.Money > std::numeric_limits<int64_t>::max() - aAmount)
        return Result::Overflow;

    if (aRecord.Money + aAmount > EconomyMigration::kMaxPlausibleMoney)
        return Result::Overflow;

    aRecord.Money += aAmount;
    return Result::Success;
}

/**
 * Move money between two characters, all-or-nothing.
 *
 * The reason this is one function rather than a Debit followed by a Credit at the call site:
 * a Debit that succeeds followed by a Credit that fails destroys money. The recipient's
 * ceiling is therefore checked BEFORE the payer is touched, and the debit is undone if the
 * credit somehow still fails.
 */
inline Result Transfer(CharacterRecord& aFrom, CharacterRecord& aTo, int64_t aAmount)
{
    if (aAmount <= 0)
        return Result::InvalidAmount;

    if (aFrom.Money < aAmount)
        return Result::InsufficientFunds;

    // Checked before anything moves. Money that leaves one side and cannot arrive at the
    // other has been destroyed, and no error code gives it back.
    if (aTo.Money > std::numeric_limits<int64_t>::max() - aAmount ||
        aTo.Money + aAmount > EconomyMigration::kMaxPlausibleMoney)
    {
        return Result::Overflow;
    }

    const auto debited = Debit(aFrom, aAmount);
    if (debited != Result::Success)
        return debited;

    const auto credited = Credit(aTo, aAmount);
    if (credited != Result::Success)
    {
        aFrom.Money += aAmount;   // put it back - it never left, as far as anyone can tell
        return credited;
    }

    return Result::Success;
}

// ---------------------------------------------------------------------------------------
// Inventory
// ---------------------------------------------------------------------------------------

/**
 * The stack for this item, or null.
 *
 * FIRST MATCH, matching PlayerStore::FindStack and HeldQuantity exactly.
 *
 * That is a real quirk and it is preserved rather than fixed here: nothing merges stacks on
 * the way in, so a client save CAN produce two stacks with the same id, and everything in
 * the server sees only the first. It under-reports, which is the safe direction - nobody can
 * spend what the server cannot see - but it does mean a duplicated stack is unreachable.
 *
 * Normalising it would change what trade believes people own, which is not a change to make
 * inside a mutation primitive. Recorded as a finding instead.
 */
inline CharacterRecord::ItemStack* FindStack(CharacterRecord& aRecord, uint64_t aItemId)
{
    for (auto& stack : aRecord.Inventory)
    {
        if (stack.Id == aItemId)
            return &stack;
    }

    return nullptr;
}

inline uint32_t Held(const CharacterRecord& acRecord, uint64_t aItemId)
{
    for (const auto& stack : acRecord.Inventory)
    {
        if (stack.Id == aItemId)
            return stack.Quantity;
    }

    return 0;
}

/**
 * Give items. Merges into an existing stack, or creates one.
 */
inline Result AddItem(CharacterRecord& aRecord, uint64_t aItemId, uint32_t aQuantity)
{
    if (aItemId == 0)
        return Result::InvalidItem;

    if (aQuantity == 0)
        return Result::InvalidAmount;

    if (auto* pStack = FindStack(aRecord, aItemId))
    {
        // The guard that was missing from MoveAssets until the trade audit: a stack near the
        // uint32 ceiling wraps, and the difference is not moved anywhere - it is destroyed.
        if (pStack->Quantity > std::numeric_limits<uint32_t>::max() - aQuantity)
            return Result::Overflow;

        pStack->Quantity += aQuantity;
        return Result::Success;
    }

    aRecord.Inventory.push_back({aItemId, aQuantity});
    return Result::Success;
}

/**
 * Take items. Fails rather than removing part of what was asked for.
 *
 * An emptied stack is erased rather than left at zero: a zero-quantity stack is not a
 * holding, and leaving one makes "do they have any" answer yes forever.
 */
inline Result RemoveItem(CharacterRecord& aRecord, uint64_t aItemId, uint32_t aQuantity)
{
    if (aItemId == 0)
        return Result::InvalidItem;

    if (aQuantity == 0)
        return Result::InvalidAmount;

    auto* pStack = FindStack(aRecord, aItemId);

    if (!pStack || pStack->Quantity < aQuantity)
        return Result::InsufficientQuantity;

    pStack->Quantity -= aQuantity;

    if (pStack->Quantity == 0)
    {
        for (auto it = aRecord.Inventory.begin(); it != aRecord.Inventory.end(); ++it)
        {
            if (&(*it) == pStack)
            {
                aRecord.Inventory.erase(it);
                break;
            }
        }
    }

    return Result::Success;
}

// ---------------------------------------------------------------------------------------
// Revision - Phase 5 stage 5
//
// THE ONE RULE: revision the TRANSACTION, not the primitive.
//
// Nothing above this line touches EconomyRevision, and that is deliberate rather than an
// omission. Debit, Credit, AddItem and RemoveItem are components - a starter kit is four
// AddItems and a Credit, a trade is a Transfer and several item moves - so a revision inside
// them would advance a character four or five times for one thing that happened. A version
// number that counts function calls instead of state changes is not a version number.
//
// The transaction boundary owns it: validate headroom for every participant, mutate the
// candidates, advance each affected participant exactly once, then commit.
// ---------------------------------------------------------------------------------------

/**
 * Is this record's revision meaningful?
 *
 * Only a MIGRATED record has an authoritative economy, so only a migrated record has a
 * revision worth advancing. The Stage 3 invariant is the definition, and there is
 * deliberately no third state: an unmigrated record is (0, 0) and stays that way through
 * every trade, payment, sale and save until migration itself moves it to revision 1.
 */
inline bool IsMigrated(const CharacterRecord& acRecord)
{
    return acRecord.MigratedAt > 0 && acRecord.EconomyRevision >= 1;
}

/**
 * Could this record take another revision? Ask BEFORE mutating anything.
 *
 * This exists separately from AdvanceRevision because revision capacity is part of
 * TRANSACTION VALIDATION, not part of committing. In a two-party trade, discovering that the
 * second participant cannot advance after the first has already been mutated would mean
 * unwinding a transaction that should never have started.
 *
 * An unmigrated record answers Success: there is nothing to advance, and refusing its
 * transactions would break every legacy character on the server while migration is still
 * inactive.
 */
inline Result CanAdvanceRevision(const CharacterRecord& acRecord)
{
    if (!IsMigrated(acRecord))
        return Result::Success;

    if (acRecord.EconomyRevision == std::numeric_limits<uint64_t>::max())
        return Result::RevisionExhausted;

    return Result::Success;
}

/**
 * Advance exactly once, for one committed logical transaction.
 *
 * Call ONLY from a transaction boundary, and only on state that is about to be committed -
 * never on a candidate that might still be discarded, and never during validation.
 *
 * An unmigrated record is left at 0 and reports success: the transaction really did happen,
 * it simply has no authoritative revision to advance yet.
 *
 * At UINT64_MAX it REFUSES rather than wrapping. A wrapped revision is worse than a stuck
 * one - every stale client view would suddenly compare as current, and the staleness check
 * would invert from protecting the server to endorsing whatever a client last believed. So
 * the impossible case fails loudly instead of silently becoming wrong.
 */
inline Result AdvanceRevision(CharacterRecord& aRecord)
{
    if (!IsMigrated(aRecord))
        return Result::Success;

    if (aRecord.EconomyRevision == std::numeric_limits<uint64_t>::max())
        return Result::RevisionExhausted;

    ++aRecord.EconomyRevision;
    return Result::Success;
}

/**
 * What a client's claimed revision means about its view of the world.
 *
 * OBSERVATION ONLY. Stage 5 measures; Stage 7 enforces.
 *
 * AND EQUALITY PROVES NOTHING ABOUT VALUES. This is the part that is easy to get wrong: a
 * client at the same revision as the server can still send a fabricated balance. Matching
 * revisions mean only "the base version you claim to be working from is not obviously
 * stale" - never "therefore the money you reported is real". Treating Match as permission
 * would recreate client authority wearing a version number.
 */
enum class RevisionView
{
    Match,    // same version - says nothing about whether the values are honest
    Stale,    // the client is behind; whatever it computed was based on old state
    Future,   // the client claims a version that has never existed - impossible, or a lie
    Legacy,   // the record is unmigrated, so there is no revision to compare against
};

inline RevisionView ClassifyClientRevision(const CharacterRecord& acRecord, uint64_t aClientRevision)
{
    if (!IsMigrated(acRecord))
        return RevisionView::Legacy;

    if (aClientRevision == acRecord.EconomyRevision)
        return RevisionView::Match;

    return aClientRevision < acRecord.EconomyRevision ? RevisionView::Stale : RevisionView::Future;
}

inline const char* Describe(RevisionView aView)
{
    switch (aView)
    {
    case RevisionView::Match:  return "current";
    case RevisionView::Stale:  return "stale";
    case RevisionView::Future: return "impossible (ahead of the server)";
    case RevisionView::Legacy: return "legacy (unmigrated)";
    }

    return "?";
}

/*
 * The overflow policy, now IMPLEMENTED above rather than only designed.
 *
 * When revisions begin incrementing, UINT64_MAX must never wrap to 0. A wrapped revision is
 * worse than a stuck one: every stale client view would suddenly compare as current, and the
 * staleness check would silently invert from protecting the server to endorsing whatever a
 * client last believed.
 *
 * Policy when a character's revision would exceed UINT64_MAX:
 *
 *   - REFUSE the mutation with Result::RevisionExhausted
 *   - leave money and inventory unchanged
 *   - emit one error-level audit event naming the character
 *
 * Not a rollover epoch and not a reset - both reintroduce the ambiguity the counter exists
 * to remove. A character reaching 1.8e19 economy mutations is not a real scenario; the point
 * of the policy is that the impossible case fails loudly rather than silently becoming
 * wrong.
 */
} // namespace Economy
