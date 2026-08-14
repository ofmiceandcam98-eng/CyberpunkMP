#pragma once

#include "Scripting/IConfig.h"
#include "PermissionLevel.h"

struct FlecsConfig
{
    bool Enabled{true};
    uint16_t Port{27750};
    std::string IpAddress{"127.0.0.1"};

    bool IsEnabled() const { return Enabled; }
    uint16_t GetPort() const { return Port; }
    const char* GetIpAddress() const { return IpAddress.c_str(); }

    // _WITH_DEFAULT, not the plain macro: the plain one uses at() and throws when a key is
    // absent, so adding any field would stop every existing server from starting until its
    // config was hand-edited. With defaults, an older config simply picks up the new field.
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(FlecsConfig, Enabled, Port, IpAddress)
};

// Discord-backed player identity.
//
// The client never asserts who it is. It presents an OAuth access token, and the SERVER
// asks Discord who that token belongs to and whether they are in the guild. Anything the
// client claims about itself is untrusted by construction - a launcher ships to players
// and can be patched, and raw packets can be sent without it.
struct DiscordConfig
{
    bool Enabled{false};
    // The Discord server players must belong to. Enable Developer Mode in Discord, then
    // right-click your server -> Copy Server ID.
    std::string GuildId{};
    // Reject anyone who is not a member. Off means a valid Discord account is enough.
    bool RequireMembership{true};

    // The one account that always has full access, regardless of roles. Set this to your
    // own Discord id: it is the way back in if the role setup is ever broken, and it
    // cannot be removed by someone editing roles in Discord.
    std::string OwnerId{};

    // Discord role id -> permission level. Role ids, not names: names get renamed, and a
    // rename should not silently grant or revoke anything.
    //
    //   "Roles": { "1234567890": "admin", "9876543210": "moderator" }
    //
    // A player with several mapped roles gets the highest.
    std::map<std::string, std::string> Roles{};

    bool IsEnabled() const { return Enabled; }
    const char* GetGuildId() const { return GuildId.c_str(); }
    bool GetRequireMembership() const { return RequireMembership; }

    // Resolve a set of Discord role ids into a permission level.
    EPermissionLevel ResolveLevel(const std::string& acUserId, const std::vector<std::string>& acRoleIds) const
    {
        if (!OwnerId.empty() && acUserId == OwnerId)
            return EPermissionLevel::kOwner;

        auto level = EPermissionLevel::kPlayer;

        for (const auto& roleId : acRoleIds)
        {
            const auto it = Roles.find(roleId);
            if (it == Roles.end())
                continue;

            auto mapped = EPermissionLevel::kPlayer;

            if (it->second == "owner")          mapped = EPermissionLevel::kOwner;
            else if (it->second == "admin")     mapped = EPermissionLevel::kAdmin;
            else if (it->second == "moderator") mapped = EPermissionLevel::kModerator;

            // Highest wins, so adding a junior role never demotes anyone.
            if (mapped > level)
                level = mapped;
        }

        return level;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DiscordConfig, Enabled, GuildId, RequireMembership, OwnerId, Roles)
};

struct Config : IConfig
{
    std::string Name{"Default"};
    std::string Description{};
    std::string ApiKey{};
    std::string IconUrl{};
    uint16_t MaxPlayer{4};
    std::string Tags{};
    bool Public{false};
    uint16_t Port{11778};
    uint16_t WebPort{11779};
    uint16_t TickRate{60};

    // Position updates per second, per player.
    //
    // This is the ceiling on how smooth anyone else can possibly look: at 10 there was a
    // hundred milliseconds between samples, and the client's simulation delay is derived
    // from it too (50ms + 1500/rate), so a low rate cost both smoothness AND
    // responsiveness. Interpolation can hide the gap but it cannot invent detail that was
    // never sent, and Cam's group described the result as other players teleporting
    // rather than walking.
    //
    // A MoveEntityRequest is a few dozen bytes. Thirty of them a second, per player, is
    // nothing next to what the connection already carries.
    uint16_t UpdateRate{30};
    std::string Password{};
    FlecsConfig Flecs{};
    DiscordConfig Discord{};

    const char* GetName() const override { return Name.c_str(); }
    const char* GetDescription() const override { return Description.c_str(); }
    const char* GetApiKey() const override { return ApiKey.c_str(); }
    const char* GetIconUrl() const override { return IconUrl.c_str(); }
    uint16_t GetMaxPlayer() const override { return MaxPlayer; }
    const char* GetTags() const override { return Tags.c_str(); }
    bool GetPublic() const override { return Public; }
    uint16_t GetPort() const override { return Port; }
    uint16_t GetWebPort() const override { return WebPort; }
    uint16_t GetTickRate() const override { return TickRate; }
    uint16_t GetUpdateRate() const override { return UpdateRate; }
    const char* GetPassword() const { return Password.c_str(); }
    const FlecsConfig& GetFlecsConfig() const { return Flecs; }
    const DiscordConfig& GetDiscordConfig() const { return Discord; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Config, Name, Description, IconUrl, MaxPlayer, Tags, TickRate, UpdateRate, Public, Port, Password, ApiKey, Flecs, Discord)
};