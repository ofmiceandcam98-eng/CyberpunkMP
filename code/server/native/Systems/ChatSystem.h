#pragma once

#include "Components/PlayerComponent.h"
#include "CharacterRecord.h"
#include "CallStore.h"
#include "TradeStore.h"

// How far a line of chat carries, in metres.
//
// Range is what makes a roleplay server feel like a place rather than a group chat: two
// people talking on a street corner are not audible across Night City, and a shout is
// meant to attract attention from further than a conversation. The distances are
// deliberately generous compared to real life - too realistic and people cannot find each
// other; too far and every conversation is public.
namespace ChatRange
{
constexpr float kWhisper = 5.f;
constexpr float kLocal = 30.f;
constexpr float kYell = 60.f;
}

// Travels with every message so the client can colour it.
//
// The SERVER decides this, never the client and never the text. Colouring by sniffing for
// "[yells]" in the message would let anyone type that prefix and have ordinary local
// chatter render as a shout - or worse, fake a server notice.
namespace ChatChannel
{
constexpr uint32_t kLocal = 0;
constexpr uint32_t kYell = 1;
constexpr uint32_t kWhisper = 2;
constexpr uint32_t kAdvert = 3;
constexpr uint32_t kServer = 4;
}

// How far a player is placed in front of whoever teleported them.
//
// Far enough not to be standing inside each other - two puppets sharing a spot look
// broken and can shove each other - close enough to be obviously deliberate.
constexpr float kTeleportDistance = 5.f;

// How far short of their original spot /return puts someone.
//
// Not the exact spot. Whatever they were standing in or beside may have moved while they
// were away, and materialising inside it drops people through the world. A few metres
// behind their own footprints, still facing the way they were, is close enough to count
// as "put back" and far enough not to land inside a car that has since parked there.
constexpr float kReturnBackoff = 3.f;

// How far a jailed player may stray before the server puts them back.
//
// Bigger than a cell on purpose. Cyberpunk positions jitter, floors are uneven, and a
// tight leash would yank someone every time they shifted their feet - which reads as the
// game being broken rather than as being locked up. Roomy enough to move around in,
// small enough that leaving is not possible.
constexpr float kJailRadius = 12.f;

struct World;
struct ChatSystem
{
    ChatSystem(gsl::not_null<World*> apWorld);

    // Everyone on the server, wherever they are. For SERVER notices and /advert.
    void Broadcast(String acUsername, String acMessage, uint32_t aChannel = ChatChannel::kServer);

    // One player only. Anything addressed to "you" belongs here: usage text, refusals,
    // and command output. Broadcasting those tells the whole server that someone tried
    // something they were not allowed to, which is both noise and a small humiliation.
    void Tell(const PlayerComponent& acPlayer, const std::string& acMessage);

    // Opens the character-name box on a player's client. Public because the spawn path in
    // Level.cpp needs it too - that is the only route that ever reaches somebody whose
    // character was made before the prompt existed.
    void AskForCharacterName(const PlayerComponent& acPlayer, const CharacterRecord& acCharacter);

    /**
     * A vehicle sale waiting on the buyer.
     *
     * Held in memory rather than on disk deliberately. An offer is a conversation, not
     * property: if the server restarts mid-offer, the right outcome is that nothing
     * happened, and the vehicle's lock is cleared on load for exactly that reason. A
     * persisted offer would resume against players who are no longer here and prices
     * neither of them remembers agreeing to.
     */
    struct PendingSale
    {
        std::string Token;      // also the vehicle's lock - see VehicleStore::Lock
        std::string VehicleId;
        std::string SellerId;
        std::string BuyerId;
        int64_t Price{0};
        int64_t OfferedAt{0};
    };

    // Runs the transfer: funds checked, money moved both ways, ownership changed, both
    // clients corrected. Separate from the command so the same path serves an expiry or a
    // disconnect later without the logic being duplicated.
    void CompleteSale(const PendingSale& acSale, const PlayerComponent& acBuyer);

    // Tells one player what the server thinks their balance is, so their game agrees.
    void PushMoney(const PlayerComponent& acPlayer, int32_t aBalance, const std::string& acReason);

    // Only the client owning a journal can change it, so /quest skip relays rather than acts.
    void SendQuestSkip(flecs::entity aSubject, const std::string& acQuest);

    /**
     * Hands over everything texted to this player's ACTIVE character while they were away.
     *
     * Public because the spawn path calls it - arrival is when somebody is in a position
     * to read anything, and it is also the moment a character switch has finished, which
     * is the other time the answer changes. Resolving the character here rather than at
     * the call site is deliberate: the caller knows about a connection, and which of that
     * account's characters is listening is exactly the question that must not be guessed.
     */
    void DeliverPendingMessages(const PlayerComponent& acPlayer);

    // ------------------------------------------------------------------- calls ----
    //
    // Player-to-player calls only. Nothing here goes near the game's PhoneSystem, which is
    // what keeps the Songbird block intact - see CallStore.h for the full argument.

    /**
     * The connected player whose ACTIVE character is this one, or an empty entity.
     *
     * "Active" is the whole point. Somebody logged in as their other character is, for
     * every purpose here, offline: their phone is a different phone, and ringing it would
     * be ringing a person who is not holding it.
     */
    flecs::entity FindByActiveCharacter(const std::string& acCharacterId) const;

