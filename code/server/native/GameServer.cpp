#include "GameServer.h"

#include <cstdlib>   // getenv, for the bot token
#include <algorithm> // sort, for the digest's canonical component order

#include <openssl/sha.h>  // already linked for the Discord HTTPS calls
#include <BuildInfo.h>    // BUILD_COMMIT - so a protocol denial can name the stale side

#include <Components/PlayerComponent.h>
#include <Components/MovementComponent.h>
#include "Systems/ChatSystem.h"   // jail radius and the chat channel ids

#include "Core/Filesystem.h"
#include "Game/Level.h"
#include "Scripting/IRpc.h"
#include "Scripting/RpcScriptInstance.h"

#include "PlayerManager.h"


using nlohmann::json;

GameServer* GServer = nullptr;

GameServer::GameServer()
    : Server(client::kIdentifier, server::kIdentifier)
    , m_log(m_path)
    , m_lastUpdate(std::chrono::steady_clock::now())
{
    GServer = this;

    try
    {
        auto serverPath = GetPath();

        std::ifstream config(serverPath / "config" / "server.json");
        if (!config.is_open())
        {
            spdlog::info("No configuration file found, creating config/server.json");
            std::error_code ec;
            create_directory(serverPath / "config", ec);

            std::ofstream of(serverPath / "config" / "server.json");
            json data = m_config;
            of << std::setw(4) << data;
        }
        else
        {
            json data = json::parse(config);
            m_config = data.get<Config>();
        }

        m_bans.Load(serverPath / "config" / "bans.json");
        m_players.Load(serverPath / "config" / "players.json");
        m_worldFacts.Load(serverPath / "config" / "worldfacts.json");
        m_vehicles.Load(serverPath / "config" / "vehicles.json");

        LoadServerManifest(serverPath / "config" / "server-manifest.json");

        // Opened last, so its first line records a server that has finished loading its
        // state rather than one that may still fail on a malformed store.
        //
        // Under config/ with the rest, but note it is not the same KIND of thing: the
        // stores above are authoritative state and are the standing problem for running a
        // second instance, whereas this is a record nothing reads back. It moves to the
        // database with them; it does not block on it.
        m_audit.Open(serverPath / "config" / "audit.log");

        m_respawnPath = serverPath / "config" / "respawn.json";
        if (std::ifstream file(m_respawnPath); file.is_open())
        {
            try
            {
                const auto data = nlohmann::json::parse(file);
                m_respawnPosition = {data.value("x", 0.f), data.value("y", 0.f), data.value("z", 0.f)};
                m_respawnYaw = data.value("yaw", 0.f);
                m_hasRespawnPoint = true;

                spdlog::info("Respawn point loaded: ({:.1f}, {:.1f}, {:.1f})", m_respawnPosition.x,
                             m_respawnPosition.y, m_respawnPosition.z);
            }
            catch (const std::exception& e)
            {
                // Not fatal. Without a respawn point players simply revive where they
                // fell, which is worse than the Afterlife but not broken.
                spdlog::error("Could not read {}: {}", m_respawnPath.string(), e.what());
            }
        }

        m_startPath = serverPath / "config" / "startpoint.json";
        if (std::ifstream file(m_startPath); file.is_open())
        {
            try
            {
                const auto data = nlohmann::json::parse(file);
                m_startPosition = {data.value("x", 0.f), data.value("y", 0.f), data.value("z", 0.f)};
                m_startYaw = data.value("yaw", 0.f);
                m_hasStartPoint = true;

                spdlog::info("Start point loaded: ({:.1f}, {:.1f}, {:.1f})", m_startPosition.x,
                             m_startPosition.y, m_startPosition.z);
            }
            catch (const std::exception& e)
            {
                // Not fatal. Without one, new characters simply begin wherever the world
                // template put them, which is where they began before this existed.
                spdlog::error("Could not read {}: {}", m_startPath.string(), e.what());
            }
        }
    }
    catch (std::exception& e)
    {
        spdlog::error("Error parsing config.json: {}", e.what());
        m_run = false;
        return;
    }

    uint16_t port = m_config.Port;
    while (!Host(port, m_config.TickRate))
    {
        spdlog::warn("Port {} is already in use, trying {}", port, port + 1);
        port++;
    }

    m_pWorld = MakeUnique<World>(m_config.GetFlecsConfig());
    m_pWorld->GetScriptInstance()->Initialize();

    RegisterHandler<&GameServer::HandleAuthentication>(this);

    spdlog::info("Server started on port {}", GetPort());

    // Pull the guild's roles now rather than waiting for the first player.
    //
    // It is only needed when somebody connects, but doing it lazily means roles.json does
    // not exist until then - so the owner has no way to check the role mapping is working
    // short of asking a friend to join. Detached because it is a network call and nothing
    // here should wait on Discord.
    if (m_config.Discord.Enabled && !m_config.Discord.GuildId.empty())
    {
        std::thread(
            [this]()
            {
                const auto names = GetGuildRoleNames();

                if (names.empty())
                    spdlog::info("No Discord role names available - only role IDS in the config will grant anything. "
                                 "Set Discord.BotTokenFile to use names.");
                else
                    spdlog::info("Loaded {} Discord role name(s)", names.size());
            })
            .detach();
    }
}

GameServer::~GameServer()
{
    GServer = nullptr;
}

void GameServer::Kill()
{
    m_run = false;
    Close();
}

void GameServer::Run()
{
    while (m_run && IsListening())
        Update();
}

void GameServer::OnUpdate()
{
    const auto now = std::chrono::steady_clock::now();
    const auto delta = now - m_lastUpdate;
    m_lastUpdate = now;

    m_tasks.Drain();
    m_dispatcher.update();

    ReportPlayerConnections(now);
    ReverifyPlayers(now);
    SavePlayerPositions(now);
    EnforceJail(now);

    m_pWorld->Update(std::chrono::duration_cast<std::chrono::duration<float>>(delta).count());
}

void GameServer::SavePlayerPositions(std::chrono::steady_clock::time_point aNow)
{
    const auto sinceLast = std::chrono::duration_cast<std::chrono::seconds>(aNow - m_lastPlayerSave).count();

    // Every thirty seconds. This is the "autosave" - not a Cyberpunk save file, which
    // would hitch the game and can be corrupted by a crash mid-write, but the server
    // writing down what it already knows. Cheap enough to do often, and thirty seconds is
    // the most anyone loses if the SERVER dies. A player crashing loses nothing at all,
    // because their disconnect saves immediately.
    if (sinceLast < 30)
        return;

    m_lastPlayerSave = aNow;

    m_pWorld->each(
        [this](flecs::entity, const PlayerComponent& aPlayer)
        {
            const auto* pMovement = aPlayer.Puppet ? aPlayer.Puppet.get<MovementComponent>() : nullptr;
            if (!pMovement)
                return;   // connected but not standing anywhere yet

            m_players.Remember(aPlayer.DiscordId, aPlayer.Username, pMovement->Position, pMovement->Rotation.z);
        });

    m_players.Flush();
}

