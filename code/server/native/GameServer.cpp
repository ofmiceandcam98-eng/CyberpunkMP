#include "GameServer.h"

#include <Components/PlayerComponent.h>

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

    m_pWorld->Update(std::chrono::duration_cast<std::chrono::duration<float>>(delta).count());
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

    auto* pPlayerManager = GetWorld()->get_mut<PlayerManager>();

    if (const auto player = pPlayerManager->GetByConnectionId(aConnectionId))
    {
        pPlayerManager->Remove(player);

        if (auto* pPlayerComponent = player.get<PlayerComponent>())
        {
            GetWorld()->get_mut<Level>()->Remove(pPlayerComponent->Puppet);
        }

        player.destruct();

        spdlog::info("{}/{} player(s) online", pPlayerManager->Count(), m_config.MaxPlayer);
    }
}

void GameServer::HandleAuthentication(const PacketEvent<client::AuthenticationRequest>& aRequest)
{
    server::AuthenticationResponse response;

    if (aRequest.get_client_protocol() != client::kIdentifier)
    {
        response.set_success(false);
        response.set_error("Invalid protocol version!");

        spdlog::warn("Connection attempt with client identifier {:x}, expected {:x}", aRequest.get_client_protocol(), client::kIdentifier);
        Send(aRequest.ConnectionId, response);
        Kick(aRequest.ConnectionId);
        return;
    }

    if (aRequest.get_server_protocol() != server::kIdentifier)
    {
        response.set_success(false);
        response.set_error("Invalid protocol version!");

        spdlog::warn("Connection attempt with server identifier {:x}, expected {:x}", aRequest.get_client_protocol(), server::kIdentifier);
        Send(aRequest.ConnectionId, response);
        Kick(aRequest.ConnectionId);
        return;
    }

    // Identity is decided here, by asking Discord - not by believing the client.
    // Note: the project's String has a custom allocator, so cross to std::string via c_str()
    // at the boundary with httplib rather than assuming they are interchangeable.
    std::string username = aRequest.get_username().c_str();
    std::string discordId;
    auto level = EPermissionLevel::kPlayer;

    if (m_config.Discord.Enabled)
    {
        DiscordIdentity identity;
        const auto result = VerifyDiscordToken(aRequest.get_token().c_str(), identity);

        if (result != EDiscordAuthResult::kOk)
        {
            const char* reason = "Discord sign-in required.";

            switch (result)
            {
            case EDiscordAuthResult::kInvalidToken:
                reason = "Your Discord sign-in has expired. Open the launcher and sign in again.";
                break;
            case EDiscordAuthResult::kNotAMember:
                reason = "You must be a member of the Night City Online Discord to play here.";
                break;
            case EDiscordAuthResult::kUnreachable:
                // Fail closed. Membership is required, so an unverifiable token is not a
                // token we can accept - letting people in during an outage would mean
                // anyone could join by simply making Discord unreachable.
                reason = "Cannot reach Discord to verify your account right now. Try again shortly.";
                break;
            default:
                break;
            }

            spdlog::warn("Rejected connection {:x}: {}", aRequest.ConnectionId, reason);

            response.set_success(false);
            response.set_error(reason);
            Send(aRequest.ConnectionId, response);
            Kick(aRequest.ConnectionId);
            return;
        }

        // Banned? Checked here, after Discord has told us who they actually are, so the
        // ban follows the account rather than a name or address they can change.
        if (const auto* pBan = m_bans.Find(identity.Id))
        {
            spdlog::info("Rejected banned player {} ({}): {}", identity.Username, identity.Id,
                         pBan->Reason);

            response.set_success(false);
            response.set_error(pBan->Reason.empty()
                                   ? String("You are banned from this server.")
                                   : String(("You are banned from this server: " + pBan->Reason).c_str()));
            Send(aRequest.ConnectionId, response);
            Kick(aRequest.ConnectionId);
            return;
        }

        username = identity.Username;
        discordId = identity.Id;
        level = identity.Level;

        // Both ids together, on purpose. Players see only the derived number, so a
        // report saying "232998 is griefing" is meaningless unless the log ties it
        // back to an account you can actually moderate.
        spdlog::info("Authorised {} [player {}] (discord {}) as {} on connection {:x}", username,
                     DerivePlayerId(discordId), discordId, ToString(level), aRequest.ConnectionId);
    }
    else
    {
        spdlog::info("Authorize connection from {} (Discord verification disabled)", username);
    }

    response.set_success(true);

    server::Settings settings;
    settings.set_update_rate(m_config.UpdateRate);
    response.set_settings(settings);

    server::RpcDefinitions definitions;

    const auto* pRpc = static_cast<RpcScriptInstance*>(IRpc::Get());
    pRpc->Serialize(definitions);

    if (!Send(aRequest.ConnectionId, definitions))
        spdlog::error("Failed to send message to {:x}", aRequest.ConnectionId);

    if (!Send(aRequest.ConnectionId, response))
        spdlog::error("Failed to send message to {:x}", aRequest.ConnectionId);

    // The player was accepted, rpc definitions are ready, we can create the player's handle
    const auto player = GetWorld()->get_mut<PlayerManager>()->Create(aRequest.ConnectionId, username.c_str());

    // Attach the identity Discord vouched for. Gameplay code reads permissions from here,
    // so every check is against something the server established - not something a client
    // claimed about itself.
    if (player)
    {
        if (auto* pComponent = player.get_mut<PlayerComponent>())
        {
            pComponent->DiscordId = discordId;
            pComponent->Level = level;
            pComponent->DiscordToken = aRequest.get_token().c_str();
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

    aOutIdentity.Level = m_config.Discord.ResolveLevel(aOutIdentity.Id, aOutIdentity.RoleIds);

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
