#include "ChatSystem.h"

#include "GameServer.h"
#include "Components/PlayerComponent.h"
#include "Components/MovementComponent.h"
#include "Components/AppearanceComponent.h"
#include "CharacterRecord.h"
#include "Components/CharacterComponent.h"
#include "Game/Level.h"

#include "PlayerManager.h"

ChatSystem::ChatSystem(gsl::not_null<World*> apWorld)
    : m_pWorld(apWorld)
{
    GServer->RegisterHandler<&ChatSystem::HandleChatMessageRequest>(this);
    GServer->RegisterHandler<&ChatSystem::HandleRespawnRequest>(this);
    GServer->RegisterHandler<&ChatSystem::HandleSaveCharacterRequest>(this);
}

void ChatSystem::HandleSaveCharacterRequest(const PacketEvent<client::SaveCharacterRequest>& aMessage)
{
    auto* pPlayerManager = m_pWorld->get<PlayerManager>();

    const auto entity = pPlayerManager->GetByConnectionId(aMessage.ConnectionId);
    if (!entity || !entity.has<PlayerComponent>())
        return;

    auto* pPlayer = entity.get_mut<PlayerComponent>();

    // Identity comes from Discord, never from the client. Without a verified account there
    // is nothing durable to key a character on, and storing one against a connection id
    // would lose it the moment they reconnect.
    if (pPlayer->DiscordId.empty())
    {
        Tell(*pPlayer, "Your Discord sign-in could not be verified, so a character cannot be saved.");
        return;
    }

    const auto& blob = aMessage.get_ccstate();

    // Same bound as the spawn path. This is stored and then relayed to every other client,
    // so an unbounded blob is a way to make one save allocate arbitrary memory everywhere.
    constexpr size_t kMaxCcstate = 256 * 1024;

    // Too SMALL is the case that actually happened. A real appearance is 7-9KB; a 23-byte
    // one was saved during testing and then used to spawn that player, because the
    // customization state exists briefly before it is populated. Rejecting only empty
    // blobs let the degenerate case straight through, and the server is the last place to
    // catch it before it becomes somebody's stored character.
    constexpr size_t kMinCcstate = 1024;

    if (blob.size() < kMinCcstate || blob.size() > kMaxCcstate)
    {
        spdlog::warn("Refused a character save from {} - {} bytes of appearance is not plausible",
                     pPlayer->Username, blob.size());
        Tell(*pPlayer, "That character could not be saved - the appearance data was not usable.");
        return;
    }

    auto& store = GServer->GetPlayerStore();

    CharacterRecord character;
    character.Slot = 0;
    character.IsMale = aMessage.get_is_male();
    character.Appearance = Base64::Encode(std::vector<uint8_t>(blob.begin(), blob.end()));

    // What they typed with /character save wins over the name the client offered, which is
    // only their launcher display name. Falls back through the client's name to their
    // Discord name, so a character always has something to be called.
    std::string name = pPlayer->PendingCharacterName;
    if (name.empty())
        name = aMessage.get_name().c_str();
    if (name.size() > 32)
        name.resize(32);

    // Consumed, so a later save without a name does not silently re-apply an old one.
    pPlayer->PendingCharacterName.clear();

    // An existing character keeps its name when the player is only editing their face.
    if (name.empty())
    {
        if (const auto* pExisting = store.FindCharacter(pPlayer->DiscordId))
            name = pExisting->Name;
    }

    // Carried over, so editing your face at a ripperdoc does not make the server think
    // you never picked a name and ask again every visit.
    if (const auto* pExisting = store.FindCharacter(pPlayer->DiscordId))
        character.NameChosen = pExisting->NameChosen;

    // Typing a name into /character save IS choosing one.
    if (!pPlayer->PendingCharacterName.empty() || !aMessage.get_name().empty())
        character.NameChosen = character.NameChosen || !name.empty();

    character.Name = name.empty() ? pPlayer->Username : name;

    // Progression carried forward, so editing your face does not reset your character and
    // re-grant the starting loadout.
    if (const auto* pExisting = store.FindCharacter(pPlayer->DiscordId))
    {
        character.Level = pExisting->Level;
        character.AttributePoints = pExisting->AttributePoints;
        character.PerkPoints = pExisting->PerkPoints;
        character.Initialised = pExisting->Initialised;
    }

    store.SaveCharacter(pPlayer->DiscordId, pPlayer->Username, character);

    spdlog::info("{} saved character '{}' from the creator ({} bytes)", pPlayer->Username,
                 character.Name, blob.size());

    Tell(*pPlayer, fmt::format("Character saved as '{}'. You will look like this every time you join.",
                               character.Name));

    // Nobody should have to know a command exists to be called something.
    //
    // The creator has no name field, so a character arrives with the account name on it -
    // which is the one thing roleplay cannot have, and it is now on the nameplate, in the
    // scanner and in front of every line they type. Asking here means the box appears the
    // moment they finish creating, which is exactly when they are thinking about who this
    // person is.
    //
    // Only when they have not chosen one. Somebody editing their face at a ripperdoc has
    // already answered this and being asked again every visit would be its own kind of
    // broken.
    if (!character.NameChosen)
        AskForCharacterName(*pPlayer, character);
}

/**
 * Opens the name box on a player's client.
 *
 * Split out because there are two moments worth asking: straight after the creator, and
 * on spawn for anybody whose character predates the prompt existing. Both want identical
 * behaviour, and the second one is the only route that ever reaches an existing player.
 */
void ChatSystem::AskForCharacterName(const PlayerComponent& acPlayer, const CharacterRecord& acCharacter)
{
    server::RequestCharacterName ask;

    // Their account name is NOT offered as a starting value. It is what we are trying to
    // get away from, and pre-filling it makes pressing Enter - the path of least
    // resistance - reproduce exactly the problem the prompt exists to solve.
    ask.set_current("");

    GServer->Send(acPlayer.Connection, ask);

    spdlog::info("Asked {} to name their character", acPlayer.Username);
}

void ChatSystem::HandleRespawnRequest(const PacketEvent<client::RespawnRequest>& aMessage)
{
    auto* pPlayerManager = m_pWorld->get<PlayerManager>();

    const auto entity = pPlayerManager->GetByConnectionId(aMessage.ConnectionId);
    if (!entity || !entity.has<PlayerComponent>())
        return;

    auto* pPlayer = entity.get_mut<PlayerComponent>();

    glm::vec3 position;
    float yaw = 0.f;

    if (!GServer->GetRespawnPoint(position, yaw))
    {
        // No respawn point set yet. Reviving where they fell is a worse outcome than the
        // Afterlife but a far better one than lying dead waiting for a load screen, so
        // the client has already healed them and this simply leaves them put.
        Tell(*pPlayer, "You are back up. (No respawn point set - an admin can fix that with /setspawn.)");
        spdlog::warn("{} respawned in place - no respawn point configured", pPlayer->Username);
        return;
    }

    // A jailed player respawns in the cell, not at the Afterlife. Dying is not a way out
    // of a sentence, and EnforceJail would drag them back within the second regardless -
    // this just avoids the visible teleport across the map and straight back.
    if (const auto* pRecord = GServer->GetPlayerStore().Find(pPlayer->DiscordId))
    {
        if (pRecord->JailedUntil > 0)
        {
            position = {pRecord->JailX, pRecord->JailY, pRecord->JailZ};
            yaw = 0.f;
        }
    }

    server::NotifyTeleport teleport;

    common::Vector3 destination;
    destination.set_x(position.x);
    destination.set_y(position.y);
    destination.set_z(position.z);
    teleport.set_position(destination);
    teleport.set_rotation(yaw);

    GServer->Send(pPlayer->Connection, teleport);

    spdlog::info("Respawned {} at ({:.1f}, {:.1f}, {:.1f})", pPlayer->Username, position.x, position.y, position.z);
}