void GameServer::SetRespawnPoint(const glm::vec3& acPosition, float aYaw)
{
    m_respawnPosition = acPosition;
    m_respawnYaw = aYaw;
    m_hasRespawnPoint = true;

    try
    {
        std::filesystem::create_directories(m_respawnPath.parent_path());

        std::ofstream file(m_respawnPath);
        file << nlohmann::json{{"x", acPosition.x}, {"y", acPosition.y}, {"z", acPosition.z}, {"yaw", aYaw}}.dump(2);
    }
    catch (const std::exception& e)
    {
        spdlog::error("Could not write {}: {}", m_respawnPath.string(), e.what());
    }
}

bool GameServer::GetRespawnPoint(glm::vec3& aPosition, float& aYaw) const
{
    if (!m_hasRespawnPoint)
        return false;

    aPosition = m_respawnPosition;
    aYaw = m_respawnYaw;
    return true;
}

void GameServer::SetStartPoint(const glm::vec3& acPosition, float aYaw)
{
    m_startPosition = acPosition;
    m_startYaw = aYaw;
    m_hasStartPoint = true;

    try
    {
        std::filesystem::create_directories(m_startPath.parent_path());

        std::ofstream file(m_startPath);
        file << nlohmann::json{{"x", acPosition.x}, {"y", acPosition.y}, {"z", acPosition.z}, {"yaw", aYaw}}.dump(2);
    }
    catch (const std::exception& e)
    {
        spdlog::error("Could not write {}: {}", m_startPath.string(), e.what());
    }
}

bool GameServer::GetStartPoint(glm::vec3& aPosition, float& aYaw) const
{
    if (!m_hasStartPoint)
        return false;

    aPosition = m_startPosition;
    aYaw = m_startYaw;
    return true;
}

void GameServer::EnforceJail(std::chrono::steady_clock::time_point aNow)
{
    // Once a second. Often enough that walking out is pointless, rarely enough that it
    // costs nothing - and slow enough that the teleport reads as being dragged back
    // rather than as the game stuttering.
    if (std::chrono::duration_cast<std::chrono::milliseconds>(aNow - m_lastJailCheck).count() < 1000)
        return;

    m_lastJailCheck = aNow;

    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    Vector<std::pair<ConnectionId, glm::vec3>> toReturn;
    Vector<std::pair<ConnectionId, std::string>> toRelease;

    m_pWorld->each(
        [&](flecs::entity, const PlayerComponent& aPlayer)
        {
            const auto* pRecord = m_players.Find(aPlayer.DiscordId);
            if (!pRecord || pRecord->JailedUntil == 0)
                return;

            // Sentence served. Released wherever they are - dragging someone back to the
            // cell to let them out of it would be a strange last act.
            if (seconds >= pRecord->JailedUntil)
            {
                toRelease.emplace_back(aPlayer.Connection, aPlayer.DiscordId);
                return;
            }

            const auto* pMovement = aPlayer.Puppet ? aPlayer.Puppet.get<MovementComponent>() : nullptr;
            if (!pMovement)
                return;

            const glm::vec3 cell{pRecord->JailX, pRecord->JailY, pRecord->JailZ};

            if (glm::distance(pMovement->Position, cell) > kJailRadius)
                toReturn.emplace_back(aPlayer.Connection, cell);
        });

    // Sent outside the iteration. Sending while walking the world is asking for trouble
    // if a handler ever touches the entity list.
    for (const auto& [connection, cell] : toReturn)
    {
        server::NotifyTeleport teleport;

        common::Vector3 position;
        position.set_x(cell.x);
        position.set_y(cell.y);
        position.set_z(cell.z);
        teleport.set_position(position);
        teleport.set_rotation(0.f);

        Send(connection, teleport);
    }

    for (const auto& [connection, discordId] : toRelease)
    {
        m_players.ClearJail(discordId);

        server::ChatMessage message;
        message.set_username("SERVER");
        message.set_message("Your sentence is served. You are free to go.");
        message.set_channel(ChatChannel::kServer);

        Send(connection, message);

        spdlog::info("Released {} from jail", discordId);
    }
}

void GameServer::ReverifyPlayers(std::chrono::steady_clock::time_point aNow)
{
    if (!m_config.Discord.Enabled)
        return;

    const auto sinceLast = std::chrono::duration_cast<std::chrono::seconds>(aNow - m_lastReverify).count();

    // Every two minutes. Frequent enough that a ban takes effect while the moderator is
    // still watching, infrequent enough to be nothing to Discord's rate limits.
    if (sinceLast < 120)
        return;

    m_lastReverify = aNow;

    // Snapshot plain values - connection id and token. Never capture a flecs entity and
    // hand it to another thread: the world can change underneath it, and the entity may
    // not exist by the time the check comes back.
    Vector<std::pair<ConnectionId, std::string>> toCheck;

    m_pWorld->each(
        [&toCheck](flecs::entity, const PlayerComponent& aPlayer)
        {
            if (!aPlayer.DiscordToken.empty())
                toCheck.emplace_back(aPlayer.Connection, aPlayer.DiscordToken);
        });

    if (toCheck.empty())
        return;

    // Off the game thread. These are blocking HTTPS calls - doing them inline would
    // stall the simulation for every connected player, every two minutes.
    std::thread(
        [this, toCheck = std::move(toCheck)]()
        {
            for (const auto& [connection, token] : toCheck)
            {
                DiscordIdentity identity;
                const auto result = VerifyDiscordToken(token, identity);

                // Only act on a definite answer. An unreachable Discord must not kick
                // anyone - an outage would empty the server, and that is a far worse
                // failure than a banned player staying on for a few extra minutes.
                if (result == EDiscordAuthResult::kUnreachable)
                    continue;

                const bool revoked = (result == EDiscordAuthResult::kNotAMember ||
                                      result == EDiscordAuthResult::kInvalidToken);
                const auto level = identity.Level;
                const auto username = identity.Username;

                // Back to the game thread to touch the world or the connection.
                GetTaskQueue()->Add(
                    [this, connection, revoked, level, username]()
                    {
                        auto* pPlayerManager = m_pWorld->get_mut<PlayerManager>();
                        const auto player = pPlayerManager->GetByConnectionId(connection);

                        if (!player)
                            return;

                        auto* pComponent = player.get_mut<PlayerComponent>();
                        if (!pComponent)
                            return;

                        if (revoked)
                        {
                            // Banned, kicked, or they left the Discord. Either way they no
                            // longer meet the condition for being here.
                            spdlog::info("Removing {} - no longer a member of the Discord",
                                         pComponent->Username);
                            Kick(connection);
                            return;
                        }

                        // Roles can change too. Promote someone to moderator in Discord and
                        // they have it in game within a couple of minutes, without rejoining.
                        if (pComponent->Level != level)
                        {
                            spdlog::info("{} permission changed: {} -> {}", pComponent->Username,
                                         ToString(pComponent->Level), ToString(level));
                            pComponent->Level = level;
                        }
                    });
            }
        })
        .detach();
}

