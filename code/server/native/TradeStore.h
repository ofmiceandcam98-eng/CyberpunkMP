#pragma once

/**
 * Player-to-player trading.
 *
 * THE GOVERNING RULE, and everything here follows from it: a trade never CREATES value. It
 * moves value that already exists from one owner to another. Any change that could make
 * the total before a trade differ from the total after is wrong, however convenient.
 *
 * WHERE THE AUTHORITY LIVES
 *
 * Not here. PlayerStore owns money and possessions, and PlayerStore::ApplyTrade is what
 * actually moves them - atomically, both sides, one flush. This file decides WHETHER an
 * exchange should happen and holds the conversation while two people agree on it. Two
 * things that could both move money would be two authorities, and they would disagree the
 * first time either changed.
 *
 * SESSIONS ARE IN MEMORY, DELIBERATELY
 *
 * A trade is a conversation, not property. If the server restarts mid-negotiation the
 * right outcome is that nothing happened - which an empty session list already means, with
 * nothing to reconcile and no reservations to release. The same reasoning as calls and as
 * ChatSystem::PendingSale.
 *
 * That is also why COMMITTING is the one state that must not simply vanish: everything
 * before it is reversible by doing nothing, and the commit itself is one call into
 * PlayerStore that either happened or did not.
 *
 * RESERVATIONS ARE THE OFFERS THEMSELVES
 *
 * There is no separate reservation table, and that is a design decision rather than a
 * shortcut. A reservation table and a set of offers are two representations of one fact,
 * and they drift: an offer changed without its reservation updated is money promised twice,
 * which is precisely the duplication this system exists to prevent. Asking "what has this
 * character promised" by summing their live offers cannot disagree with the offers.
 */

#include <string>
#include <vector>
#include <list>
#include <algorithm>
#include <chrono>
#include <random>

#include "CharacterRecord.h"
#include "PlayerStore.h"

/**
 * How close two people must be, in metres.
 *
 * Trading is a thing characters do face to face. Without a distance rule it becomes a menu
 * two people open from opposite ends of Night City, which is both an immersion problem and
 * the mechanism every remote-scam relies on.
 */
inline constexpr float kTradeDistance = 4.f;

// How long an unanswered invitation stands, and how long an open trade may sit idle.
inline constexpr int64_t kTradeRequestSeconds = 30;
inline constexpr int64_t kTradeOpenSeconds = 300;

// A ceiling on how many distinct stacks one side may offer. Not a gameplay limit - it
// bounds what a single request can make the server validate and write.
inline constexpr size_t kTradeItemLimit = 24;

enum class TradeState : uint32_t
{
    Requested = 0,   // sent, not yet accepted
    Open = 1,        // both in, offers editable
    Committing = 2,  // validated, mid-apply - never cancellable
    Completed = 3,
    Cancelled = 4,
    Expired = 5,
    Failed = 6,
};

inline bool IsTradeOver(TradeState aState)
{
    return aState != TradeState::Requested && aState != TradeState::Open &&
           aState != TradeState::Committing;
}

inline const char* TradeStateName(TradeState aState)
{
    switch (aState)
    {
    case TradeState::Requested: return "requested";
    case TradeState::Open: return "open";
    case TradeState::Committing: return "committing";
    case TradeState::Completed: return "completed";
    case TradeState::Cancelled: return "cancelled";
    case TradeState::Expired: return "expired";
    case TradeState::Failed: return "failed";
    }

    return "?";
}

struct TradeOffer
{
    int64_t Money{0};
    std::vector<CharacterRecord::ItemStack> Items;

    /**
     * Bumped on EVERY change, and the reason confirmations are safe.
     *
     * A confirmation names the version it is confirming. If the offer has moved on, the
     * confirmation is stale and is refused rather than applied - which is what stops
     * somebody agreeing to one deal and receiving another, whether by a race or by a
     * partner who edits at the last instant.
     */
    uint32_t Version{0};

    bool Confirmed{false};

    void Touch()
    {
        ++Version;
        Confirmed = false;
    }
};

struct TradeSession
{
    std::string TradeId;

    // CharacterIds. A player's two characters are two traders, like everywhere else.
    std::string A;
    std::string B;

    TradeOffer OfferA;
    TradeOffer OfferB;

    TradeState State{TradeState::Requested};

    int64_t CreatedAt{0};
    int64_t TouchedAt{0};

    bool Involves(const std::string& acCharacterId) const
    {
        return A == acCharacterId || B == acCharacterId;
    }

    std::string Other(const std::string& acCharacterId) const
    {
        if (A == acCharacterId)
            return B;
        if (B == acCharacterId)
            return A;
        return {};
    }

    TradeOffer* OfferFor(const std::string& acCharacterId)
    {
        if (A == acCharacterId)
            return &OfferA;
        if (B == acCharacterId)
            return &OfferB;
        return nullptr;
    }

    const TradeOffer* OfferFor(const std::string& acCharacterId) const
    {
        return const_cast<TradeSession*>(this)->OfferFor(acCharacterId);
    }