void ChatSystem::Broadcast(String acUsername, String acMessage, uint32_t aChannel)
{
    server::ChatMessage message;
    message.set_username(std::move(acUsername));
    message.set_message(std::move(acMessage));
    message.set_channel(aChannel);

    m_pWorld->each([&message](flecs::entity, const PlayerComponent& aPlayer)
    {
        GServer->Send(aPlayer.Connection, message);
    });
}

void ChatSystem::Tell(const PlayerComponent& acPlayer, const std::string& acMessage)
{
    server::ChatMessage message;
    message.set_username("SERVER");
    message.set_message(acMessage.c_str());
    message.set_channel(ChatChannel::kServer);

    GServer->Send(acPlayer.Connection, message);
}

void ChatSystem::BroadcastInRange(const std::string& acUsername, const std::string& acMessage,
                                  const glm::vec3& acOrigin, float aRange, flecs::entity aSender,
                                  uint32_t aChannel)
{
    server::ChatMessage message;
    message.set_username(acUsername.c_str());
    message.set_message(acMessage.c_str());
    message.set_channel(aChannel);

    m_pWorld->each(
        [&](flecs::entity aEntity, const PlayerComponent& aPlayer)
        {
            if (aEntity == aSender)
            {
                GServer->Send(aPlayer.Connection, message);
                return;
            }

            // No puppet means they are connected but not yet standing anywhere, so there
            // is no distance to measure. They hear nothing local until they spawn, which
            // is the same as not being in the room.
            const auto* pMovement = aPlayer.Puppet ? aPlayer.Puppet.get<MovementComponent>() : nullptr;
            if (!pMovement)
                return;

            if (glm::distance(pMovement->Position, acOrigin) <= aRange)
                GServer->Send(aPlayer.Connection, message);
        });
}

bool ChatSystem::ResolveChannel(const PlayerComponent& acSender, const std::string& acLine,
                                std::string& aText, float& aRange, bool& aEveryone, uint32_t& aChannel)
{
    // Default: ordinary talking, heard by anyone nearby. Typing nothing special is the
    // common case and must stay the shortest path.
    aText = acLine;
    aRange = ChatRange::kLocal;
    aEveryone = false;
    aChannel = ChatChannel::kLocal;

    const auto prefix = [&](const char* acWord) { return acLine.rfind(acWord, 0) == 0; };

    // Each of these is checked WITH a trailing space, so "/yellow hair" is not read as a
    // yell. The bare form is handled separately so it can explain itself.
    struct Channel { const char* Word; float Range; bool Everyone; EPermissionLevel Needs; const char* Marker; uint32_t Id; };

    static constexpr Channel kChannels[] = {
        {"/whisper", ChatRange::kWhisper, false, EPermissionLevel::kPlayer, "[whispers] ", ChatChannel::kWhisper},
        {"/yell",    ChatRange::kYell,    false, EPermissionLevel::kPlayer, "[yells] ",    ChatChannel::kYell},
        {"/advert",  0.f,                 true,  EPermissionLevel::kAdmin,  "[ADVERT] ",   ChatChannel::kAdvert},
    };

    for (const auto& channel : kChannels)
    {
        const std::string withSpace = std::string(channel.Word) + " ";

        if (!prefix(channel.Word))
            continue;

        if (!acSender.HasAtLeast(channel.Needs))
        {
            Tell(acSender, fmt::format("You need to be {} to use {}.",
                                       ToString(channel.Needs), channel.Word));
            return false;
        }

        if (!prefix(withSpace.c_str()) || acLine.size() <= withSpace.size())
        {
            Tell(acSender, fmt::format("Usage: {} <message>", channel.Word));
            return false;
        }

        // The marker travels inside the message rather than the username, so the chat UI
        // still groups consecutive lines by author instead of treating every yell as a
        // new speaker.
        aText = std::string(channel.Marker) + acLine.substr(withSpace.size());
        aRange = channel.Range;
        aEveryone = channel.Everyone;
        aChannel = channel.Id;
        return true;
    }

    return true;
}

namespace
{
// Splits "/ban SomeName being a nuisance" into { "/ban", "SomeName", "being a nuisance" }.
// The reason keeps its spaces - only the command and target are tokenised.
void SplitCommand(const std::string& acLine, std::string& aCommand, std::string& aTarget, std::string& aRest)
{
    const auto first = acLine.find(' ');
    if (first == std::string::npos)
    {
        aCommand = acLine;
        return;
    }

    aCommand = acLine.substr(0, first);

    const auto second = acLine.find(' ', first + 1);
    if (second == std::string::npos)
    {
        aTarget = acLine.substr(first + 1);
        return;
    }

    aTarget = acLine.substr(first + 1, second - first - 1);
    aRest = acLine.substr(second + 1);
}
}