void GameServer::ReportPlayerConnections(std::chrono::steady_clock::time_point aNow)
{
    // Per-player link health. The transport measures all of this already; without
    // surfacing it, "the server feels laggy" is unanswerable - you cannot tell a struggling
    // server from one player on a bad connection.
    const auto sinceLast = std::chrono::duration_cast<std::chrono::seconds>(aNow - m_lastConnectionReport).count();

    if (sinceLast < 30)
        return;

    m_lastConnectionReport = aNow;

    m_pWorld->each(
        [this](flecs::entity, const PlayerComponent& aPlayer)
        {
            const auto status = GetConnectionRealTimeStatus(aPlayer.Connection);

            // -1 means "not measured yet", not "everything is lost". Feeding it into
            // (1 - quality) produced "200.0% loss - DEGRADED" for a player on a
            // perfect local connection, which is worse than saying nothing: it
            // reports a fault that does not exist and trains people to ignore it.
            const bool localKnown = status.m_flConnectionQualityLocal >= 0.f;
            const bool remoteKnown = status.m_flConnectionQualityRemote >= 0.f;

            if (!localKnown && !remoteKnown)
            {
                spdlog::info("[Link] {} - ping {}ms, quality not measured yet", aPlayer.Username, status.m_nPing);
                return;
            }

            float quality = 1.f;
            if (localKnown) quality = std::min(quality, status.m_flConnectionQualityLocal);
            if (remoteKnown) quality = std::min(quality, status.m_flConnectionQualityRemote);

            const bool degraded = status.m_nPing > 200 || quality < 0.95f;

            if (degraded)
            {
                spdlog::warn("[Link] {} DEGRADED - ping {}ms, {:.1f}% loss, {} unacked bytes",
                             aPlayer.Username, status.m_nPing, (1.f - quality) * 100.f,
                             status.m_cbSentUnackedReliable);
            }
            else
            {
                spdlog::info("[Link] {} - ping {}ms, {:.1f}% loss", aPlayer.Username, status.m_nPing,
                             (1.f - quality) * 100.f);
            }
        });
}

void GameServer::OnConsume(const void* apData, uint32_t aSize, ConnectionId aConnectionId)
{
    // ReSharper disable once CppCStyleCast
    ViewBuffer buffer((uint8_t*)apData, aSize);  // NOLINT(clang-diagnostic-cast-qual)
    Buffer::Reader reader(&buffer);

    if (const auto result = client::Deserializer::Process(reader, aConnectionId, m_dispatcher); !result)
    {
        spdlog::error("Failed to deserialize packet from {}", aConnectionId);
    }
}

void GameServer::OnConnection(ConnectionId aHandle)
{
    // Log at info, with the peer address. At debug level this is invisible by
    // default, which makes "the client cannot connect" impossible to diagnose:
    // there is no way to tell a client that never reached us from one that
    // reached us and failed later.
    char address[SteamNetworkingIPAddr::k_cchMaxString] = {};
    const auto info = GetConnectionInfo(aHandle);
    info.m_addrRemote.ToString(address, sizeof(address), true);

    spdlog::info("Connection received from {} (id {:x})", address, aHandle);
}

void GameServer::OnDisconnection(ConnectionId aConnectionId, EDisconnectReason aReason)
{
    spdlog::info("Connection {:x} ended (reason {})", aConnectionId, static_cast<uint32_t>(aReason));

    // If their verification is still out with Discord, the verdict no longer has a
    // recipient. The completion task checks this set before acting, so erasing here is
    // what turns a mid-verify disconnect into a non-event instead of a join for a ghost.
    m_pendingVerifies.erase(aConnectionId);

    auto* pPlayerManager = GetWorld()->get_mut<PlayerManager>();

    if (const auto player = pPlayerManager->GetByConnectionId(aConnectionId))
    {
        pPlayerManager->Remove(player);

        if (auto* pPlayerComponent = player.get<PlayerComponent>())
        {
            // Save BEFORE the puppet is removed - afterwards there is no position left
            // to read. This is the branch that matters most: a crashed game drops its
            // connection, so this runs for a crash exactly as it does for a clean quit,
            // and the player comes back where they fell over rather than wherever their
            // singleplayer save happens to put them.
            if (const auto* pMovement = pPlayerComponent->Puppet
                                            ? pPlayerComponent->Puppet.get<MovementComponent>()
                                            : nullptr)
            {
                m_players.Remember(pPlayerComponent->DiscordId, pPlayerComponent->Username,
                                   pMovement->Position, pMovement->Rotation.z);
                m_players.Flush();
            }

            auto* pLevel = GetWorld()->get_mut<Level>();

            pLevel->Remove(pPlayerComponent->Puppet);

            // Before player.destruct(), which would take the cars with it without telling
            // anybody - see Level::RemoveOwnedVehicles.
            pLevel->RemoveOwnedVehicles(player);
        }

        player.destruct();

        spdlog::info("{}/{} player(s) online", pPlayerManager->Count(), m_config.MaxPlayer);
    }
}