    bool BothConfirmed() const { return OfferA.Confirmed && OfferB.Confirmed; }
};

class TradeStore
{
public:
    /**
     * The trade this character is in, or nullptr. One at a time.
     *
     * One is not a simplification - it is what makes reservations correct. With two live
     * trades a character could promise the same 8,000 eddies in both, and each trade would
     * validate on its own and be right.
     */
    TradeSession* Active(const std::string& acCharacterId)
    {
        if (acCharacterId.empty())
            return nullptr;

        for (auto& session : m_sessions)
        {
            if (!IsTradeOver(session.State) && session.Involves(acCharacterId))
                return &session;
        }

        return nullptr;
    }

    const TradeSession* Active(const std::string& acCharacterId) const
    {
        return const_cast<TradeStore*>(this)->Active(acCharacterId);
    }

    TradeSession& Begin(const std::string& acFrom, const std::string& acTo)
    {
        TradeSession session;
        session.TradeId = GenerateTradeId();
        session.A = acFrom;
        session.B = acTo;
        session.State = TradeState::Requested;
        session.CreatedAt = Now();
        session.TouchedAt = session.CreatedAt;

        m_sessions.push_back(std::move(session));
        return m_sessions.back();
    }

    /**
     * How much money this character has PROMISED in a live trade.
     *
     * Summed from the offers rather than tracked, so it cannot disagree with them. Any path
     * that spends money must subtract this, or the reservation means nothing - see
     * PlayerStore::AvailableMoney.
     */
    int64_t ReservedMoney(const std::string& acCharacterId) const
    {
        int64_t reserved = 0;

        for (const auto& session : m_sessions)
        {
            if (IsTradeOver(session.State))
                continue;

            if (const auto* pOffer = session.OfferFor(acCharacterId))
                reserved += pOffer->Money;
        }

        return reserved;
    }

    // The same question for one item. Spending paths that move items owe it the same check.
    uint32_t ReservedItems(const std::string& acCharacterId, uint64_t aItemId) const
    {
        uint32_t reserved = 0;

        for (const auto& session : m_sessions)
        {
            if (IsTradeOver(session.State))
                continue;

            const auto* pOffer = session.OfferFor(acCharacterId);
            if (!pOffer)
                continue;

            for (const auto& stack : pOffer->Items)
            {
                if (stack.Id == aItemId)
                    reserved += stack.Quantity;
            }
        }

        return reserved;
    }

    void End(TradeSession& aSession, TradeState aState)
    {
        if (IsTradeOver(aSession.State))
            return;

        aSession.State = aState;
    }

    TradeSession* EndFor(const std::string& acCharacterId, TradeState aState)
    {
        auto* pSession = Active(acCharacterId);

        if (!pSession)
            return nullptr;

        /**
         * A trade that is COMMITTING is not cancellable, by anybody or anything.
         *
         * Disconnects, deaths and timeouts all land here, and all of them are correct
         * reasons to cancel a trade that is still being negotiated. None of them is a
         * reason to abandon one that is mid-apply: the commit is a single call into
         * PlayerStore which either moved both sides or moved neither, and "cancelling" it
         * from outside could only ever mean losing track of which.
         */
        if (pSession->State == TradeState::Committing)
            return nullptr;

        End(*pSession, aState);
        return pSession;
    }

    // Trades nobody is attending any more. Returned rather than ended, so the caller can
    // tell both sides before the session goes.
    std::vector<TradeSession*> Expired()
    {
        std::vector<TradeSession*> expired;

        const auto now = Now();

        for (auto& session : m_sessions)
        {
            if (IsTradeOver(session.State) || session.State == TradeState::Committing)
                continue;

            const auto limit =
                session.State == TradeState::Requested ? kTradeRequestSeconds : kTradeOpenSeconds;

            if (now - session.TouchedAt >= limit)
                expired.push_back(&session);
        }

        return expired;
    }

    void Sweep()
    {
        m_sessions.remove_if([](const TradeSession& acSession)
                             { return IsTradeOver(acSession.State); });
    }

    // Every trade still running, for the checks that have to sweep all of them - distance,
    // most importantly, which is a property of a PAIR and cannot be asked of one character.
    std::vector<TradeSession*> Live()
    {
        std::vector<TradeSession*> live;

        for (auto& session : m_sessions)
        {
            if (!IsTradeOver(session.State))
                live.push_back(&session);
        }

        return live;
    }

    static int64_t Now()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

private:
    static std::string GenerateTradeId()
    {
        static std::mt19937_64 engine{std::random_device{}()};

        char buffer[17] = {};
        std::snprintf(buffer, sizeof(buffer), "%016llx",
                      static_cast<unsigned long long>(engine()));

        return buffer;
    }

    /**
     * A list, not a vector - Active(), EndFor() and Expired() all hand out pointers, and a
     * caller holds one while telling both sides, which can start another trade. Same
     * reasoning as CallStore, and the same bug avoided: a vector would reallocate and
     * dangle every outstanding pointer, intermittently, only under load.
     */
    std::list<TradeSession> m_sessions;
};
