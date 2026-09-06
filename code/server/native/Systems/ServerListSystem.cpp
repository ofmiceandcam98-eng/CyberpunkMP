#include "ServerListSystem.h"

#include "GameServer.h"
#include "Components/PlayerComponent.h"
#include "PlayerManager.h"

/*
 * The endpoint is CONFIGURATION now, and it defaults to empty - see Config::ServerListEndpoint.
 *
 * It was hardcoded to https://cyberpunk.skyrim-together.com, upstream Tilted Phoques' master
 * server. VERIFIED 2026-09-06: that subdomain has NO DNS RECORD, from the NAS and from inside
 * the server container; the parent skyrim-together.com still resolves, so it was retired
 * rather than broken. Every server had been announcing into nothing once a minute and logging
 * "Server could not reach the server list!" each time - on the live box for hours, and one
 * second after boot on a clean rebuild of the test box, which is what proved it systemic
 * rather than a stale binary.
 *
 * Discovery does not depend on it: publish/server.json is what every launcher fetches from
 * releases/latest. So the announce is off unless somebody points it at a list that exists.
 */

ServerListSystem::ServerListSystem(gsl::not_null<World*> apWorld)
    : m_pWorld(apWorld)
    , m_nextAnnounce{}
{
    // Do NOT release this iterator - see WorldClock.cpp. fini() segfaulted the server.
    m_updateSystem = apWorld->system("Server list Update")
                         .kind(flecs::OnUpdate)
                         .run(
                             [this](flecs::iter& aIt)
                             {
                                 Tick();
                             });

    m_serverListObserver = apWorld->observer<PlayerComponent>("Server list player Observer")
                               .event(flecs::OnSet)
                               .event(flecs::OnRemove)
                               .each([this](flecs::iter& it, size_t i, PlayerComponent& component) { m_nextAnnounce = {}; });

    m_updateSystem.child_of(apWorld->entity("systems"));
    m_serverListObserver.child_of(apWorld->entity("observers"));
}

void ServerListSystem::Tick() noexcept
{
    if (m_nextAnnounce < std::chrono::steady_clock::now())
    {
        Announce();

        m_nextAnnounce = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    }
}

void ServerListSystem::Announce() noexcept
{
    // A list that refused us stays refused until the operator changes the config. Retrying a
    // 403 once a minute is how you turn a refusal into a rate-limit ban.
    if (m_refused)
        return;

    const auto& config = GServer->GetConfig();
    const std::string endpoint = config->ServerListEndpoint;

    // No endpoint configured: announce nothing. Checked BEFORE the thread is spawned, so a
    // server with this off does not detach a thread a minute to do nothing, and says so once
    // instead of erroring forever.
    if (endpoint.empty())
    {
        if (!m_announcedDisabled)
        {
            m_announcedDisabled = true;
            spdlog::info("Server list: no ServerListEndpoint configured - not announcing. "
                         "Players find this server through publish/server.json, not a public list.");
        }

        return;
    }

    std::thread(
        [this, endpoint]()
        {
            const auto& cfg = GServer->GetConfig();
            auto pc = static_cast<uint16_t>(m_pWorld->get<PlayerManager>()->Count());
            PostAnnouncement(endpoint, cfg->Name, cfg->Description, cfg->IconUrl, GServer->GetPort(), GServer->GetTickRate(), pc, 10000, cfg->Tags, true, false, 0);
        })
        .detach();
}

void ServerListSystem::PostAnnouncement(
    const std::string& acEndpoint, const std::string& acName, const std::string& acDesc, const std::string& acIconUrl, uint16_t aPort, uint16_t aTick, uint16_t aPlayerCount, uint16_t aPlayerMaxCount,
    const std::string& acTagList, bool aPublic, bool aPassword, int32 aFlags) noexcept
{
    const std::string kVersion{"v0.1"};
    const httplib::Params params{
        {"name", acName},
        {"desc", acDesc},
        {"icon_url", acIconUrl},
        {"version", kVersion},
        {"port", std::to_string(aPort)},
        {"tick", std::to_string(aTick)},
        {"player_count", std::to_string(aPlayerCount)},
        {"max_player_count", std::to_string(aPlayerMaxCount)},
        {"tags", acTagList},
        {"public", aPublic ? "true" : "false"},
        {"pass", aPassword ? "true" : "false"},
        {"flags", std::to_string(aFlags)},
    };

    httplib::Client client(acEndpoint);
    client.enable_server_certificate_verification(false);
    client.set_read_timeout(std::chrono::milliseconds(30000));
    const auto response = client.Post("/announce", params);

    /*
     * A 403 USED TO KILL THE SERVER. It does not any more, and that is the point of this
     * change.
     *
     * The old code called GServer->Kill() on 403 - upstream's "we banned this server". With
     * the endpoint hardcoded to their master server, that was a REMOTE KILL SWITCH over every
     * server this fork runs, held by a project we forked away from. It was inert only because
     * the DNS record is gone; a re-pointed, re-registered or squatted subdomain would have
     * shut down every deployment at once, and during a hardware migration that would have
     * looked like the migration failing.
     *
     * A list refusing us is a reason to STOP ANNOUNCING to that list. It is not a reason to
     * disconnect the people currently playing. So it is logged loudly and announcing stops
     * until the operator changes the config.
     */
    if (response)
    {
        if (response->status == 403)
        {
            m_refused = true;
            spdlog::error("Server list at {} REFUSED this server (403). Announcing is now off. "
                          "Players are unaffected - this only removes us from that public list.",
                          acEndpoint);
        }
        else if (response->status != 200)
        {
            spdlog::error("Server list error! {}", response->body);
        }
    }
    else
    {
        spdlog::error("Server could not reach the server list at {}! {}", acEndpoint, to_string(response.error()));
    }
}
