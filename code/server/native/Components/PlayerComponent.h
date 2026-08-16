#pragma once

#include "PermissionLevel.h"

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

    const char* GetUsername() const;

    const char* GetDiscordId() const { return DiscordId.c_str(); }
    EPermissionLevel GetLevel() const { return Level; }

    // Levels are ordered, so this is a single comparison rather than a list of roles.
    bool HasAtLeast(EPermissionLevel aRequired) const { return Level >= aRequired; }

    static void Register(flecs::world& aWorld);
};