bool ChatSystem::HandleModerationCommand(flecs::entity aSender, const PlayerComponent& acSender,
                                         const std::string& acLine)
{
    if (acLine.empty() || acLine[0] != '/')
        return false;

    std::string command, target, rest;
    SplitCommand(acLine, command, target, rest);

    // Refusals say what rank is needed. "You can't do that" invites people to keep
    // trying; "requires moderator" tells them to go ask for the role.
    const auto deny = [&](EPermissionLevel aRequired)
    {
        Tell(acSender, fmt::format("You need to be {} to use {}.", ToString(aRequired), command));
        spdlog::info("Denied {} ({}) command {}", acSender.Username, ToString(acSender.Level), command);
        return true;
    };

    // Find a connected player by name. Case-insensitive, because nobody types display
    // names exactly, and a moderator fumbling capitalisation during an incident is bad.
    // Four ways to name the same person, because staff know them by different things.
    //
    // Matching only the Discord username meant an admin watching somebody roleplay as
    // "Silas Voss" had to go and look up that this was noremacxxi before they could do
    // anything about them - and a character id, the one identifier that is stable and
    // safe to paste anywhere, could not be used at all.
    //
    // Order matters. Character id first because it is exact and unambiguous by
    // construction; then Discord id, equally exact; then the two human names. A character
    // name is checked before an account name so that in-world identity wins - if somebody
    // names their character after another player's account, the character they are
    // standing in front of is the one an admin means.
    const auto equalsInsensitive = [](const std::string& acLeft, const std::string& acRight)
    {
        return acLeft.size() == acRight.size() &&
               std::equal(acLeft.begin(), acLeft.end(), acRight.begin(),
                          [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    };

    const auto findPlayer = [&](const std::string& acQuery) -> flecs::entity
    {
        flecs::entity byCharacterId{};
        flecs::entity byDiscordId{};
        flecs::entity byCharacterName{};
        flecs::entity byUsername{};

        m_pWorld->each(
            [&](flecs::entity aEntity, const PlayerComponent& aOther)
            {
                if (equalsInsensitive(aOther.Username, acQuery))
                {
                    if (!byUsername) byUsername = aEntity;
                }

                if (aOther.DiscordId == acQuery)
                {
                    if (!byDiscordId) byDiscordId = aEntity;
                }

                const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(aOther.DiscordId);
                if (!pCharacter)
                    return;

                if (!pCharacter->CharacterId.empty() &&
                    equalsInsensitive(pCharacter->CharacterId, acQuery))
                {
                    if (!byCharacterId) byCharacterId = aEntity;
                }

                if (!pCharacter->Name.empty() && equalsInsensitive(pCharacter->Name, acQuery))
                {
                    if (!byCharacterName) byCharacterName = aEntity;
                }
            });

        if (byCharacterId)   return byCharacterId;
        if (byDiscordId)     return byDiscordId;
        if (byCharacterName) return byCharacterName;

        return byUsername;
    };

    // ---------------------------------------------------------------- /kick ---
    if (command == "/kick")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
            return deny(EPermissionLevel::kModerator);

        if (target.empty())
        {
            Tell(acSender, "Usage: /kick <player> [reason]");
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        const auto* pVictim = victim.get<PlayerComponent>();

        // Rank protects rank. Without this, one moderator can kick another in a loop,
        // and anyone who talks a mod into a bad decision can decapitate the staff.
        if (pVictim->Level >= acSender.Level)
        {
            Tell(acSender, "You cannot kick someone at or above your own rank.");
            return true;
        }

        spdlog::info("{} kicked {} ({})", acSender.Username, pVictim->Username, rest);
        Broadcast("SERVER", fmt::format("{} was kicked by {}{}", pVictim->Username, acSender.Username,
                                        rest.empty() ? "" : (" - " + rest)).c_str());
        GServer->Kick(pVictim->Connection);
        return true;
    }

    // ----------------------------------------------------------------- /ban ---
    if (command == "/ban")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        if (target.empty())
        {
            Tell(acSender, "Usage: /ban <player> [reason]");
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        const auto* pVictim = victim.get<PlayerComponent>();

        if (pVictim->Level >= acSender.Level)
        {
            Tell(acSender, "You cannot ban someone at or above your own rank.");
            return true;
        }

        if (pVictim->DiscordId.empty())
        {
            // Nothing durable to ban. Kicking is the honest outcome rather than
            // pretending a ban was recorded.
            Tell(acSender, "That player has no verified Discord account - kicking instead.");
            GServer->Kick(pVictim->Connection);
            return true;
        }

        GServer->GetBanList().Add(pVictim->DiscordId, pVictim->Username, rest, acSender.Username);

        spdlog::info("{} BANNED {} ({}): {}", acSender.Username, pVictim->Username, pVictim->DiscordId, rest);
        Broadcast("SERVER", fmt::format("{} was banned by {}{}", pVictim->Username, acSender.Username,
                                        rest.empty() ? "" : (" - " + rest)).c_str());
        GServer->Kick(pVictim->Connection);
        return true;
    }

    // --------------------------------------------------------------- /unban ---
    if (command == "/unban")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        if (target.empty())
        {
            Tell(acSender, "Usage: /unban <discord id>  (see /bans)");
            return true;
        }

        const bool removed = GServer->GetBanList().Remove(target);
        Tell(acSender, removed ? fmt::format("Unbanned {}.", target)
                               : fmt::format("{} is not banned.", target));

        if (removed)
            spdlog::info("{} unbanned {}", acSender.Username, target);

        return true;
    }

    // ---------------------------------------------------------------- /bans ---
    if (command == "/bans")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
            return deny(EPermissionLevel::kModerator);

        const auto& entries = GServer->GetBanList().Entries();

        if (entries.empty())
        {
            Tell(acSender, "Nobody is banned.");
            return true;
        }

        for (const auto& entry : entries)
        {
            Tell(acSender, fmt::format("{} ({}) - banned by {}{}", entry.Username, entry.DiscordId,
                                       entry.BannedBy,
                                       entry.Reason.empty() ? "" : (" - " + entry.Reason)));
        }

        return true;
    }

    // ----------------------------------------------------------------- /tp ----
    //
    // Two directions, because both are wanted for different reasons.
    //
    //   /tp <player>     brings them to you - you chose where to stand
    //   /tp to <player>  sends you to them - you go and see what they are looking at
    //
    // Written as a word rather than a separate command so there is one thing to remember
    // and one place to look in /help.
    if (command == "/tp")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        const bool goToThem = (target == "to");

        // Re-split when "to" ate the first word: the player's name is what follows it.
        if (goToThem)
        {
            const auto nameStart = rest.find_first_not_of(' ');
            target = (nameStart == std::string::npos) ? std::string{} : rest.substr(nameStart);

            // A trailing word after the name would be a typo rather than a reason.
            if (const auto space = target.find(' '); space != std::string::npos)
                target = target.substr(0, space);
        }

        if (target.empty())
        {
            Tell(acSender, "Usage: /tp <player>   (brings them to you)");
            Tell(acSender, "       /tp to <player>   (sends you to them)");
            Tell(acSender, "  <player> can be a character name, a Discord name, a Discord id,");
            Tell(acSender, "  or a character id from /whois.");
            return true;
        }

        if (goToThem)
        {
            const auto them = findPlayer(target);
            if (!them)
            {
                Tell(acSender, fmt::format("No player called '{}' is online.", target));
                return true;
            }

            const auto* pThem = them.get<PlayerComponent>();
            const auto* pTheirs = pThem->Puppet ? pThem->Puppet.get<MovementComponent>() : nullptr;

            if (!pTheirs)
            {
                Tell(acSender, fmt::format("{} has not spawned in yet.", pThem->Username));
                return true;
            }

            // Remember where WE were, so /return brings us back. Same mechanism, applied
            // to the person running the command instead of the person being moved.
            if (const auto* pMine = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr)
            {
                auto* pSelf = aSender.get_mut<PlayerComponent>();
                pSelf->ReturnPosition = pMine->Position;
                pSelf->ReturnRotation = pMine->Rotation;
                pSelf->HasReturnPoint = true;
            }

            // Beside them rather than on top of them. Two puppets sharing a spot look
            // broken and shove each other apart.
            const float theirYaw = pTheirs->Rotation.z;
            const glm::vec3 theirForward{-std::sin(theirYaw), std::cos(theirYaw), 0.f};
            const glm::vec3 destination = pTheirs->Position + theirForward * kTeleportDistance;

            server::NotifyTeleport teleport;
            common::Vector3 position;
            position.set_x(destination.x);
            position.set_y(destination.y);
            position.set_z(destination.z);
            teleport.set_position(position);

            // Facing them, since you came to see what they are doing.
            teleport.set_rotation(theirYaw + 3.14159265f);

            GServer->Send(acSender.Connection, teleport);

            spdlog::info("{} teleported to {}", acSender.Username, pThem->Username);

            Tell(acSender, fmt::format("Went to {}. /return brings you back.", pThem->Username));
            Tell(*pThem, fmt::format("{} teleported to you.", acSender.Username));
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        const auto* pVictim = victim.get<PlayerComponent>();

        const auto* pMine = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr;
        if (!pMine)
        {
            Tell(acSender, "You need to be in the world to teleport anyone to you.");
            return true;
        }

        if (!pVictim->Puppet)
        {
            Tell(acSender, fmt::format("{} has not spawned in yet.", pVictim->Username));
            return true;
        }

        // Remember where they were BEFORE moving them, so /return can undo this.
        //
        // Recorded from the server's own copy of their position rather than asking the
        // client, and only overwritten when there is a real position to record - so
        // teleporting someone twice in a row still returns them to where they actually
        // started, not to the last place staff dragged them.
        if (const auto* pTheirs = pVictim->Puppet.get<MovementComponent>())
        {
            auto* pVictimMutable = victim.get_mut<PlayerComponent>();
            pVictimMutable->ReturnPosition = pTheirs->Position;
            pVictimMutable->ReturnRotation = pTheirs->Rotation;
            pVictimMutable->HasReturnPoint = true;
        }

        // Yaw is rotation about Z, and Cyberpunk's world is Y-forward at yaw 0, so the
        // facing vector is (-sin, cos). Getting this backwards would still place them the
        // right distance away, just behind - worth an eyeball on the first use.
        const float yaw = pMine->Rotation.z;
        const glm::vec3 forward{-std::sin(yaw), std::cos(yaw), 0.f};
        const glm::vec3 destination = pMine->Position + forward * kTeleportDistance;

        server::NotifyTeleport teleport;

        common::Vector3 position;
        position.set_x(destination.x);
        position.set_y(destination.y);
        position.set_z(destination.z);
        teleport.set_position(position);

        // Turned to face the person who summoned them, which is the whole point of
        // being brought somewhere.
        teleport.set_rotation(yaw + 3.14159265f);

        GServer->Send(pVictim->Connection, teleport);

        spdlog::info("{} teleported {} to ({:.1f}, {:.1f}, {:.1f})", acSender.Username, pVictim->Username,
                     destination.x, destination.y, destination.z);

        Tell(acSender, fmt::format("Brought {} to you.", pVictim->Username));
        Tell(*pVictim, fmt::format("You were teleported to {}.", acSender.Username));
        return true;
    }

    // --------------------------------------------------------------- /kill ----
    //
    // Sends someone to the respawn point immediately. Named /kill because that is what it
    // is for in roleplay terms, but nothing actually dies - players cannot, by design, and
    // a command that produced a FLATLINED screen would hand back the exact session-killer
    // the rest of this work exists to remove.
    if (command == "/kill")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
            return deny(EPermissionLevel::kModerator);

        if (target.empty())
        {
            Tell(acSender, "Usage: /kill <player> [reason]");
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        auto* pVictim = victim.get_mut<PlayerComponent>();

        if (victim != aSender && pVictim->Level >= acSender.Level)
        {
            Tell(acSender, "You cannot do that to someone at or above your own rank.");
            return true;
        }

        glm::vec3 position;
        float yaw = 0.f;

        if (!GServer->GetRespawnPoint(position, yaw))
        {
            Tell(acSender, "No respawn point set - stand where you want it and run /setspawn first.");
            return true;
        }

        // Jail wins, same as an ordinary death. Being killed should not be a way out.
        if (const auto* pRecord = GServer->GetPlayerStore().Find(pVictim->DiscordId))
        {
            if (pRecord->JailedUntil > 0)
                position = {pRecord->JailX, pRecord->JailY, pRecord->JailZ};
        }

        server::NotifyTeleport teleport;
        common::Vector3 destination;
        destination.set_x(position.x);
        destination.set_y(position.y);
        destination.set_z(position.z);
        teleport.set_position(destination);
        teleport.set_rotation(yaw);

        GServer->Send(pVictim->Connection, teleport);

        spdlog::info("{} killed {} ({})", acSender.Username, pVictim->Username, rest);

        Broadcast("SERVER", fmt::format("{} was killed by {}{}", pVictim->Username, acSender.Username,
                                        rest.empty() ? "" : (" - " + rest)).c_str());
        return true;
    }

    // ----------------------------------------------------------- /setspawn ----
    //
    // Where players reappear after dying. Recorded from where the admin is standing,
    // for the same reason /jail works that way: nobody should have to look up
    // coordinates, and "stand in the Afterlife and run this" is a instruction anyone can
    // follow. Persisted, so it survives a restart.
    if (command == "/setspawn")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        const auto* pMovement = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr;
        if (!pMovement)
        {
            Tell(acSender, "Spawn into the world first, then stand where you want people to respawn.");
            return true;
        }

        GServer->SetRespawnPoint(pMovement->Position, pMovement->Rotation.z);

        spdlog::info("{} set the respawn point to ({:.1f}, {:.1f}, {:.1f})", acSender.Username,
                     pMovement->Position.x, pMovement->Position.y, pMovement->Position.z);

        Tell(acSender, fmt::format("Respawn point set here ({:.0f}, {:.0f}, {:.0f}). Players will reappear here when they die.",
                                   pMovement->Position.x, pMovement->Position.y, pMovement->Position.z));
        return true;
    }

    // ------------------------------------------------------------- /name ----
    //
    // What your character is called, which is not your Discord name. Somebody being
    // "noremacxxi" and their character being someone else is the point of roleplay.
    //
    // Separate from appearance because the two are chosen at different moments: a face is
    // fiddled with at a mirror and saves itself, a name is a decision typed once.
    if (command == "/name")
    {
        const auto nameStart = acLine.find(' ');
        std::string wanted = (nameStart == std::string::npos) ? std::string{} : acLine.substr(nameStart + 1);

        while (!wanted.empty() && wanted.front() == ' ')
            wanted.erase(wanted.begin());

        if (wanted.empty())
        {
            const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(acSender.DiscordId);
            Tell(acSender, fmt::format("You are called '{}'. Change it with /name <name>.",
                                       (pCharacter && !pCharacter->Name.empty()) ? pCharacter->Name
                                                                                 : acSender.Username));
            return true;
        }

        if (wanted.size() > 32)
            wanted.resize(32);

        auto& store = GServer->GetPlayerStore();
        const auto* pCharacter = store.FindCharacter(acSender.DiscordId);

        if (!pCharacter)
        {
            Tell(acSender, "You have no character yet - look in a mirror first, then set a name.");
            return true;
        }

        auto updated = *pCharacter;
        updated.Name = wanted;

        // Typing /name is the definition of choosing one, so the prompt stops asking.
        updated.NameChosen = true;

        store.SaveCharacter(acSender.DiscordId, acSender.Username, updated);

        spdlog::info("{} named their character '{}'", acSender.Username, wanted);

        Tell(acSender, fmt::format("You are now known as '{}'.", wanted));
        return true;
    }

    // ------------------------------------------------------------- /whois ----
    //
    // Who is this, by every name they have?
    //
    // Staff see three different identities for one person - the character in front of them,
    // the Discord account that owns it, and the id that outlives both - and until now there
    // was no way to get from any one to the others. That made the character id useless in
    // practice: nothing could tell you what it was.
    if (command == "/whois")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
            return deny(EPermissionLevel::kModerator);

        const auto nameStart = acLine.find(' ');
        std::string query = (nameStart == std::string::npos) ? std::string{} : acLine.substr(nameStart + 1);

        while (!query.empty() && query.front() == ' ')
            query.erase(query.begin());

        if (query.empty())
        {
            Tell(acSender, "Usage: /whois <player>   (character name, Discord name, or id)");
            return true;
        }

        const auto who = findPlayer(query);
        if (!who)
        {
            Tell(acSender, fmt::format("Nobody matching '{}' is online.", query));
            return true;
        }

        const auto* pWho = who.get<PlayerComponent>();
        const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(pWho->DiscordId);

        Tell(acSender, fmt::format("Character : {}",
                                   (pCharacter && !pCharacter->Name.empty()) ? pCharacter->Name
                                                                             : "not named yet"));
        Tell(acSender, fmt::format("Account   : {}", pWho->Username));

        if (pCharacter && !pCharacter->CharacterId.empty())
            Tell(acSender, fmt::format("Character id : {}", pCharacter->CharacterId));
        else
            Tell(acSender, "Character id : none yet - it is assigned when a character is saved.");

        // The Discord id is deliberately NOT shown. It identifies the human being rather
        // than the character, it is the key everything on the server is filed under, and
        // nothing an admin does in chat needs it - /tp and the rest already accept the
        // character id, which is safe to paste anywhere.
        return true;
    }

    // ---------------------------------------------------------- /profile ----
    //
    // Inspect full Character Profile:
    // Discord Account -> Player ID -> Character ID -> Character Profile -> Entity
    if (command == "/profile" || command == "/charprofile")
    {
        const auto nameStart = acLine.find(' ');
        std::string query = (nameStart == std::string::npos) ? std::string{} : acLine.substr(nameStart + 1);

        while (!query.empty() && query.front() == ' ')
            query.erase(query.begin());

        const CharacterRecord* pCharacter = nullptr;
        std::string accountName;
        std::string discordId;

        if (query.empty())
        {
            discordId = acSender.DiscordId;
            accountName = acSender.Username;
            pCharacter = GServer->GetPlayerStore().FindCharacter(discordId);
        }
        else
        {
            const auto who = findPlayer(query);
            if (who)
            {
                const auto* pWho = who.get<PlayerComponent>();
                discordId = pWho->DiscordId;
                accountName = pWho->Username;
                pCharacter = GServer->GetPlayerStore().FindCharacter(discordId);
            }
            else
            {
                Tell(acSender, fmt::format("Nobody matching '{}' is online.", query));
                return true;
            }
        }

        if (!pCharacter)
        {
            Tell(acSender, "Character profile not found.");
            return true;
        }

        Tell(acSender, "==== CHARACTER PROFILE ====");
        Tell(acSender, fmt::format("Name         : {}", pCharacter->Name.empty() ? accountName : pCharacter->Name));
        Tell(acSender, fmt::format("Character ID : {}", pCharacter->CharacterId));
        Tell(acSender, fmt::format("Account      : {} (Discord: {})", accountName, discordId));
        Tell(acSender, fmt::format("Lifepath     : {}", pCharacter->Lifepath));
        Tell(acSender, fmt::format("Occupation   : {}", pCharacter->Occupation));
        Tell(acSender, fmt::format("Affiliation  : {}{}", pCharacter->Affiliation, pCharacter->IsAffiliationLeader ? " (Leader)" : ""));
        Tell(acSender, fmt::format("Level        : {} (Body:{} Ref:{} Tech:{} Intel:{} Cool:{})",
                                   pCharacter->Level,
                                   pCharacter->Attributes.Body,
                                   pCharacter->Attributes.Reflexes,
                                   pCharacter->Attributes.TechnicalAbility,
                                   pCharacter->Attributes.Intelligence,
                                   pCharacter->Attributes.Cool));
        Tell(acSender, fmt::format("Health       : {:.0f} / {:.0f}", pCharacter->Health, pCharacter->MaxHealth));
        Tell(acSender, fmt::format("Wallet       : ${} Eddies", pCharacter->Money));
        Tell(acSender, fmt::format("Wanted Level : Level {} (Bounty: ${:.0f})",
                                   pCharacter->WantedStatus.Level, pCharacter->WantedStatus.Bounty));
        Tell(acSender, fmt::format("Bio          : {}", pCharacter->Bio.empty() ? "(No bio written yet)" : pCharacter->Bio));
        Tell(acSender, fmt::format("Position     : ({:.1f}, {:.1f}, {:.1f})", pCharacter->PositionX, pCharacter->PositionY, pCharacter->PositionZ));
        Tell(acSender, "===========================");
        return true;
    }

    // ----------------------------------------------------------- /setbio ----
    //
    // Set character backstory/bio.
    // RULE: Non-admin players can set their bio ONCE. Subsequent updates require an Admin.
    if (command == "/setbio")
    {
        const auto textStart = acLine.find(' ');
        std::string newBio = (textStart == std::string::npos) ? std::string{} : acLine.substr(textStart + 1);

        while (!newBio.empty() && newBio.front() == ' ')
            newBio.erase(newBio.begin());

        if (newBio.empty())
        {
            Tell(acSender, "Usage: /setbio <character backstory text>");
            return true;
        }

        auto& store = GServer->GetPlayerStore();
        const auto* pCharacter = store.FindCharacter(acSender.DiscordId);

        if (!pCharacter)
        {
            Tell(acSender, "You have no character yet.");
            return true;
        }

        const bool isAdmin = acSender.HasAtLeast(EPermissionLevel::kAdmin);

        if (pCharacter->BioSet && !isAdmin)
        {
            Tell(acSender, "Your character bio has already been set and locked. Contact an admin to update it.");
            return true;
        }

        auto updated = *pCharacter;
        updated.Bio = newBio;
        updated.BioSet = true;

        store.SaveCharacter(acSender.DiscordId, acSender.Username, updated);

        Tell(acSender, "Character bio updated successfully.");
        spdlog::info("{} updated character bio for '{}'", acSender.Username, updated.Name);
        return true;
    }

    // ---------------------------------------------------- /setoccupation ----
    if (command == "/setoccupation" || command == "/setocc")
    {
        const auto textStart = acLine.find(' ');
        std::string newOcc = (textStart == std::string::npos) ? std::string{} : acLine.substr(textStart + 1);

        while (!newOcc.empty() && newOcc.front() == ' ')
            newOcc.erase(newOcc.begin());

        if (newOcc.empty())
        {
            Tell(acSender, "Usage: /setoccupation <Solo|Netrunner|Techie|Nomad|Fixer|Medtech|Rockerboy|Corp|...>");
            return true;
        }

        auto& store = GServer->GetPlayerStore();
        const auto* pCharacter = store.FindCharacter(acSender.DiscordId);

        if (!pCharacter)
        {
            Tell(acSender, "You have no character yet.");
            return true;
        }

        auto updated = *pCharacter;
        updated.Occupation = newOcc;

        store.SaveCharacter(acSender.DiscordId, acSender.Username, updated);

        Tell(acSender, fmt::format("Occupation set to '{}'.", newOcc));
        return true;
    }

    // --------------------------------------------------- /setaffiliation ----
    //
    // Set faction/gang/corp affiliation.
    // RULE: Players can only change affiliation ONCE PER WEEK (604,800 seconds).
    // Future expansion: Require leader permission for protected affiliations.
    if (command == "/setaffiliation" || command == "/setaffil")
    {
        const auto textStart = acLine.find(' ');
        std::string newAffil = (textStart == std::string::npos) ? std::string{} : acLine.substr(textStart + 1);

        while (!newAffil.empty() && newAffil.front() == ' ')
            newAffil.erase(newAffil.begin());

        if (newAffil.empty())
        {
            Tell(acSender, "Usage: /setaffiliation <Unaffiliated|Maelstrom|Mox|Valentinos|Arasaka|Militech|NCPD|...>");
            return true;
        }

        auto& store = GServer->GetPlayerStore();
        const auto* pCharacter = store.FindCharacter(acSender.DiscordId);

        if (!pCharacter)
        {
            Tell(acSender, "You have no character yet.");
            return true;
        }

        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        constexpr int64_t kOneWeekSeconds = 7 * 24 * 3600;
        const bool isAdmin = acSender.HasAtLeast(EPermissionLevel::kAdmin);

        if (!isAdmin && pCharacter->LastAffiliationChange > 0)
        {
            const int64_t elapsed = now - pCharacter->LastAffiliationChange;
            if (elapsed < kOneWeekSeconds)
            {
                const int64_t remaining = kOneWeekSeconds - elapsed;
                const int days = static_cast<int>(remaining / 86400);
                const int hours = static_cast<int>((remaining % 86400) / 3600);
                Tell(acSender, fmt::format("Affiliation can only be changed once per week. Next change available in {}d {}h.", days, hours));
                Tell(acSender, "Note: Future updates will require permission from an Affiliation Leader to join official factions.");
                return true;
            }
        }

        auto updated = *pCharacter;
        updated.Affiliation = newAffil;
        updated.LastAffiliationChange = now;

        store.SaveCharacter(acSender.DiscordId, acSender.Username, updated);

        Tell(acSender, fmt::format("Affiliation set to '{}'.", newAffil));
        spdlog::info("{} changed affiliation to '{}'", acSender.Username, newAffil);
        return true;
    }

    // ---------------------------------------------------------- /setstart ----
    //
    // Where a brand-new character arrives. Separate from /setspawn on purpose - see
    // GameServer::SetStartPoint.
    if (command == "/setstart")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        const auto* pMovement = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr;
        if (!pMovement)
        {
            Tell(acSender, "Spawn into the world first, then stand where new players should arrive.");
            return true;
        }

        GServer->SetStartPoint(pMovement->Position, pMovement->Rotation.z);

        spdlog::info("{} set the start point to ({:.1f}, {:.1f}, {:.1f})", acSender.Username,
                     pMovement->Position.x, pMovement->Position.y, pMovement->Position.z);

        Tell(acSender, fmt::format("Start point set here ({:.0f}, {:.0f}, {:.0f}). New characters will arrive here.",
                                   pMovement->Position.x, pMovement->Position.y, pMovement->Position.z));
        return true;
    }

    // --------------------------------------------------------------- /jail ----
    //
    // The cell is wherever the staff member is standing. No configuration, no coordinates
    // to look up - walk into the room you want to use as a cell, bring them, and jail
    // them. Any building in Night City becomes a holding cell.
    if (command == "/jail")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
            return deny(EPermissionLevel::kModerator);

        if (target.empty())
        {
            Tell(acSender, "Usage: /jail <player> <minutes> [reason]");
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        auto* pVictim = victim.get_mut<PlayerComponent>();

        // Jailing YOURSELF is always allowed.
        //
        // Rank protects rank, but it protected the owner from themselves - the highest
        // rank on the server could not jail anybody at all, including their own
        // character, which made this impossible to try without a second person. Locking
        // yourself up harms nobody, and being able to test a punishment before using it
        // on a player is worth more than the consistency.
        if (victim != aSender && pVictim->Level >= acSender.Level)
        {
            Tell(acSender, "You cannot jail someone at or above your own rank.");
            return true;
        }

        if (pVictim->DiscordId.empty())
        {
            // Nothing durable to hold them by. A sentence that cannot survive a reconnect
            // is not one, so say so rather than pretending.
            Tell(acSender, "That player has no verified Discord account, so a sentence could not be kept.");
            return true;
        }

        // "/jail bob 15 breaking character" - minutes first, the rest is the reason.
        int minutes = 0;
        std::string reason;
        {
            const auto space = rest.find(' ');
            const auto minutesText = space == std::string::npos ? rest : rest.substr(0, space);

            try { minutes = std::stoi(minutesText); } catch (...) { minutes = 0; }

            if (space != std::string::npos)
                reason = rest.substr(space + 1);
        }

        if (minutes <= 0)
        {
            Tell(acSender, "Usage: /jail <player> <minutes> [reason]");
            return true;
        }

        // Capped. An accidental extra digit should not sentence somebody to a week.
        minutes = std::min(minutes, 1440);

        const auto* pCell = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr;
        if (!pCell)
        {
            Tell(acSender, "Stand where you want the cell to be, then jail them.");
            return true;
        }

        // Where they were arrested, so /unjail and the end of the sentence can put them
        // back. Same mechanism /tp uses.
        if (const auto* pTheirs = pVictim->Puppet ? pVictim->Puppet.get<MovementComponent>() : nullptr)
        {
            pVictim->ReturnPosition = pTheirs->Position;
            pVictim->ReturnRotation = pTheirs->Rotation;
            pVictim->HasReturnPoint = true;
        }

        const auto until = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() + (minutes * 60);

        GServer->GetPlayerStore().SetJail(pVictim->DiscordId, pVictim->Username, until,
                                          pCell->Position, acSender.Username, reason);

        // Straight into the cell. EnforceJail would drag them there within the second
        // anyway; doing it now means the sentence starts where it should.
        server::NotifyTeleport teleport;
        common::Vector3 position;
        position.set_x(pCell->Position.x);
        position.set_y(pCell->Position.y);
        position.set_z(pCell->Position.z);
        teleport.set_position(position);
        teleport.set_rotation(pCell->Rotation.z);
        GServer->Send(pVictim->Connection, teleport);

        spdlog::info("{} jailed {} for {} minutes ({})", acSender.Username, pVictim->Username, minutes, reason);

        Broadcast("SERVER", fmt::format("{} was jailed for {} minute(s) by {}{}", pVictim->Username, minutes,
                                        acSender.Username, reason.empty() ? "" : (" - " + reason)).c_str());

        Tell(*pVictim, fmt::format("You are jailed for {} minute(s). Leaving the cell will put you back in it.",
                                   minutes));
        return true;
    }

    // ------------------------------------------------------------- /unjail ----
    if (command == "/unjail")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
            return deny(EPermissionLevel::kModerator);

        if (target.empty())
        {
            Tell(acSender, "Usage: /unjail <player>");
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        auto* pVictim = victim.get_mut<PlayerComponent>();
        const auto* pRecord = GServer->GetPlayerStore().Find(pVictim->DiscordId);

        if (!pRecord || pRecord->JailedUntil == 0)
        {
            Tell(acSender, fmt::format("{} is not jailed.", pVictim->Username));
            return true;
        }

        GServer->GetPlayerStore().ClearJail(pVictim->DiscordId);

        // Released back where they were arrested, if that is still known.
        if (pVictim->HasReturnPoint)
        {
            server::NotifyTeleport teleport;
            common::Vector3 position;
            position.set_x(pVictim->ReturnPosition.x);
            position.set_y(pVictim->ReturnPosition.y);
            position.set_z(pVictim->ReturnPosition.z);
            teleport.set_position(position);
            teleport.set_rotation(pVictim->ReturnRotation.z);
            GServer->Send(pVictim->Connection, teleport);

            pVictim->HasReturnPoint = false;
        }

        spdlog::info("{} released {} from jail", acSender.Username, pVictim->Username);

        Broadcast("SERVER", fmt::format("{} was released by {}", pVictim->Username, acSender.Username).c_str());
        Tell(*pVictim, "You have been released.");
        return true;
    }

    // ------------------------------------------------------------- /return ----
    //
    // Puts someone back where /tp took them from. The counterpart to a summon: staff
    // pulling a player out of whatever they were doing should be able to undo it.
    if (command == "/return")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        if (target.empty())
        {
            Tell(acSender, "Usage: /return <player>");
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        auto* pVictim = victim.get_mut<PlayerComponent>();

        if (!pVictim->HasReturnPoint)
        {
            Tell(acSender, fmt::format("{} has not been teleported, so there is nowhere to send them back to.",
                                       pVictim->Username));
            return true;
        }

        // A few metres short of where they were, facing the way they were facing.
        //
        // Landing on the exact spot is asking for trouble: whatever they were standing in
        // or next to may have moved, and materialising inside it drops people through the
        // world or wedges them in geometry. Arriving a short walk behind their own
        // footprints reads as being put back without pretending the world stood still.
        const float yaw = pVictim->ReturnRotation.z;
        const glm::vec3 forward{-std::sin(yaw), std::cos(yaw), 0.f};
        const glm::vec3 destination = pVictim->ReturnPosition - forward * kReturnBackoff;

        server::NotifyTeleport teleport;

        common::Vector3 position;
        position.set_x(destination.x);
        position.set_y(destination.y);
        position.set_z(destination.z);
        teleport.set_position(position);
        teleport.set_rotation(yaw);

        GServer->Send(pVictim->Connection, teleport);

        // Cleared so a second /return does not silently send them to a stale spot they
        // may have long since walked away from.
        pVictim->HasReturnPoint = false;

        spdlog::info("{} returned {} to ({:.1f}, {:.1f}, {:.1f})", acSender.Username, pVictim->Username,
                     destination.x, destination.y, destination.z);

        Tell(acSender, fmt::format("Sent {} back.", pVictim->Username));
        Tell(*pVictim, "You were sent back to where you were.");
        return true;
    }

    // ---------------------------------------------------------- /character ----
    //
    // The multiplayer character system, before it has a UI.
    //
    // Everything here is deliberately usable by one person with no second player and no
    // launcher work: the point is to find out whether the storage half is right before
    // building a creator on top of it. The creator replaces /character save, not the rest.
    if (command == "/character" || command == "/char")
    {
        auto& store = GServer->GetPlayerStore();

        if (target.empty() || target == "show")
        {
            const auto* pCharacter = store.FindCharacter(acSender.DiscordId);

            if (!pCharacter)
            {
                Tell(acSender, "You have no multiplayer character yet.");
                Tell(acSender, "  /character save <name>  - save how you look now as your character");
                return true;
            }

            Tell(acSender, fmt::format("Character: {}", pCharacter->Name.empty() ? "unnamed" : pCharacter->Name));
            Tell(acSender, fmt::format("  appearance {} bytes stored, {}",
                                       Base64::Decode(pCharacter->Appearance).size(),
                                       pCharacter->Initialised ? "initialised" : "not initialised yet"));
            Tell(acSender, "  /character new <name>  - retire this one and start again");
            return true;
        }

        // Asks the client for the appearance it has RIGHT NOW.
        //
        // Deliberately not read from the puppet's AppearanceComponent: that was captured
        // at spawn, so it would save whatever they looked like when they joined and
        // silently discard everything they just did in the creator. The client is the only
        // side that knows the current state, so the client is asked.
        //
        // The name is remembered here and applied when the appearance comes back, because
        // the reply carries no idea of what the player typed.
        if (target == "save")
        {
            if (!rest.empty())
            {
                auto* pMutable = aSender.get_mut<PlayerComponent>();
                pMutable->PendingCharacterName = rest.substr(0, 32);
            }

            server::OpenCharacterCreator capture;
            capture.set_capture_only(true);
            GServer->Send(acSender.Connection, capture);

            return true;
        }

        // How to change your appearance.
        //
        // Driving the game's creator directly is not possible from scripts - its system is
        // native-only, checked against the 2.31 type hierarchy rather than assumed. The
        // mirror is the game's own answer to the same problem and already works in a live
        // world, so it is what players are pointed at.
        //
        // Nothing to run afterwards: the client notices when the mirror closes and saves
        // it. Making players type a command to keep their own face was an implementation
        // limitation showing through the design.
        if (target == "create" || target == "edit")
        {
            Tell(acSender, "Visit any ripperdoc and change how you look - they do appearance, not just cyberware.");
            Tell(acSender, "It saves by itself when you close it. Use /name to choose what you are called.");
            return true;
        }

        if (target == "new")
        {
            if (store.RetireCharacter(acSender.DiscordId))
                Tell(acSender, "Your old character has been retired - it is kept, not deleted.");
            else
                Tell(acSender, "You had no character yet, so there was nothing to retire.");

            Tell(acSender, "Change how you look at any ripperdoc - it saves by itself.");
            return true;
        }

        Tell(acSender, "Usage: /character [show | create | new | save <name>]");
        return true;
    }

    // --------------------------------------------------------------- /help ----
    //
    // Nobody discovers a chat channel by accident, and an unlisted feature may as well
    // not exist. Only the commands the asker can actually use are listed - offering
    // someone /ban and then refusing it is worse than not mentioning it.
    if (command == "/help")
    {
        Tell(acSender, "Chat:");
        Tell(acSender, fmt::format("  just type          - local, heard within {:.0f}m", ChatRange::kLocal));
        Tell(acSender, fmt::format("  /yell <message>    - heard within {:.0f}m", ChatRange::kYell));
        Tell(acSender, fmt::format("  /whisper <message> - heard within {:.0f}m", ChatRange::kWhisper));

        if (acSender.HasAtLeast(EPermissionLevel::kAdmin))
            Tell(acSender, "  /advert <message>  - the whole server");

        Tell(acSender, "Other: /who");

        if (acSender.HasAtLeast(EPermissionLevel::kModerator))
        {
            Tell(acSender, "Staff: /kick <player> [reason], /bans");
            Tell(acSender, "       /jail <player> <minutes> [reason] - cell is where you stand");
            Tell(acSender, "       /unjail <player>");
            Tell(acSender, "       /kill <player> [reason] - sends them to the respawn point");
        }

        if (acSender.HasAtLeast(EPermissionLevel::kAdmin))
        {
            Tell(acSender, "Admin: /ban <player> [reason], /unban <discord id>");
            Tell(acSender, "       /tp <player>    - brings them to you");
            Tell(acSender, "       /tp to <player> - sends you to them");
            Tell(acSender, "       /return <player>");
            Tell(acSender, "       /setspawn - where players wake up after being downed");
            Tell(acSender, "       /setstart - where brand-new characters arrive");
        }

        return true;
    }

    // ---------------------------------------------------------------- /who ----
    if (command == "/who")
    {
        // Answered to the asker alone. One person checking who is online should not
        // print the roster into everyone else's chat.
        m_pWorld->each(
            [&](flecs::entity, const PlayerComponent& aOther)
            {
                Tell(acSender, fmt::format("{} [{}]", aOther.Username, ToString(aOther.Level)));
            });
        return true;
    }

    return false;
}