void GameServer::LoadServerManifest(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath);
    if (!file.is_open())
    {
        // The migration state, not an error: releases predating the manifest system have
        // nothing to copy here, and the launcher-side fallback covers those players.
        spdlog::info("No server-manifest.json - manifest checks disabled (migration state)");
        return;
    }

    try
    {
        const auto data = json::parse(file);

        m_manifestVersion = data.value("manifestVersion", "");
        if (m_manifestVersion.empty())
        {
            spdlog::error("server-manifest.json has no manifestVersion - manifest checks stay disabled");
            return;
        }

        m_unknownModPolicy = data.contains("policy") ? data["policy"].value("unknownMods", "warn") : "warn";

        // The install digest, computed once from manifest-declared fields only - never
        // from anything a client could vary. The canonical string is (and must stay)
        // byte-identical to computeInstallDigest in code/launcher-lite/manifest.js:
        //
        //   for every component with required==true and audience=="all",
        //   sorted by id ascending:   id + ":" + version + ":" + archive.sha256 + "\n"
        //   then:                     "payload:" + client.payload.archive.sha256 + "\n"
        //   then (no trailing \n):    "manifest:" + manifestVersion
        //
        // sha256 over the UTF-8 bytes, lowercase hex.
        std::vector<std::pair<std::string, std::string>> lines;
        if (data.contains("components"))
        {
            for (const auto& component : data["components"])
            {
                // The helper rule (crew decree 2026-08-22): content mods are never
                // load-bearing. The generator refuses to emit class:"nexus" with
                // required:true and the launcher treats such a manifest as invalid;
                // a copy reaching this server was crafted around both. Same posture
                // as the malformed-component case below: checks stay disabled, loudly,
                // rather than gating every join on a content mod's exact version.
                if (component.value("class", "") == "nexus" && component.value("required", false))
                {
                    spdlog::error("server-manifest.json component '{}' is class:nexus yet marked required - "
                                  "content mods are never load-bearing; manifest checks stay disabled",
                                  component.value("id", "?"));
                    m_manifestVersion.clear();
                    return;
                }

                if (!component.value("required", false) || component.value("audience", "all") != "all")
                    continue;

                const auto id = component.value("id", "");
                const auto version = component.value("version", "");
                const std::string archiveSha = component.contains("archive") ? component["archive"].value("sha256", "") : "";

                // The generator guarantees these for required components, so absence
                // means a malformed manifest - refuse to compute a digest that the
                // launcher (which throws on the same condition) can never match.
                if (id.empty() || version.empty() || archiveSha.empty())
                {
                    spdlog::error("server-manifest.json component '{}' is required but missing version/archive hash - manifest checks stay disabled", id);
                    m_manifestVersion.clear();
                    return;
                }

                lines.emplace_back(id, id + ":" + version + ":" + archiveSha);
            }
        }

        // Sorted by ID, not by the assembled line - the two differ once one id is a
        // prefix of another ("mod2" sorts before "mod24" by id, but "mod2:" sorts AFTER
        // "mod24:" byte-wise because ':' outranks '4'). The launcher sorts by id; a
        // whole-line sort here would disagree on exactly the manifests with such ids.
        std::sort(lines.begin(), lines.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        std::string canonical;
        for (const auto& line : lines)
            canonical += line.second + "\n";

        std::string payloadSha;
        if (data.contains("client") && data["client"].contains("payload") && data["client"]["payload"].contains("archive"))
            payloadSha = data["client"]["payload"]["archive"].value("sha256", "");

        canonical += "payload:" + payloadSha + "\n";
        canonical += "manifest:" + m_manifestVersion;

        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(canonical.data()), canonical.size(), digest);

        m_expectedDigest.clear();
        m_expectedDigest.reserve(SHA256_DIGEST_LENGTH * 2);
        static constexpr char kHex[] = "0123456789abcdef";
        for (const unsigned char byte : digest)
        {
            m_expectedDigest.push_back(kHex[byte >> 4]);
            m_expectedDigest.push_back(kHex[byte & 0xF]);
        }

        spdlog::info("Manifest {} loaded: expecting install digest {}, unknown-mod policy '{}'",
                     m_manifestVersion, m_expectedDigest, m_unknownModPolicy);
    }
    catch (const std::exception& e)
    {
        // Fail open on a malformed manifest, loudly. Failing closed here would let one
        // bad deploy refuse every player, and the launcher already refused to verify
        // against a manifest whose signature does not check out.
        spdlog::error("Could not read {}: {} - manifest checks disabled", acPath.string(), e.what());
        m_manifestVersion.clear();
        m_expectedDigest.clear();
    }
}

void GameServer::Deny(const ConnectionId aConnectionId, const EDenialCode aCode,
                      const std::string& acReason, const bool aIncludeRequiredManifest)
{
    server::AuthenticationResponse response;
    response.set_success(false);
    response.set_error(acReason.c_str());
    response.set_denial_code(static_cast<uint32_t>(aCode));

    if (aIncludeRequiredManifest && !m_manifestVersion.empty())
        response.set_required_manifest(m_manifestVersion.c_str());

    Send(aConnectionId, response);
    Kick(aConnectionId);
}

bool GameServer::DenyIfFull(const ConnectionId aConnectionId)
{
    const auto* pPlayerManager = GetWorld()->get_mut<PlayerManager>();
    if (pPlayerManager && pPlayerManager->Count() >= m_config.MaxPlayer)
    {
        spdlog::info("Refused connection {:x}: server is full ({}/{})", aConnectionId,
                     pPlayerManager->Count(), m_config.MaxPlayer);
        Deny(aConnectionId, EDenialCode::kServerFull,
             "The server is full (" + std::to_string(m_config.MaxPlayer) + " players). Try again in a bit.");
        return true;
    }

    return false;
}

