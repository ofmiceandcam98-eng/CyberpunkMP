#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

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

    // Discord role -> permission level. Keys may be a role ID or a role NAME.
    //
    //   "Roles": { "dev": "admin", "1234567890": "moderator" }
    //
    // Ids were originally the only option, on the reasoning that a rename should not
    // silently grant or revoke anything. That reasoning was right and it made the feature
    // unusable: nobody knows their role ids, the map stayed empty, and the "dev" role in
    // Cam's Discord had no privileges in game for weeks because configuring it meant
    // turning on Developer Mode and copying a snowflake.
    //
    // Names are resolved against the guild's real role list, fetched with the bot token,
    // so a name here means the role that actually has that name right now. If no bot
    // token is available only ids work, and every unmapped role id a player carries is
    // logged so it can be copied from there.
    //
    // A player with several mapped roles gets the highest.
    std::map<std::string, std::string> Roles{};

    // Where the bot token lives. Read by the server at runtime, never stored in this file
    // - a token in a config that gets pasted into chat for debugging is a token that has
    // to be regenerated. The environment variable NCO_DISCORD_BOT_TOKEN wins if set.
    //
    // tools/.discord-bot is where the Discord announcer already keeps it, and it is
    // gitignored.
    std::string BotTokenFile{};

    // Where to write the resolved role -> level map, so the launcher can show the same
    // people the same controls the game gives them.
    //
    // Contains role ids, names and levels - all of which are visible to any member of the
    // Discord already - and no token. Point it at publish\roles.json and Ship publishes it
    // with the release, which is how the launcher gets it.
    std::string RolesFile{};

    bool IsEnabled() const { return Enabled; }
    const char* GetGuildId() const { return GuildId.c_str(); }
    bool GetRequireMembership() const { return RequireMembership; }

    static EPermissionLevel ParseLevel(const std::string& acName)
    {
        if (acName == "owner")     return EPermissionLevel::kOwner;
        if (acName == "admin")     return EPermissionLevel::kAdmin;
        if (acName == "moderator") return EPermissionLevel::kModerator;
        if (acName == "support")   return EPermissionLevel::kSupport;

        // Both spellings, because roles.json is written by a person and "event staff" is
        // what the Discord role is called while "eventstaff" is what somebody types.
        if (acName == "eventstaff" || acName == "event staff")
            return EPermissionLevel::kEventStaff;

        return EPermissionLevel::kPlayer;
    }

    static std::string Lower(std::string aValue)
    {
        std::transform(aValue.begin(), aValue.end(), aValue.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return aValue;
    }

    // Roles that mean what they say, without anyone configuring anything.
    //
    // A server whose "dev" role does nothing until someone finds its snowflake is a server
    // where the role does nothing, in practice, forever. Anything in Roles overrides these
    // - this is a floor, not a ceiling.
    //
    // Only someone who can already manage roles in the Discord can create a role with one
    // of these names, and they are trusted with far more than this by definition.
    static EPermissionLevel DefaultForName(const std::string& acLowerName)
    {
        if (acLowerName == "owner")     return EPermissionLevel::kOwner;

        if (acLowerName == "dev" || acLowerName == "devs" || acLowerName == "developer" ||
            acLowerName == "developers" || acLowerName == "admin" || acLowerName == "admins" ||
            acLowerName == "administrator" ||
            // Cam replaced the old moderator/admin roles on 2026-09-02. SENIOR MODERATOR is
            // the top staff rank below dev and carries the full admin set - bans, /rename,
            // world state.
            acLowerName == "senior moderator" || acLowerName == "senior mod" ||
            acLowerName == "seniormoderator" || acLowerName == "senior moderators")
            return EPermissionLevel::kAdmin;

        /**
         * EVENT STAFF. Everything a moderator has, plus the event tools.
         *
         * A rung of its own rather than the moderator rung, because Cam's rule is that
         * event staff can spawn things and support cannot - and with both on kModerator
         * there is no way to express that. See EPermissionLevel::kEventStaff.
         *
         * Matched before the moderator block below, since "event staff" also contains
         * "staff" and the plain "staff" entry there would otherwise claim it first and
         * quietly demote them.
         */
        if (acLowerName == "event staff" || acLowerName == "eventstaff" ||
            acLowerName == "events" || acLowerName == "event team")
            return EPermissionLevel::kEventStaff;

        if (acLowerName == "mod" || acLowerName == "mods" || acLowerName == "moderator" ||
            acLowerName == "moderators" || acLowerName == "staff")
            return EPermissionLevel::kModerator;

        /**
         * SUPPORT: extra character slots and nothing else. Cam's rule, 2026-09-02.
         *
         * It used to return kModerator, because no support level existed and the slot rule
         * needed something to test - which handed ticket staff the entire moderator toolkit
         * as a side effect of wanting to give them slots. kSupport sits BELOW moderator now,
         * so every `>= kModerator` check excludes them without anyone having to remember to.
         *
         * Listed by name rather than left to roles.json for the reason this whole function
         * exists: a role that does nothing until somebody finds its snowflake does nothing,
         * in practice, forever.
         */
        if (acLowerName == "support" || acLowerName == "support team")
            return EPermissionLevel::kSupport;

        return EPermissionLevel::kPlayer;
    }

    /**
     * Resolve a player's Discord roles into a permission level.
     *
     * acRoleNames maps role id -> lowercased role name, as fetched from the guild. It may
     * be empty, in which case only id entries in Roles can match.
     */
    EPermissionLevel ResolveLevel(const std::string& acUserId,
                                  const std::vector<std::string>& acRoleIds,
                                  const std::map<std::string, std::string>& acRoleNames = {}) const
    {
        if (!OwnerId.empty() && acUserId == OwnerId)
            return EPermissionLevel::kOwner;

        auto level = EPermissionLevel::kPlayer;

        for (const auto& roleId : acRoleIds)
        {
            auto mapped = EPermissionLevel::kPlayer;

            // An explicit id entry is the most specific thing anyone can write, so it wins.
            if (const auto byId = Roles.find(roleId); byId != Roles.end())
            {
                mapped = ParseLevel(Lower(byId->second));
            }
            else if (const auto named = acRoleNames.find(roleId); named != acRoleNames.end())
            {
                if (const auto byName = Roles.find(named->second); byName != Roles.end())
                    mapped = ParseLevel(Lower(byName->second));
                else
                    mapped = DefaultForName(named->second);
            }

            // Highest wins, so adding a junior role never demotes anyone.
            if (mapped > level)
                level = mapped;
        }

        return level;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DiscordConfig, Enabled, GuildId, RequireMembership, OwnerId, Roles, BotTokenFile, RolesFile)
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