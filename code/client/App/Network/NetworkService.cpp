#include "NetworkService.h"

#include "App/Settings.h"
#include "App/World/NetworkWorldSystem.h"
#include "Game/Utils.h"
#include "RED4ext/Scripting/Natives/Generated/Vector4.hpp"
#include "RED4ext/Scripting/Natives/Generated/game/Object.hpp"
#include "App/World/AppearanceSystem.h"
#include "Game/CharacterCustomizationSystem.h"

NetworkService::NetworkService()
    : Client(client::kIdentifier, server::kIdentifier)
    , m_lastUpdate(std::chrono::steady_clock::now())
{
    BindMessageHandlers();
}

NetworkService::~NetworkService()
{
}

void NetworkService::BindMessageHandlers()
{
    GetSink<server::AuthenticationResponse>().connect<&NetworkService::HandleAuthentication>(this);
}

void NetworkService::OnConsume(const void* apData, uint32_t aSize)
{
    ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    if(!server::Deserializer::Process(reader, 0, m_dispatcher))
    {
        spdlog::error("Failed to deserialize a message from the server.");
    }
}

void NetworkService::OnConnected()
{
    spdlog::info("Connected to server.");

    client::AuthenticationRequest request;

    // The token decides who the server thinks we are. Username is only a hint for servers
    // with Discord verification switched off - where it IS on, the server overwrites this
    // with whatever Discord says, because a client naming itself proves nothing.
    const auto& token = Settings::Get().discordToken;
    request.set_token(token.empty() ? "test" : token.c_str());

    const auto& name = Settings::Get().discordName;
    request.set_username(name.empty() ? "Player" : name.c_str());
    request.set_client_protocol(client::kIdentifier);
    request.set_server_protocol(server::kIdentifier);

    Send(request);
}

void NetworkService::OnDisconnected(EDisconnectReason aReason)
{
    spdlog::info("Disconnected from server {}", static_cast<uint32_t>(aReason));
    Red::GetGameSystem<NetworkWorldSystem>()->OnDisconnected(aReason);

    m_authenticated = false;
}

void NetworkService::OnUpdate()
{
    m_dispatcher.update();

    ReportConnectionHealth();

    Red::GetGameSystem<NetworkWorldSystem>()->Update(GetClock().GetCurrentTick());
}

void NetworkService::ReportConnectionHealth()
{
    if (!IsConnected())
        return;

    const auto now = std::chrono::steady_clock::now();

    const auto status = GetConnectionStatus();

    // GameNetworkingSockets already measures all of this - it is what the clock uses to
    // stay in sync. None of it was ever surfaced, so a laggy or degrading link looked
    // identical to a healthy one right up until it dropped.
    //
    // Quality is 0..1 as seen from each end; a value below 1 means packets are being lost.
    // A negative quality means "not measured yet", not "100% loss". Comparing it
    // against a threshold made a fresh, healthy connection report DEGRADED every
    // two seconds for the first few seconds of every session.
    const bool localKnown = status.m_flConnectionQualityLocal >= 0.f;
    const bool remoteKnown = status.m_flConnectionQualityRemote >= 0.f;

    const bool degraded = status.m_nPing > 200 ||
                          (localKnown && status.m_flConnectionQualityLocal < 0.95f) ||
                          (remoteKnown && status.m_flConnectionQualityRemote < 0.95f);

    // Report on a slow heartbeat normally, but immediately when something looks wrong and
    // it has not already been reported recently.
    const auto sinceLast = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastHealthReport).count();

    if (sinceLast < (degraded ? 2 : 15))
        return;

    m_lastHealthReport = now;

    // The "not measured yet" sentinel has to be kept out of the TEXT as well, not just out
    // of the degraded test above. Multiplying -1 by 100 printed "quality -100%/-100%" on
    // every healthy fresh connection, which reads like total packet loss and sent me
    // hunting a network fault that did not exist. An unknown value is reported as unknown.
    const auto describe = [](bool aKnown, float aQuality)
    {
        if (!aKnown)
            return std::string("n/a");

        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%.0f%%", aQuality * 100.f);
        return std::string(buffer);
    };

    const auto local = describe(localKnown, status.m_flConnectionQualityLocal);
    const auto remote = describe(remoteKnown, status.m_flConnectionQualityRemote);

    if (degraded)
    {
        spdlog::warn("[Link] DEGRADED - ping {}ms, quality {}/{} (local/remote), "
                     "{} unacked reliable bytes, {}ms queued",
                     status.m_nPing,
                     local,
                     remote,
                     status.m_cbSentUnackedReliable,
                     status.m_usecQueueTime / 1000);
    }
    else
    {
        spdlog::info("[Link] ok - ping {}ms, quality {}/{}, in {:.0f}/s out {:.0f}/s",
                     status.m_nPing,
                     local,
                     remote,
                     status.m_flInPacketsPerSec,
                     status.m_flOutPacketsPerSec);
    }
}