void GameServer::HandleAuthentication(const PacketEvent<client::AuthenticationRequest>& aRequest)
{
    if (aRequest.get_client_protocol() != client::kIdentifier)
    {
        // The build stamp turns "Invalid protocol version!" from undiagnosable into a
        // named pair of builds - which side is stale is now readable from either log.
        spdlog::warn("Connection attempt with client identifier {:x}, expected {:x} (their build '{}', ours '{}')",
                     aRequest.get_client_protocol(), client::kIdentifier,
                     aRequest.get_build_stamp().c_str(), BUILD_COMMIT);
        Deny(aRequest.ConnectionId, EDenialCode::kProtocolMismatch,
             "Your mod is built against a different protocol than this server. Open the launcher and update.");
        return;
    }

    if (aRequest.get_server_protocol() != server::kIdentifier)
    {
        spdlog::warn("Connection attempt with server identifier {:x}, expected {:x} (their build '{}', ours '{}')",
                     aRequest.get_server_protocol(), server::kIdentifier,
                     aRequest.get_build_stamp().c_str(), BUILD_COMMIT);
        Deny(aRequest.ConnectionId, EDenialCode::kProtocolMismatch,
             "Your mod is built against a different protocol than this server. Open the launcher and update.");
        return;
    }

    // Checked before Discord is asked anything: a wrong password costs nothing to refuse,
    // and the password gates the SERVER, not the identity.
    if (!m_config.Password.empty() && m_config.Password != aRequest.get_password().c_str())
    {
        spdlog::warn("Refused connection {:x}: wrong server password", aRequest.ConnectionId);
        Deny(aRequest.ConnectionId, EDenialCode::kWrongPassword, "Wrong server password.");
        return;
    }

    // Manifest checks run only when the deploy provided one (migration otherwise). An
    // empty client manifest_version means "started without the launcher", which during
    // migration is every old client - the equality check catches both cases at once.
    if (!m_manifestVersion.empty())
    {
        const std::string clientManifest = aRequest.get_manifest_version().c_str();
        if (clientManifest != m_manifestVersion)
        {
            spdlog::info("Refused connection {:x}: manifest '{}', server wants '{}'",
                         aRequest.ConnectionId, clientManifest, m_manifestVersion);
            Deny(aRequest.ConnectionId, EDenialCode::kManifestMismatch,
                 "Your installation was verified against manifest " +
                     (clientManifest.empty() ? std::string("(none)") : clientManifest) +
                     " but this server runs " + m_manifestVersion + ". Open the launcher and update.",
                 true);
            return;
        }

        // Same manifest version but different content verification outcome. This is the
        // accident detector of docs/MANIFEST-ARCHITECTURE.md 7.2 - a half-updated or
        // wrong-branch install cannot produce the right digest by accident, and that is
        // all it claims to catch.
        if (m_expectedDigest != aRequest.get_install_digest().c_str())
        {
            spdlog::info("Refused connection {:x}: install digest mismatch (theirs '{}', expected '{}')",
                         aRequest.ConnectionId, aRequest.get_install_digest().c_str(), m_expectedDigest);
            Deny(aRequest.ConnectionId, EDenialCode::kDigestMismatch,
                 "Your installation does not match what the launcher verified for manifest " +
                     m_manifestVersion + ". Open the launcher and press Verify.",
                 true);
            return;
        }

        // Advisory by design: this list is client-computed, so it stops the player who
        // FORGOT about a mod, never one hiding it - which is exactly the population an
        // RP server actually has (see the doc, 7.3). Never presented as a security gate.
        if (!aRequest.get_unmanaged().empty())
        {
            std::string names;
            for (const auto& entry : aRequest.get_unmanaged())
            {
                if (!names.empty())
                    names += ", ";
                names += entry.c_str();
            }

            if (m_unknownModPolicy == "block")
            {
                spdlog::info("Refused connection {:x}: unmanaged mods present ({})", aRequest.ConnectionId, names);
                Deny(aRequest.ConnectionId, EDenialCode::kUnmanagedBlocked,
                     "This server does not allow unmanaged mods. Found: " + names);
                return;
            }

            spdlog::info("Connection {:x} carries unmanaged mods ({}) - policy '{}', admitted",
                         aRequest.ConnectionId, names, m_unknownModPolicy);
        }
    }

    if (DenyIfFull(aRequest.ConnectionId))
        return;

    // Identity is decided by asking Discord - not by believing the client.
    // Note: the project's String has a custom allocator, so cross to std::string via c_str()
    // at the boundary with httplib rather than assuming they are interchangeable.
    if (!m_config.Discord.Enabled)
    {
        const std::string username = aRequest.get_username().c_str();

        spdlog::info("Authorize connection from {} (Discord verification disabled)", username);

        AdmitPlayer(aRequest.ConnectionId, username, {}, EPermissionLevel::kPlayer,
                    aRequest.get_token().c_str());
        return;
    }

    // A join already being verified needs no second verification. The client sends one
    // AuthenticationRequest per connection, so a duplicate here is a retransmit or a
    // misbehaving client - either way, the answer already on its way covers it.
    if (m_pendingVerifies.count(aRequest.ConnectionId))
        return;

    m_pendingVerifies.insert(aRequest.ConnectionId);

    // The verify is up to two blocking HTTPS calls with five-second timeouts, and this
    // handler runs on the thread that relays everyone's movement. Inline, every cold join
    // froze every player's stream for the duration - the excavation measured 4/4
    // crash-rejoins each hitching the relay. So only the WAITING moves to a worker; the
    // world, the player manager and the connection are still touched exclusively from the
    // game thread, via the task queue, exactly as ReverifyPlayers already does. Until the
    // completion task runs, no player entity exists for this connection, so every handler
    // refuses its traffic the same way it refuses any unauthenticated connection.
    std::thread(
        [this, connection = aRequest.ConnectionId,
         token = std::string(aRequest.get_token().c_str())]()
        {
            DiscordIdentity identity;
            const auto result = VerifyDiscordToken(token, identity);

            GetTaskQueue()->Add(
                [this, connection, token, result, identity]()
                { FinishAuthentication(connection, result, identity, token); });
        })
        .detach();
}

void GameServer::FinishAuthentication(const ConnectionId aConnectionId, const EDiscordAuthResult aResult,
                                      const DiscordIdentity& acIdentity, const std::string& acToken)
{
    // The connection can die while Discord is answering. OnDisconnection removes it from
    // the pending set, so a missing entry means this verdict has no recipient - dropped
    // without a word, as if they had never asked. IsAlive covers the remaining sliver:
    // a disconnect the transport has seen that has not reached OnDisconnection yet.
    if (m_pendingVerifies.erase(aConnectionId) == 0 || !IsAlive(aConnectionId))
        return;

    if (aResult != EDiscordAuthResult::kOk)
    {
        const char* reason = "Discord sign-in required.";
        auto code = EDenialCode::kDiscordExpired;

        switch (aResult)
        {
        case EDiscordAuthResult::kInvalidToken:
            reason = "Your Discord sign-in has expired. Open the launcher and sign in again.";
            code = EDenialCode::kDiscordExpired;
            break;
        case EDiscordAuthResult::kNotAMember:
            reason = "You must be a member of the Night City Online Discord to play here.";
            code = EDenialCode::kNotAMember;
            break;
        case EDiscordAuthResult::kUnreachable:
            // Fail closed. Membership is required, so an unverifiable token is not a
            // token we can accept - letting people in during an outage would mean
            // anyone could join by simply making Discord unreachable.
            reason = "Cannot reach Discord to verify your account right now. Try again shortly.";
            code = EDenialCode::kDiscordUnreachable;
            break;
        default:
            break;
        }

        spdlog::warn("Rejected connection {:x}: {}", aConnectionId, reason);
        Deny(aConnectionId, code, reason);
        return;
    }

    // Banned? Checked here, after Discord has told us who they actually are, so the
    // ban follows the account rather than a name or address they can change.
    if (const auto* pBan = m_bans.Find(acIdentity.Id))
    {
        spdlog::info("Rejected banned player {} ({}): {}", acIdentity.Username, acIdentity.Id,
                     pBan->Reason);

        Deny(aConnectionId, EDenialCode::kBanned,
             pBan->Reason.empty() ? std::string("You are banned from this server.")
                                  : "You are banned from this server: " + std::string(pBan->Reason.c_str()));
        return;
    }

    // Re-checked here because the count can grow while Discord answers - two players
    // racing for the last slot both pass the pre-verify check, and only this one decides.
    if (DenyIfFull(aConnectionId))
        return;

    // Both ids together, on purpose. Players see only the derived number, so a
    // report saying "232998 is griefing" is meaningless unless the log ties it
    // back to an account you can actually moderate.
    spdlog::info("Authorised {} [player {}] (discord {}) as {} on connection {:x}", acIdentity.Username,
                 DerivePlayerId(acIdentity.Id), acIdentity.Id, ToString(acIdentity.Level), aConnectionId);

    AdmitPlayer(aConnectionId, acIdentity.Username, acIdentity.Id, acIdentity.Level, acToken);
}