    /**
     * End whatever call this character is in, and tell the other side.
     *
     * Public because three callers outside this file need it and all three are cases where
     * a call must not survive: a disconnect, a crash, and a CHARACTER SWITCH. The last is
     * the one worth naming - a call belongs to the character that made it, so it must end
     * rather than follow the player to their next character.
     */
    void EndCallFor(const std::string& acCharacterId, CallState aState);

    // Rings out calls nobody answered. Driven from the server tick.
    void TickCalls();

    // ------------------------------------------------------------------ trades ----

    // Both sides of a live trade, shown to BOTH people. A private view of a shared deal is
    // how somebody confirms something they never saw.
    void ShowTrade(const TradeSession& acSession);

    /**
     * End this character's trade and tell the other side why.
     *
     * Public because disconnect, death and a character switch all need it. Never ends one
     * that is COMMITTING - see TradeStore::EndFor for why that is not a cancellable state.
     */
    void EndTradeFor(const std::string& acCharacterId, TradeState aState,
                     const std::string& acWhy);

    // Expiry and the continuous distance check. Driven from the server tick.
    void TickTrades();

    // ----------------------------------------------------------------- medical ----

    /**
     * Bleedout, and finishing procedures. Driven from the server tick.
     *
     * The ONE place a downed player becomes dead, and the one place a treatment completes.
     * Both are deadlines rather than countdowns - see Medical.h - so this only ever asks
     * whether a stored timestamp has passed, and a slow or skipped tick delays an outcome
     * without changing it.
     */
    void TickMedical();

    // Tells both sides where a call now is. One place, so a state change cannot be
    // announced to one participant and not the other.
    void AnnounceCall(const CallSession& acSession, CallState aState);

    /**
     * Ring a number, and answer/decline/hang up.
     *
     * THE ONE IMPLEMENTATION. The phone app and the chat fallback both land here, so
     * there is no second copy of the rules for one of them to drift away from - which is
     * the failure the brief's "do not duplicate call logic" is about, and the reason the
     * chat commands were reduced to callers rather than left as a parallel path.
     *
     * Validation lives here rather than at either surface for the same reason: a rule
     * enforced where the player typed it is a rule the other surface gets to break.
     */
    void BeginCall(const PlayerComponent& acPlayer, const std::string& acNumber);

    // 0 answer, 1 decline, 2 hang up. An empty call id means "whatever I am in", which is
    // what the chat fallback has - the phone always sends the id it is displaying.
    void ControlCall(const PlayerComponent& acPlayer, const std::string& acCallId,
                     uint32_t aAction);

    std::vector<PendingSale> m_pendingSales;

    // Only players whose puppet is within aRange of acOrigin.
    //
    // The sender is always included regardless of distance. Not seeing your own message
    // reads as "chat is broken" - and if you are out of everyone else's range, silence is
    // the correct outcome but an alarming one to watch.
    void BroadcastInRange(const std::string& acUsername, const std::string& acMessage,
                          const glm::vec3& acOrigin, float aRange, flecs::entity aSender,
                          uint32_t aChannel);

protected:

    void HandleChatMessageRequest(const PacketEvent<client::ChatMessageRequest>& aMessage);

    // A downed player asking where to get up. The server decides - see /setspawn.
    void HandleRespawnRequest(const PacketEvent<client::RespawnRequest>& aMessage);

    // Stores what the character creator produced. The one message whose appearance the
    // server keeps - the spawn message's describes whatever save the client loaded.
    void HandleSaveCharacterRequest(const PacketEvent<client::SaveCharacterRequest>& aMessage);

    // The selector's trash can. Retires rather than destroys - see PlayerStore.
    void HandleDeleteCharacterRequest(const PacketEvent<client::DeleteCharacterRequest>& aMessage);

    // The phone app dialling and pressing buttons. Both relay to BeginCall/ControlCall,
    // which the chat fallback also uses.
    void HandleCallRequest(const PacketEvent<client::CallRequest>& aMessage);
    void HandleCallControlRequest(const PacketEvent<client::CallControlRequest>& aMessage);

    // "Play as the character in this slot." Answers nothing directly - the real answer is
    // the spawn that follows, or a refusal carried on the character list.
    void HandleSelectCharacterRequest(const PacketEvent<client::SelectCharacterRequest>& aMessage);

    // Send this connection its current character list. The selector redraws from this
    // rather than assuming what a delete did.
    void SendCharacterList(const PlayerComponent& acPlayer, const std::string& acError = {});

    // Splits a chat channel prefix off the front of a line. Returns false when the line
    // named a channel the sender is not allowed to use, or gave it no text, having
    // already told them why.
    bool ResolveChannel(const PlayerComponent& acSender, const std::string& acLine,
                        std::string& aText, float& aRange, bool& aEveryone, uint32_t& aChannel);

    // Returns true when the line was a command and has been dealt with, so it is not
    // also echoed to everyone as normal chat. Permission is checked against the level
    // the server derived from Discord - not anything the client claims.
    bool HandleModerationCommand(flecs::entity aSender, const PlayerComponent& acSender,
                                 const std::string& acLine);

private:

    gsl::not_null<World*> m_pWorld;
};

