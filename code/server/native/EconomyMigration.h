#pragma once

/**
 * The trust-once economy migration: mechanism only, deliberately NOT activated.
 *
 * WHAT IT IS FOR
 *
 * Every character's money and inventory today is whatever a client last declared - see the
 * note in HandleSaveCharacterRequest. Phase 5 replaces that with server authority, and the
 * first question is what each existing character should start from.
 *
 * There is no answer that can be computed. No historical ledger exists, so "what SHOULD this
 * player have" is unknowable, and any formula invented to guess it would take possessions
 * from honest players to punish an exploit nobody can prove happened. So the policy is
 * TRUST ONCE: whatever the server currently has persisted becomes the opening authoritative
 * balance, unchanged, and from that moment the server owns it.
 *
 * The consequence is stated plainly rather than buried: anyone who exploited before the line
 * keeps what they took. That is the price of not destroying legitimate possessions, and it
 * is a decision for the server owner to make with the [MONEY] audit trail in front of them -
 * not something this code should quietly decide by resetting people.
 *
 * WHY THIS IS SPLIT OUT OF PlayerStore
 *
 * Two reasons, and the second is the important one:
 *
 *   - PlayerStore drags in glm and spdlog, which is why trade_test restates its algorithm
 *     instead of including it. The rules below carry every invariant of the migration, so
 *     they are the part that most needs testing against the REAL code rather than a copy
 *     that can drift.
 *   - Separating "what should happen to one record" from "how do we commit a whole file
 *     transactionally" keeps each honest. PlayerStore owns the transaction; this owns the
 *     policy.
 *
 * NOTHING CALLS THIS DURING NORMAL OPERATION. Not startup, not load, not connect, not
 * authentication, not character select, not spawn, not save, not the flush tick, not
 * shutdown. It exists, it is tested, and crossing the boundary is a separate deliberate act.
 *
 * The reason for that is worth keeping: while HandleSaveCharacterRequest still accepts a
 * client-declared balance, stamping MoneyMigratedAt would be a lie. The mark would say "the
 * server owns this now" while the client could still rewrite it on the next save. The
 * migration and the authority cutover have to happen together, and until then this stays
 * inert.
 */

#include <algorithm>   // std::find, in InspectLegacyMetadata
#include <cstdint>
#include <string>
#include <utility>     // std::move, in InspectLegacyMetadata - MSVC supplies it
                       // transitively and GCC does not, which is the whole point of
                       // tools\CheckIncludes.ps1
#include <vector>

#include <nlohmann/json.hpp>   // InspectLegacyMetadata reads the raw document

#include "CharacterRecord.h"

namespace EconomyMigration
{
/**
 * The upper bound on a plausible balance.
 *
 * Deliberately the SAME constant the live save path already refuses above - see the money
 * guard in HandleSaveCharacterRequest. Migration must not be more permissive than the
 * running server, or it would bless a balance that ordinary play would have rejected; and it
 * must not be stricter, or it would block characters the server has been happily accepting.
 * One rule, applied in both places.
 */
inline constexpr int64_t kMaxPlausibleMoney = 1'000'000'000;

enum class State
{
    // MoneyMigratedAt == 0 && MoneyRevision == 0. The normal starting state.
    Unmigrated,

    // MoneyMigratedAt > 0 && MoneyRevision >= 1. Already done; must not be touched again.
    Migrated,

    /**
     * The two halves disagree - one is set and the other is not.
     *
     * NOT REPAIRED, deliberately. Either of these means something wrote one field without
     * the other, and that is a bug somewhere rather than a state to tidy away. Silently
     * "fixing" it would destroy the evidence and could mark a character migrated that never
     * was, or re-open one that already crossed. It is reported so a person looks.
     */
    Inconsistent,