void GameServer::AdmitPlayer(const ConnectionId aConnectionId, const std::string& acUsername,
                             const std::string& acDiscordId, const EPermissionLevel aLevel,
                             const std::string& acToken)
{
    server::AuthenticationResponse response;
    response.set_success(true);

    server::Settings settings;
    settings.set_update_rate(m_config.UpdateRate);
    settings.set_world_id("night-city");
    settings.set_coordinate_version(1);
    settings.set_cell_size(60000);
    settings.set_interest_radius(3);
    response.set_settings(settings);

    // Tell them what character this ACCOUNT owns, before they can decide for themselves.
    //
    // Keyed on the Discord id the server just verified - never on anything the client
    // sent, and never on the Cyberpunk save sitting on their disk, which looks like an
    // answer and is not one. One account, at most one character: the store is keyed that
    // way, so "which of their characters" is not a question that can be asked.
    //
    // Absent when Discord verification is disabled, because then there is no durable
    // account to hang a character on and every connection is effectively a stranger.
    if (!acDiscordId.empty())
    {
        // One lookup, appended to a list. The store is keyed on Discord id so there can
        // only be one - but the shape is a list, so the day slots exist this loop grows
        // rather than every reader changing.
        if (const auto* pCharacter = m_players.FindCharacter(acDiscordId))
        {
            server::CharacterSummary summary;

            // Through c_str(): the project's String carries a custom allocator and is not
            // interchangeable with std::string at this boundary.
            summary.set_id(pCharacter->CharacterId.c_str());
            summary.set_name(pCharacter->Name.c_str());
            summary.set_spawned_before(pCharacter->SpawnedBefore);

            // The record's own Level field, not a search through Proficiencies. Both hold
            // it - the note in CharacterRecord explains why the overlap was kept - and
            // this is the one the spawn path applies, so a selector that read the other
            // could show a number the player never actually gets.
            summary.set_level(pCharacter->Level);

            // Set as a whole list, not appended: the generator emits set_X(vector) for
            // repeated fields, the same shape SaveCharacterRequest's inventory uses.
            Vector<server::CharacterSummary> characters;
            characters.push_back(summary);
            response.set_characters(characters);

            spdlog::info("{} has character '{}' ({})", acUsername,
                         pCharacter->Name.empty() ? "unnamed" : pCharacter->Name,
                         pCharacter->SpawnedBefore ? "played" : "never spawned");
        }
        else
        {
            spdlog::info("{} has no character yet", acUsername);
        }
    }

    server::RpcDefinitions definitions;

    const auto* pRpc = static_cast<RpcScriptInstance*>(IRpc::Get());
    pRpc->Serialize(definitions);

    if (!Send(aConnectionId, definitions))
        spdlog::error("Failed to send message to {:x}", aConnectionId);

    if (!Send(aConnectionId, response))
        spdlog::error("Failed to send message to {:x}", aConnectionId);

    // The player was accepted, rpc definitions are ready, we can create the player's handle
    const auto player = GetWorld()->get_mut<PlayerManager>()->Create(aConnectionId, acUsername.c_str());

    // Attach the identity Discord vouched for. Gameplay code reads permissions from here,
    // so every check is against something the server established - not something a client
    // claimed about itself.
    if (player)
    {
        if (auto* pComponent = player.get_mut<PlayerComponent>())
        {
            pComponent->DiscordId = acDiscordId;
            pComponent->Level = aLevel;
            pComponent->DiscordToken = acToken.c_str();
        }
    }
}

std::string DerivePlayerId(const std::string& acSnowflake)
{
    // FNV-1a, deliberately, rather than a real digest.
    //
    // This produces a DISPLAY number, not a secret. It exists so a Discord snowflake
    // never lands in a screenshot - not to resist attack, and there is nothing to gain
    // by reversing it since the server keys bans and permissions on the snowflake
    // itself. What it must be is byte-identical to the launcher's version, and a hash
    // that is ten lines in both languages is far easier to keep in step than one that
    // depends on a crypto library being in scope on both sides.
    const std::string input = "nightcity:" + acSnowflake;

    uint32_t hash = 2166136261u;   // FNV offset basis

    for (const unsigned char c : input)
    {
        hash ^= c;
        hash *= 16777619u;         // FNV prime; wraps at 32 bits, as intended
    }

    // Six digits reads like a player number rather than a hash.
    return std::to_string(hash % 900000u + 100000u);
}

// See the header. The token is read at the moment it is needed and dropped again; it is
// never stored in the config, never cached, and never logged.
std::string GameServer::GetBotToken() const
{
    // _dupenv_s rather than getenv: MSVC deprecates the latter, and a warning on every
    // build is a warning nobody reads by the third one.
#ifdef _WIN32
    {
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, "NCO_DISCORD_BOT_TOKEN") == 0 && value)
        {
            std::string token(value);
            free(value);
            if (!token.empty())
                return token;
        }
    }
#else
    if (const char* fromEnv = std::getenv("NCO_DISCORD_BOT_TOKEN"); fromEnv && *fromEnv)
        return fromEnv;
#endif

    if (m_config.Discord.BotTokenFile.empty())
        return {};

    std::ifstream file(m_config.Discord.BotTokenFile);
    if (!file)
        return {};

    // Same format tools\DiscordNotify.ps1 already uses: `token=` and `channel=` lines,
    // with `#` comments. Reading the first line raw - which is what this did first - picks
    // up a comment and produces a 401 that looks exactly like a revoked bot.
    //
    // A file holding nothing but the token on one line also works, because somebody will
    // eventually create one that way.
    auto trim = [](std::string value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return std::string{};

        const auto last = value.find_last_not_of(" \t\r\n");
        value = value.substr(first, last - first + 1);

        // A UTF-8 BOM from an editor is invisible and turns a valid token into a 401.
        if (value.size() >= 3 && static_cast<unsigned char>(value[0]) == 0xEF &&
            static_cast<unsigned char>(value[1]) == 0xBB && static_cast<unsigned char>(value[2]) == 0xBF)
            value.erase(0, 3);

        return value;
    };

    std::string bare;
    std::string line;

    while (std::getline(file, line))
    {
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#')
            continue;

        const auto equals = trimmed.find('=');
        if (equals == std::string::npos)
        {
            if (bare.empty())
                bare = trimmed;
            continue;
        }

        if (DiscordConfig::Lower(trim(trimmed.substr(0, equals))) == "token")
            return trim(trimmed.substr(equals + 1));
    }

    return bare;
}

