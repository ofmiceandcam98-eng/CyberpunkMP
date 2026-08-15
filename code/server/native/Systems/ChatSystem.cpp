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

    if (blob.empty() || blob.size() > kMaxCcstate)
    {
        spdlog::warn("Refused a character save from {} - {} bytes of appearance",
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
    const auto findPlayer = [&](const std::string& acName) -> flecs::entity
    {
        flecs::entity found{};

        m_pWorld->each(
            [&](flecs::entity aEntity, const PlayerComponent& aOther)
            {
                if (found)
                    return;

                if (aOther.Username.size() != acName.size())
                    return;

                if (std::equal(aOther.Username.begin(), aOther.Username.end(), acName.begin(),
                               [](char a, char b) { return std::tolower(a) == std::tolower(b); }))
                {
                    found = aEntity;
                }
            });

        return found;
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

        // How to change your appearance, until the creator can be opened on demand.
        //
        // Driving the game's creator directly is not possible from scripts - its system is
        // native-only, which was checked against the 2.31 type hierarchy rather than
        // assumed. The mirror is the game's own answer to the same problem and it already
        // works in a live world, so it is what players are pointed at.
        if (target == "create" || target == "edit")
        {
            Tell(acSender, "Use a mirror to change how you look - the one in V's apartment works.");
            Tell(acSender, "When you are happy with it, run /character save <name>.");
            return true;
        }

        if (target == "new")
        {
            if (store.RetireCharacter(acSender.DiscordId))
                Tell(acSender, "Your old character has been retired - it is kept, not deleted.");
            else
                Tell(acSender, "You had no character yet, so there was nothing to retire.");

            Tell(acSender, "Change how you look at a mirror, then run /character save <name>.");
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
            Tell(acSender, "       /return <player>, /setspawn");
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

    if (everyone)
    {
        Broadcast(pPlayer->Username.c_str(), text.c_str(), channel);
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

    BroadcastInRange(pPlayer->Username, text, pMovement->Position, range, entity, channel);
}
