#pragma once

#include "Components/PlayerComponent.h"

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