std::map<std::string, std::string> GameServer::GetGuildRoleNames() const
{
    {
        std::lock_guard lock(m_roleNameMutex);
        if (std::chrono::steady_clock::now() < m_roleNamesExpire)
            return m_roleNames;
    }

    std::map<std::string, std::string> names;

    const auto token = GetBotToken();

    // Did we actually find out?
    //
    // Distinct from "are there any". A server with no bot token configured has genuinely
    // no role map and saying so is true; a server whose token was rejected has no idea,
    // and must not say the same thing. Only the second case is a lie worth guarding.
    bool conclusive = true;

    if (!token.empty() && !m_config.Discord.GuildId.empty())
    {
        httplib::Client roleClient("https://discord.com");
        roleClient.set_connection_timeout(5);
        roleClient.set_read_timeout(5);

        const httplib::Headers botHeaders{
            {"Authorization", "Bot " + token},
            {"User-Agent", "CyberpunkMP-Server (https://github.com/ofmiceandcam98-eng/CyberpunkMP, 1.0)"}};

        const auto roles = roleClient.Get("/api/v10/guilds/" + m_config.Discord.GuildId + "/roles", botHeaders);

        if (roles && roles->status == 200)
        {
            try
            {
                for (const auto& role : nlohmann::json::parse(roles->body))
                {
                    if (!role.contains("id") || !role.contains("name"))
                        continue;

                    names[role["id"].get<std::string>()] = DiscordConfig::Lower(role["name"].get<std::string>());
                }
            }
            catch (const std::exception& e)
            {
                spdlog::warn("Could not parse the guild role list: {}", e.what());
                conclusive = false;
            }
        }
        else if (roles)
        {
            // Deliberately prints no part of the token.
            spdlog::warn("Discord guild roles returned {} - role NAMES will not resolve, only ids", roles->status);
            conclusive = false;
        }
        else
        {
            // No response at all - timed out, DNS, or no route. Same ignorance as a 401.
            spdlog::warn("Could not reach Discord for the guild role list - role NAMES will not resolve");
            conclusive = false;
        }
    }

    // Cached even when empty, so a server with no bot token does not attempt this on every
    // single connection.
    {
        std::lock_guard lock(m_roleNameMutex);
        m_roleNames = names;
        m_roleNamesExpire = std::chrono::steady_clock::now() + kRoleNameTtl;
    }

    WriteRolesFile(names, conclusive);

    return names;
}

// Publishes what each role resolves to, for the launcher.
//
// Discord roles decided permissions in game and a hardcoded list of one Discord id decided
// them in the launcher, so somebody with the dev role had staff commands in the world and
// no server controls in the launcher that started it. Writing the resolved map out means
// both halves answer from the same table.
//
// Ids, names and levels only. Nothing here is a secret - every member of the Discord can
// already see who has which role.
void GameServer::WriteRolesFile(const std::map<std::string, std::string>& acRoleNames,
                               bool aConclusive) const
{
    if (m_config.Discord.RolesFile.empty())
        return;

    // Never overwrite a good map with ignorance.
    //
    // This file is the launcher's only source for who is staff. Writing an empty list
    // after a failed lookup does not merely fail to update it - it actively destroys the
    // last known-good answer and tells every launcher that nobody is an admin.
    //
    // It happened: the bot token started returning 401 on 16 August and the next publish
    // replaced two working mappings with "roles": []. It was caught before shipping only
    // because the file showed up as an unexpected diff.
    //
    // Keeping the previous file is strictly better. A stale role map is wrong only when
    // roles have actually changed; an empty one is wrong for everybody, immediately.
    if (!aConclusive)
    {
        spdlog::warn("Not publishing the role map - the guild lookup failed, so the "
                     "existing {} is left alone rather than replaced with an empty list",
                     m_config.Discord.RolesFile);
        return;
    }

    try
    {
        nlohmann::json roles = nlohmann::json::array();

        for (const auto& [id, name] : acRoleNames)
        {
            const auto level = m_config.Discord.ResolveLevel({}, {id}, acRoleNames);

            // Only the ones that grant something. A guild's full role list is mostly
            // colours and pings, and publishing it would bury the three that matter.
            if (level == EPermissionLevel::kPlayer)
                continue;

            roles.push_back({{"id", id}, {"name", name}, {"level", ToString(level)}});
        }

        // Formatted by hand. fmt's chrono support needs <fmt/chrono.h>, and without it
        // fmt::gmtime resolves to the CRT's and fails to compile in a way that reads like
        // a type error rather than a missing include.
        char stamp[32] = {};
        const std::time_t nowSeconds = std::time(nullptr);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &nowSeconds);
#else
        gmtime_r(&nowSeconds, &utc);
#endif
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &utc);

        nlohmann::json document;
        document["generatedAt"] = stamp;
        document["guildId"] = m_config.Discord.GuildId;
        document["owner"] = m_config.Discord.OwnerId;
        document["roles"] = roles;

        std::ofstream out(m_config.Discord.RolesFile);
        if (!out)
        {
            spdlog::warn("Could not open {} to publish the role map", m_config.Discord.RolesFile);
            return;
        }

        out << document.dump(2);

        spdlog::info("Published {} role mapping(s) to {}", roles.size(), m_config.Discord.RolesFile);
    }
    catch (const std::exception& e)
    {
        spdlog::warn("Could not write the role map: {}", e.what());
    }
}