void NetworkService::OnGameUpdate(RED4ext::CGameApplication* apApp)
{
    Update();
}

void NetworkService::HandleAuthentication(const PacketEvent<server::AuthenticationResponse>& aResponse)
{
    if (!aResponse.get_success())
    {
        spdlog::error("Authentication failed: {}", aResponse.get_error());
        Close();
        return;
    }

    m_settings = aResponse.get_settings();

    // What the SERVER says this account is playing, kept before anything else runs.
    //
    // The character selector is drawn from this. It has to be stored rather than acted on
    // here, because authentication can now happen at the main menu - where there is no
    // world, no player, and nothing to spawn.
    // A list, of length zero or one today. Taking the first rather than assuming one
    // exists - empty is a real answer and means "no character", which is the state the
    // selector has to offer CREATE from.
    auto* pWorldSystem = Red::GetGameSystem<NetworkWorldSystem>();
    const auto& characters = aResponse.get_characters();

    if (characters.empty())
    {
        pWorldSystem->SetCharacterStatus(false, "", 0, false);
    }
    else
    {
        const auto& first = characters[0];
        pWorldSystem->SetCharacterStatus(true, first.get_name().c_str(), first.get_level(),
                                         first.get_spawned_before());
    }

    pWorldSystem->OnConnected();

    SendSpawnCharacterRequest();
}

void NetworkService::SendSpawnCharacterRequest()
{
    client::SpawnCharacterRequest request;
    request.set_is_player(true);

    const auto system = Red::GetGameSystem<Game::PlayerSystem>();
    Red::Handle<Red::GameObject> player;

    if (system)
        system->GetLocalPlayerControlledGameObject(player);

    // No player means we are not in the world yet - authenticated from the main menu,
    // standing on the character selector.
    //
    // This used to be impossible: connecting only ever happened from the in-world HUD
    // controller, so a player object was always there. Now that MULTIPLAYER can connect
    // before loading anything, spawning here would build a request out of a null player -
    // and the position, equipment and appearance would all be read from nothing.
    //
    // Remembered rather than dropped. The spawn is what puts this player in front of
    // everyone else, so it has to happen once the world is up; EnterWorld() is what asks
    // for it, and it is called when the world is ready and PLAY has been pressed.
    if (!player)
    {
        spdlog::info("[Spawn] authenticated with no world loaded - holding the spawn until PLAY");
        m_spawnDeferred = true;
        return;
    }

    m_spawnDeferred = false;

    // Use worldTransform: localTransform is relative and stays near-origin, which made
    // the server spawn our puppet at ~(0, 3.6, 0) instead of our actual world position.
    const auto& cEntityPosition = player->placedComponent->worldTransform.Position;
    const auto cEntityRotation = Game::ToGlm(player->placedComponent->worldTransform.Orientation);

    common::Vector3 pos;
    pos.set_x(cEntityPosition.x);
    pos.set_y(cEntityPosition.y);
    pos.set_z(cEntityPosition.z);

    request.set_position(pos);
    request.set_rotation(cEntityRotation.z);
    request.set_cookie(0);

    auto appSystem = Red::GetGameSystem<NetworkWorldSystem>()->GetAppearanceSystem();
    request.set_equipment(appSystem->GetPlayerItems(player));


    auto ccSystem = Red::GetGameSystem<Red::game::ui::CharacterCustomizationSystem>();

    // GetCustomizationState() returns (ccSystem + 0x78), which can never be null - the
    // instance behind it can be, and is during normal gameplay. Serializing a null
    // instance crashes the game, so check the instance itself.
    auto stateHandle = GetCustomizationState(ccSystem);

    if (stateHandle && stateHandle->instance)
    {
        auto writer = CMPWriter();
        CharacterCustomizationState_Serialize(stateHandle->instance, &writer);
        spdlog::info("Got bytes: {}", writer.bytes.size());
        request.set_ccstate(writer.bytes);
    }
    else
    {
        spdlog::info("CustomizationState was null");
    }

    Send(request);
}

ScratchAllocator& NetworkService::GetScratch()
{
    thread_local ScratchAllocator s_allocator{1 << 19};
    return s_allocator;
}
