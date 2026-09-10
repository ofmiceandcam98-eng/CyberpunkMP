#pragma once

#include "PermissionLevel.h"
#include <string>

struct PlayerComponent
{
    ConnectionId Connection;
    flecs::entity Puppet;
    std::string Username;

    // Established by the server from Discord at connect time, never sent by the client.
    std::string DiscordId;
    EPermissionLevel Level{EPermissionLevel::kPlayer};

    // The name typed with /character save, held until the client sends the appearance
    // back. The reply carries no idea what the player typed, so it is remembered here.
    std::string PendingCharacterName;

    // Kept so membership and roles can be re-checked while the player is connected.
    // Without this, a Discord ban would only take effect the next time they tried to
    // join - which is no use at all if they are already in and causing the problem.
    std::string DiscordToken;

    // Where this player was standing before staff teleported them.
    //
    // Being summoned across the map is disruptive on a roleplay server - someone may be
    // mid-scene, or halfway through a drive. Recording the spot is what makes /tp
    // reversible rather than something that costs the player their evening.
    bool HasReturnPoint{false};
    glm::vec3 ReturnPosition{};
    glm::vec3 ReturnRotation{};

    /**
     * Flood control for chat and commands.
     *
     * Both briefs ask for this - the phone's section 27 ("MAX_MESSAGES_PER_SECOND... prevent
     * spam without making normal RP communication annoying") and the trade brief's section
     * 30 - and there was no rate limiting on chat at all. Quickhacks have per-hack
     * cooldowns and movement rejects floods; the one path a client can drive as fast as it
     * likes was the one that copies text to every player in range AND writes it to disk.
     *
     * A SLIDING WINDOW, not a per-message delay. A fixed minimum gap between messages
     * punishes normal conversation - two people talking quickly is the thing an RP server
     * exists for - while still allowing a sustained stream at exactly the limit. A budget
     * per window lets someone fire off a few lines naturally and only bites on a machine.
     *
     * Kept on the component rather than in a map keyed by connection, so it cannot outlive
     * the player or leak when they disconnect.
     */
    int64_t ChatWindowStartMs{0};
    uint32_t ChatInWindow{0};

    // So the refusal cannot itself be spammed. Told once per window, then silence - a
    // flooding client would otherwise get a reply per message, which is the same denial of
    // service with the server doing the work.
    bool ChatFloodWarned{false};

    /**
     * The same, for voice frames - and a separate budget, deliberately.
     *
     * Voice and chat are nothing like each other in rate. A legitimate client produces
     * about fifty frames a second (20ms Opus), where a person types a handful of lines a
     * minute, so one shared limit would either throttle speech or leave chat wide open.
     * Endpoint-specific limits, sized to the legitimate rate of each.
     *
     * The ceiling is 100/s - double what the client actually sends, so no real speaker can
     * reach it, while bounding what one connection can make the server relay. This is
     * purely an INBOUND flood guard: it does not change the voice cadence, and a normal
     * speaker never touches it.
     */
    int64_t VoiceWindowStartMs{0};
    uint32_t VoiceInWindow{0};
    bool VoiceFloodWarned{false};

    const char* GetUsername() const;

    const char* GetDiscordId() const { return DiscordId.c_str(); }
    EPermissionLevel GetLevel() const { return Level; }

    // Levels are ordered, so this is a single comparison rather than a list of roles.
    bool HasAtLeast(EPermissionLevel aRequired) const { return Level >= aRequired; }

    static void Register(flecs::world& aWorld);
};
