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

    // Reserved for when EconomyRevision starts incrementing. Not produced yet - see the
    // overflow policy note at the bottom of this file.
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

/*
 * FUTURE: EconomyRevision overflow policy, designed now and deliberately not implemented.
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