    /**
     * Unmigrated, but its persisted economy fails a rule the live server already enforces.
     *
     * Blocked rather than corrected: migration is the wrong moment to start changing
     * people's balances, and a record in this state needs a human to decide what happened.
     */
    Blocked,
};

/**
 * Why a record was classified as it was, for the operator's report.
 *
 * Empty for the ordinary states - a reason only exists when something is wrong.
 */
struct Classification
{
    State Result{State::Unmigrated};
    std::string Reason;
};

/**
 * Legacy metadata found in a persisted file, from before the stage 6 rename.
 *
 * WHY THIS EXISTS AT ALL, when the rename is supposed to be free.
 *
 * `EconomyRevision` / `MigratedAt` became `MoneyRevision` / `MoneyMigratedAt` because money
 * and inventory stopped crossing the authority boundary together. The rename is safe for
 * every record that exists, and safe for a precise reason rather than a hopeful one: no
 * migration has ever run, so every persisted value is 0, and a record carrying only the old
 * keys loads with the new ones defaulted to 0 - not migrated, which is exactly correct.
 *
 * A NONZERO old key breaks that reasoning. It would mean a record crossed a boundary under
 * the OLD meaning, where the mark claimed money AND inventory. Reading that as
 * `MoneyMigratedAt` would silently narrow what it asserted; reading it as both would claim
 * inventory authority the server does not have. Neither is knowable from the number.
 *
 * So it is NOT reinterpreted, in either direction. It is detected and reported, and
 * migration refuses the record until a human decides what it meant. Production is expected
 * to contain none of these - this exists so that expectation is TESTABLE rather than
 * assumed, and so the day it is wrong is a loud day rather than a quiet one.
 */
struct LegacyMetadata
{
    // Old keys present at all - normal for any file written before the rename, and harmless
    // on its own.
    bool Present{false};

    // Old keys present AND nonzero. This is the case nothing may guess at.
    bool Nonzero{false};

    // CharacterIds carrying nonzero legacy metadata, so a human knows where to look.
    std::vector<std::string> Characters;
};

/**
 * Scan a parsed players.json for pre-rename metadata.
 *
 * Takes the RAW JSON rather than a CharacterRecord on purpose: deserialization drops keys
 * the struct no longer declares, so by the time a record exists the evidence is gone. This
 * has to look at the file as written.
 *
 * Pure: reads, changes nothing, and is safe on a malformed document - anything that is not
 * shaped like a player array is simply not scanned, because reporting a parse opinion is
 * PlayerStore's job and not this function's.
 */
inline LegacyMetadata InspectLegacyMetadata(const nlohmann::json& acDocument)
{
    LegacyMetadata found;

    if (!acDocument.is_array())
        return found;

    for (const auto& player : acDocument)
    {
        if (!player.is_object() || !player.contains("Characters"))
            continue;

        const auto& characters = player["Characters"];
        if (!characters.is_array())
            continue;

        for (const auto& character : characters)
        {
            if (!character.is_object())
                continue;

            // Both old names, checked independently: an inconsistent record (one set, one
            // not) is exactly the shape that most needs a human, so neither is treated as
            // implying the other.
            for (const char* legacy : {"EconomyRevision", "MigratedAt"})
            {
                if (!character.contains(legacy))
                    continue;

                found.Present = true;

                const auto& value = character[legacy];
                if (!value.is_number())
                    continue;

                // Compared as a double so one branch covers both the unsigned revision and
                // the signed timestamp without caring which key this is.
                if (value.get<double>() != 0.0)
                {
                    found.Nonzero = true;

                    auto id = character.contains("CharacterId") && character["CharacterId"].is_string()
                                  ? character["CharacterId"].get<std::string>()
                                  : std::string{"(unnamed)"};

                    if (std::find(found.Characters.begin(), found.Characters.end(), id) ==
                        found.Characters.end())
                    {
                        found.Characters.push_back(std::move(id));
                    }
                }
            }
        }
    }

    return found;
}

/**
 * What state is this record in, and can it be migrated?
 *
 * Pure: reads the record, changes nothing.
 */
inline Classification Classify(const CharacterRecord& acRecord)
{
    const bool stamped = acRecord.MoneyMigratedAt > 0;
    const bool revised = acRecord.MoneyRevision >= 1;

    if (stamped != revised)
    {
        return {State::Inconsistent,
                stamped ? "MoneyMigratedAt is set but MoneyRevision is 0"
                        : "MoneyRevision is set but MoneyMigratedAt is 0"};
    }

    if (stamped)
        return {State::Migrated, {}};

    // Unmigrated. The persisted economy has to satisfy what the live server already demands
    // of a save, or migrating it would bless something ordinary play would have refused.
    if (acRecord.Money < 0)
        return {State::Blocked, "negative balance"};

    if (acRecord.Money > kMaxPlausibleMoney)
        return {State::Blocked, "balance above the plausible ceiling"};

    for (const auto& stack : acRecord.Inventory)
    {
        // The save path already skips these rather than storing them, so a persisted one
        // means the record predates that guard or was hand-edited. Either way a person
        // should see it rather than have it quietly migrated.
        if (stack.Id == 0)
            return {State::Blocked, "an item stack with no id"};

        if (stack.Quantity == 0)
            return {State::Blocked, "an item stack with zero quantity"};
    }

    return {State::Unmigrated, {}};
}

/**
 * Cross the boundary for ONE record.
 *
 * Money and Inventory are not touched - that is the whole of "trust once". Only the two
 * metadata fields change.
 *
 * IDEMPOTENT: a record that is already migrated is returned untouched, keeping its original
 * timestamp and revision. Running the migration twice must not give somebody a second
 * opening balance or move the line they crossed.
 *
 * Returns true if this call changed the record.
 */
inline bool Apply(CharacterRecord& aRecord, int64_t aNowSeconds)
{
    if (Classify(aRecord).Result != State::Unmigrated)
        return false;

    aRecord.MoneyMigratedAt = aNowSeconds;
    aRecord.MoneyRevision = 1;

    return true;
}

/**
 * What a migration would do, without doing any of it.
 *
 * Counts rather than lists for the ordinary states - an operator needs "how many", and a
 * line per character on a populated server is noise. Ids ARE listed for the two states that
 * need a human, because those are the ones somebody has to go and look at.
 */
struct Report
{
    size_t Total{0};
    size_t AlreadyMigrated{0};
    size_t Candidates{0};
    size_t Inconsistent{0};
    size_t Blocked{0};