GameServer::EDiscordAuthResult GameServer::VerifyDiscordToken(const std::string& acToken,
                                                              DiscordIdentity& aOutIdentity) const
{
    if (acToken.empty())
        return EDiscordAuthResult::kInvalidToken;

    const auto now = std::chrono::steady_clock::now();

    // Serve a recent answer rather than asking Discord again.
    {
        std::lock_guard lock(m_discordCacheMutex);

        // Drop anything expired while we are here, so the map cannot grow without
        // bound on a long-running server. Written as a loop rather than std::erase_if
        // because Map is tsl::hopscotch_map, which the standard overloads do not cover.
        for (auto it = m_discordCache.begin(); it != m_discordCache.end();)
        {
            if (it->second.Expires <= now)
                it = m_discordCache.erase(it);
            else
                ++it;
        }

        if (const auto it = m_discordCache.find(acToken); it != m_discordCache.end())
        {
            aOutIdentity = it->second.Identity;
            return EDiscordAuthResult::kOk;
        }
    }

    // A bearer token is all Discord needs to tell us who it belongs to. Note there is no
    // client secret anywhere in this flow - the launcher uses PKCE precisely because a
    // secret shipped to players is not a secret.
    httplib::Client client("https://discord.com");
    client.set_connection_timeout(5);
    client.set_read_timeout(5);

    const httplib::Headers headers{
        {"Authorization", "Bearer " + acToken},
        {"User-Agent", "CyberpunkMP-Server (https://github.com/ofmiceandcam98-eng/CyberpunkMP, 1.0)"}};

    // 1. Who is this?
    const auto me = client.Get("/api/v10/users/@me", headers);

    if (!me)
        return EDiscordAuthResult::kUnreachable;

    if (me->status == 401)
        return EDiscordAuthResult::kInvalidToken;

    // 429 is Discord telling us to slow down, not that this player is unwelcome.
    // Rejecting on it would mean a busy moment locks everyone out - and the busiest
    // moment is precisely when people are trying to join together.
    if (me->status == 429)
    {
        spdlog::warn("Discord rate-limited us (429). Falling back to any cached answer.");

        std::lock_guard lock(m_discordCacheMutex);
        if (const auto it = m_discordCache.find(acToken); it != m_discordCache.end())
        {
            // Deliberately ignoring expiry here: a slightly stale identity for someone
            // who verified minutes ago beats refusing a legitimate player. The
            // re-verification loop will correct it shortly either way.
            aOutIdentity = it->second.Identity;
            return EDiscordAuthResult::kOk;
        }

        return EDiscordAuthResult::kUnreachable;
    }

    if (me->status != 200)
    {
        spdlog::warn("Discord /users/@me returned {}", me->status);
        return EDiscordAuthResult::kUnreachable;
    }

    try
    {
        const auto body = nlohmann::json::parse(me->body);
        aOutIdentity.Id = body.value("id", "");

        // The HANDLE, not global_name.
        //
        // Handles are unique across Discord; display names are not, and anyone can
        // change theirs to match someone else's. For chat and moderation logs the
        // name has to identify one person unambiguously, so impersonation is not a
        // matter of editing a profile field.
        aOutIdentity.Username = body.value("username", "");
    }
    catch (const std::exception& e)
    {
        spdlog::error("Could not parse Discord identity: {}", e.what());
        return EDiscordAuthResult::kUnreachable;
    }

    if (aOutIdentity.Id.empty())
        return EDiscordAuthResult::kInvalidToken;

    if (m_config.Discord.GuildId.empty())
    {
        // No guild configured, so there are no roles to read. Owner id still applies.
        aOutIdentity.Level = m_config.Discord.ResolveLevel(aOutIdentity.Id, {});
        return EDiscordAuthResult::kOk;
    }

    // 2. Are they in our guild? This endpoint answers for the token's own user only, so it
    // needs no bot and no elevated permission - just the guilds.members.read scope.
    const auto member = client.Get("/api/v10/users/@me/guilds/" + m_config.Discord.GuildId + "/member", headers);

    if (!member)
        return EDiscordAuthResult::kUnreachable;

    if (member->status == 401)
        return EDiscordAuthResult::kInvalidToken;

    if (member->status == 429)
    {
        spdlog::warn("Discord rate-limited the membership check (429).");

        std::lock_guard lock(m_discordCacheMutex);
        if (const auto it = m_discordCache.find(acToken); it != m_discordCache.end())
        {
            aOutIdentity = it->second.Identity;
            return EDiscordAuthResult::kOk;
        }

        return EDiscordAuthResult::kUnreachable;
    }

    // 404 is the honest "not in that server" answer.
    if (member->status == 404)
    {
        // Only a hard failure when membership is actually required.
        if (m_config.Discord.RequireMembership)
            return EDiscordAuthResult::kNotAMember;

        aOutIdentity.Level = m_config.Discord.ResolveLevel(aOutIdentity.Id, {});
        return EDiscordAuthResult::kOk;
    }

    if (member->status != 200)
    {
        spdlog::warn("Discord guild member check returned {}", member->status);
        return EDiscordAuthResult::kUnreachable;
    }

    // The member object carries the player's role ids. This is the authoritative
    // source for permissions - it comes from Discord, over TLS, in response to a
    // token the player could not have forged. Anything the client said about
    // itself was discarded long before this point.
    try
    {
        const auto body = nlohmann::json::parse(member->body);

        if (body.contains("roles") && body["roles"].is_array())
        {
            for (const auto& role : body["roles"])
            {
                if (role.is_string())
                    aOutIdentity.RoleIds.push_back(role.get<std::string>());
            }
        }
    }
    catch (const std::exception& e)
    {
        // We know they are a member; we just could not read their roles. Let them
        // in as a plain player rather than refusing - failing to parse a role list
        // should not lock someone out of the server.
        spdlog::warn("Could not parse Discord roles for {}: {}", aOutIdentity.Username, e.what());
    }

    const auto roleNames = GetGuildRoleNames();

    aOutIdentity.Level = m_config.Discord.ResolveLevel(aOutIdentity.Id, aOutIdentity.RoleIds, roleNames);

    // Say what was seen and what it resolved to.
    //
    // Permissions that silently do nothing are the worst kind: the role exists in Discord,
    // it looks right, and the only evidence that it never reached the server is a player
    // being told "you do not have permission" for something they should be able to do.
    // Naming the roles here turns that into one line of log - and where no name is known,
    // printing the raw id means the manual config route is a copy and paste.
    if (!aOutIdentity.RoleIds.empty())
    {
        std::string described;
        for (const auto& roleId : aOutIdentity.RoleIds)
        {
            if (!described.empty())
                described += ", ";

            const auto named = roleNames.find(roleId);
            described += (named != roleNames.end()) ? named->second : ("id:" + roleId);
        }

        spdlog::info("{} has Discord role(s) [{}] -> {}", aOutIdentity.Username, described,
                     ToString(aOutIdentity.Level));
    }

    // Remember this, so a reconnect or the re-verification sweep does not spend two
    // more API calls on an answer we already have.
    //
    // ONLY successful verifications are cached. Caching a rejection would let a
    // transient failure lock someone out for minutes, and caching "not a member"
    // would mean joining the Discord did not take effect until it expired.
    {
        std::lock_guard lock(m_discordCacheMutex);
        m_discordCache[acToken] = CachedIdentity{aOutIdentity, std::chrono::steady_clock::now() + kDiscordCacheTtl};
    }

    return EDiscordAuthResult::kOk;
}

ScratchAllocator& GameServer::GetScratch()
{
    thread_local ScratchAllocator s_allocator{1 << 19};
    return s_allocator;
}