void ChatSystem::HandleChatMessageRequest(const PacketEvent<client::ChatMessageRequest>& aMessage)
{
    auto* pPlayerManager = m_pWorld->get<PlayerManager>();

    const auto entity = pPlayerManager->GetByConnectionId(aMessage.ConnectionId);
    if (!entity || !entity.has<PlayerComponent>())
    {
        spdlog::error("Received chat message from connection with no associated player!");
        return;
    }
    auto* pPlayer = entity.get<PlayerComponent>();

    // Length is checked before the message is logged, let alone broadcast.
    //
    // Chat is relayed verbatim to everyone in range and written to the log. With no cap, a
    // single client could send a megabyte and have the server copy it to every player and
    // to disk - a denial of service that costs the sender one packet, and needs no exploit
    // beyond a text field with no maximum.
    //
    // 512 is far more than anyone types. Truncating rather than dropping keeps an
    // over-long message readable instead of making it vanish with no explanation.
    constexpr size_t kMaxChatLength = 512;

    std::string line = aMessage.get_message().c_str();

    if (line.size() > kMaxChatLength)
    {
        spdlog::warn("Truncated a {}-byte chat message from {}", line.size(), pPlayer->Username);
        line.resize(kMaxChatLength);
    }

    // Control characters are stripped. They do nothing useful in a chat box and newlines
    // in particular let one message forge several lines in the log, which is how a chat
    // message starts pretending to be a server notice.
    line.erase(std::remove_if(line.begin(), line.end(),
                              [](unsigned char c) { return c < 0x20 && c != '\t'; }),
               line.end());

    if (line.empty())
        return;

    spdlog::info("[chat] [{}]: {}", pPlayer->Username, line);

    // Moderation commands. Every one of these checks the permission level the SERVER
    // derived from Discord at connect time - never anything the client said about itself.
    if (HandleModerationCommand(entity, *pPlayer, line))
        return;

    // Debug command: spawn a fake remote player next to the sender.
    //
    // The client-side crash we're chasing only happens when a REMOTE player spawns,
    // which normally needs a second person connected. Level::Add() broadcasts a
    // NotifyCharacterLoad for any entity with a MovementComponent, so a fabricated
    // one drives exactly the same client path - letting a single player reproduce
    // the crash on demand with a debugger attached.
    if (line == "/dummy")
    {
        auto* pOwnPuppet = pPlayer->Puppet ? pPlayer->Puppet.get<MovementComponent>() : nullptr;
        if (!pOwnPuppet)
        {
            spdlog::warn("[dummy] sender has no puppet yet - spawn into the world first");
            Broadcast("SERVER", "Spawn into the world first, then try /dummy again.");
            return;
        }

        // EXPERIMENT - distance is the one variable being changed.
        //
        // Puppets that arrive at connect time (at stale positions, far from the player)
        // complete the entire pipeline. A puppet spawned 2m away dies inside the game's
        // async assembly ~40ms later, before any of our code touches it again. Neither is
        // ever actually visible, so we cannot yet tell whether the trigger is PROXIMITY
        // (the game streams in and builds the puppet's mesh) or TIMING (mid-session spawns
        // differ from connect-time ones for some other reason).
        //
        // 200m is outside streaming range: the entity exists, but the game never builds
        // its visual representation.
        //   - survives  -> the crash is in mesh/appearance construction, and we look there
        //   - crashes   -> proximity is irrelevant and the difference is timing
        auto position = pOwnPuppet->Position;
        position.x += 200.f;

        // Deliberately NOT child_of(entity). Level::Add takes the entity's parent as its
        // owner and skips that player when broadcasting the spawn - you are not told about
        // your own puppet. Parenting the dummy to the sender therefore excluded the only
        // connected client from the very notification this command exists to trigger: the
        // server reported success, and the client was never asked to spawn anything.
        //
        // With no parent, owner is invalid, matches no player, and everyone is notified -
        // which is what a real remote player looks like to the person seeing it.
        auto dummy = m_pWorld->entity()
            .set<MovementComponent>({position, pOwnPuppet->Rotation, 0.f, pOwnPuppet->Tick})
            .set<CharacterComponent>({true})
            .set<AppearanceComponent>({{}, {}});

        spdlog::info("[dummy] spawning fake remote player {:x} at ({:.1f}, {:.1f}, {:.1f}) for {}",
                     static_cast<uint64_t>(dummy), position.x, position.y, position.z, pPlayer->Username);

        m_pWorld->get_mut<Level>()->Add(dummy);

        Broadcast("SERVER", "Spawned a dummy player next to you.");
        return;
    }

    // ------------------------------------------------------------------ channels ---
    //
    // Everything below is ordinary talking. Which channel it goes out on decides how far
    // it carries, and that is the whole point: a roleplay server where every line reaches
    // everyone is a group chat with a game attached.

    std::string text;
    float range = ChatRange::kLocal;
    bool everyone = false;
    uint32_t channel = ChatChannel::kLocal;

    if (!ResolveChannel(*pPlayer, line, text, range, everyone, channel))
        return; // Refused or misused - ResolveChannel already said why.

    // Chat shows the CHARACTER's name.
    //
    // The scanner already did - Level::Serialize sends it - so a player could be scanned
    // as one person and then speak as another, which is worse than either alone. Somebody
    // being "noremacxxi" while their character is somebody else is the point of roleplay,
    // and the account name leaking into the one place people read constantly undoes it.
    //
    // Falls back to the account name only until a character exists.
    std::string speaker = pPlayer->Username;

    if (const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(pPlayer->DiscordId))
    {
        if (!pCharacter->Name.empty())
            speaker = pCharacter->Name;
    }

    if (everyone)
    {
        Broadcast(speaker.c_str(), text.c_str(), channel);
        return;
    }

    // Ranged chat needs somewhere to speak from. Someone connected but not yet spawned
    // has no position, so there is no honest way to decide who is close enough.
    const auto* pMovement = pPlayer->Puppet ? pPlayer->Puppet.get<MovementComponent>() : nullptr;
    if (!pMovement)
    {
        Tell(*pPlayer, "You need to be in the world before anyone can hear you.");
        return;
    }

    BroadcastInRange(speaker, text, pMovement->Position, range, entity, channel);
}