    // Bounded: an operator cannot act on a thousand ids, and a report that dumps the whole
    // population when something goes wrong is a report nobody reads.
    static constexpr size_t kMaxListed = 20;

    std::vector<std::string> NeedsAttention;

    /**
     * ALL OR NOTHING. A migration only proceeds when every record is either already done or
     * cleanly migratable.
     *
     * The alternative - migrate the good ones, skip the rest - leaves the server in a state
     * nobody can reason about: half the population owns its economy and half does not, and
     * the operator has to remember which. With a whole-file store there is also no
     * meaningful sense in which the skipped ones were "not committed", since the same file
     * write carries all of them.
     */
    bool CanCommit() const { return Inconsistent == 0 && Blocked == 0; }
};

/**
 * Classify a whole population. Reads only.
 */
inline Report Inspect(const std::vector<const CharacterRecord*>& acRecords)
{
    Report report;
    report.Total = acRecords.size();

    for (const auto* pRecord : acRecords)
    {
        if (!pRecord)
            continue;

        const auto classified = Classify(*pRecord);

        switch (classified.Result)
        {
        case State::Migrated:
            ++report.AlreadyMigrated;
            break;

        case State::Unmigrated:
            ++report.Candidates;
            break;

        case State::Inconsistent:
        case State::Blocked:
            if (classified.Result == State::Inconsistent)
                ++report.Inconsistent;
            else
                ++report.Blocked;

            if (report.NeedsAttention.size() < Report::kMaxListed)
            {
                const auto& id = pRecord->CharacterId;

                report.NeedsAttention.push_back((id.empty() ? std::string("<no id>") : id) + ": " +
                                                classified.Reason);
            }
            break;
        }
    }

    return report;
}
} // namespace EconomyMigration
