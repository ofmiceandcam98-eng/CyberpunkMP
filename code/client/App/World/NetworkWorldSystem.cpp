#include "NetworkWorldSystem.h"

// For routing flecs's own allocations onto our allocator - see OnInitialize.
#include <mimalloc.h>

#include <App/Settings.h>

#include "App/Network/NetworkService.h"
#include "RED4ext/Scripting/Natives/Generated/game/Puppet.hpp"
#include "RED4ext/Scripting/Natives/Generated/vehicle/BaseObject.hpp"
#include "RED4ext/Scripting/Natives/gameIEntityStubSystem.hpp"
#include "RED4ext/Scripting/Natives/Generated/game/EntityStubComponentPS.hpp"
#include <RED4ext/Scripting/Natives/Generated/game/mounting/MountingFacility.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/mounting/MountingInfo.hpp>
#include <RED4ext/Scripting/Natives/Generated/vehicle/MoveSystem.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/TimeSystem.hpp>
#include <RED4ext/Scripting/Natives/Generated/world/WeatherScriptInterface.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/BlackboardSystem.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/bb/AllScriptDefinitions.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/bb/ScriptDefinition.hpp>

#include "App/Components/EntityComponent.h"
#include "App/Components/SpawningComponent.h"
#include "App/Components/InterpolationComponent.h"
#include "App/Components/DriverComponent.h"
#include "App/World/PuppetRegistry.h"
#include "Game/Utils.h"
#include "Game/CharacterCustomizationSystem.h"

#include "ChatSystem.h"

static uint64_t GTick = 0;

uint64_t NetworkWorldSystem::GetTick()
{
    return GTick;
}

NetworkWorldSystem::NetworkWorldSystem()
{
    set_entity_range(10'000'000, 20'000'000);
}

bool NetworkWorldSystem::Spawn(uint64_t aServerId, const Red::Vector4& aPosition, const Red::Quaternion& aRotation, const Red::DynArray<Red::TweakDBID>& aEquipment, const Vector<uint8_t> aCcstate, bool aIsMale, const std::string& acUsername, const std::string& acRecord)
{
    // Not ready is a WHEN problem, not a whether problem. The server front-loads
    // everyone already online the moment we join, which can beat the world attach -
    // dropping those spawns meant whoever was online first simply never existed for
    // the person loading in. Queue them; OnWorldAttached replays the queue.
    if (!m_ready)
    {
        spdlog::info("[Spawn] remote id {} arrived before the world was ready - queued", aServerId);
        m_pendingSpawns.push_back({aServerId, aPosition, aRotation, aEquipment, aCcstate, aIsMale, acUsername, acRecord});
        return true;
    }

    // A spawn for an id we already track replaces the old puppet instead of standing a
    // second copy next to it. Reconnects re-announce, and a missed unload in between
    // used to leave a frozen duplicate behind forever.
    if (GetEntityByServerId(aServerId))
    {
        spdlog::info("[Spawn] remote id {} already has a puppet - replacing it", aServerId);
        DeSpawn(aServerId);
    }

    const auto handle = Red::GetGameSystem<NetworkWorldSystem>();
    Red::EntityID id;
    Red::ScriptGameInstance game;

    spdlog::info("[Spawn] remote id {} - ccstate {} bytes, {} equipment item(s)", aServerId, aCcstate.size(),
                 aEquipment.size);

    if (!m_pCreatePuppet)
    {
        spdlog::error("[Spawn] CreatePuppet script function was never resolved - aborting");
        return false;
    }

    // Body gender is SERVER-GRANTED, carried on the spawn message next to the name.
    // It used to be re-derived here by parsing the appearance blob, with a silent
    // fall-back to male - and a parse hiccup spawned a female character as the male
    // mannequin (live, 2026-08-21). The blob still travels for the appearance system;
    // it just no longer gets a vote on which body to build.
    const bool isBodyGenderMale = aIsMale;

    if (aCcstate.empty() && acRecord.empty())
    {
        spdlog::warn("[Spawn] remote player sent no ccstate - spawning with default appearance");
    }

    // A server-declared NPC names its exact record; players get the configured puppet
    // record for their body gender.
    const std::string record = !acRecord.empty()
        ? acRecord
        : std::string((isBodyGenderMale ? Settings::Get().puppetRecordMale : Settings::Get().puppetRecordFemale).c_str());

    if (!Red::Detail::CallFunctionWithArgs(m_pCreatePuppet, handle, id, aPosition, aRotation, isBodyGenderMale,
                                           Red::CString(record.c_str())))
    {
        spdlog::error("[Spawn] CreatePuppet call failed for remote id {}", aServerId);
        return false;
    }

    auto apprSystem = Red::GetGameSystem<NetworkWorldSystem>()->GetAppearanceSystem();
    apprSystem->AddEntity(id, aEquipment, aCcstate);

    // Recorded BEFORE ApplyAppearance runs, since that is what reads it back to set the
    // nameplate. Doing it afterwards leaves the puppet named after whatever record it was
    // built from.
    apprSystem->SetEntityName(id, acUsername);

    if (!id.IsDynamic())
    {
        spdlog::warn("[Spawn] entity id is not dynamic - bailing out for remote id {}", aServerId);
        return false;
    }

    // Player-record puppets never enter the NPC idle-controller pipeline the legacy
    // path hooks - they are driven by the mod-owned PuppetDriver instead. The flag
    // routes EVERYTHING through the driver for A/B testing the old path's retirement.
    const bool usesDriver = Settings::Get().puppetDriverAll ||
                            record.rfind("Character.Player_Puppet", 0) == 0;

    // Register BEFORE the entity finishes assembling. The animation-thread hook
    // identifies our puppets through this registry instead of reading the entity's
    // tag array off-thread, which was racing against the main thread's setup. Driver
    // puppets go in BOTH tables - the second silences the hook for them entirely.
    App::PuppetRegistry::Add(id.hash);
    if (usesDriver)
        App::PuppetRegistry::AddDriver(id.hash);

    auto spawned = make_alive(aServerId);

    spawned.emplace<SpawningComponent>(id, nullptr, usesDriver);

    // Give the puppet somewhere to put movement immediately.
    //
    // InterpolationComponent is normally added by an observer on EntityComponent, which
    // does not exist until the promotion poll runs up to 200ms later. Movement arriving
    // before that had nowhere to go - it used to crash on a null component, and merely
    // guarding that would instead silently discard the first fifth of a second of a
    // player's movement and make them snap on arrival. Adding it here means their very
    // first update is buffered.
    spawned.add<InterpolationComponent>();

    return true;
}

void NetworkWorldSystem::DeSpawn(uint64_t aServerId) const
{
    // An unload for a spawn still waiting in the queue cancels the queue entry - the
    // player left again before our world even finished loading.
    std::erase_if(const_cast<NetworkWorldSystem*>(this)->m_pendingSpawns,
                  [aServerId](const PendingSpawn& s) { return s.ServerId == aServerId; });

    const auto entity = GetEntityByServerId(aServerId);

    if (!entity)
        return;

    if (auto* pEntity = entity.get<EntityComponent>())
    {
        App::PuppetRegistry::Remove(pEntity->Id.hash);
        App::PuppetRegistry::RemoveDriver(pEntity->Id.hash);
        const auto handle = Red::GetGameSystem<NetworkWorldSystem>();
        Red::Detail::CallFunctionWithArgs(m_pDeletePuppet, handle, pEntity->Id);
    }
    else if (auto* pEntity = entity.get<SpawningComponent>())
    {
        App::PuppetRegistry::Remove(pEntity->Id.hash);
        App::PuppetRegistry::RemoveDriver(pEntity->Id.hash);
        const auto handle = Red::GetGameSystem<NetworkWorldSystem>();
        Red::Detail::CallFunctionWithArgs(m_pDeletePuppet, handle, pEntity->Id);
    }

    entity.destruct();
}

Red::Handle<Red::Entity> NetworkWorldSystem::GetEntity(Red::EntityID aId) const
{
    Red::Handle<Red::IGameSystem> dynamicEntitySystem;
    if (!Red::CallStatic("ScriptGameInstance", "GetDynamicEntitySystem", dynamicEntitySystem))
        return nullptr;

    Red::Handle<Red::Entity> entity;
    if (!Red::CallVirtual(dynamicEntitySystem, "GetEntity", entity, aId))
        return nullptr;

    return entity;
}

flecs::entity NetworkWorldSystem::GetEntityByServerId(uint64_t aServerId) const
{
    return flecs::entity(*this, aServerId);
}

Red::EntityID NetworkWorldSystem::GetEntityIdByServerId(uint64_t aServerId) const
{
    const auto entity = GetEntityByServerId(aServerId);
    if (!entity)
        return 0;

    if (auto* pEntity = entity.get<EntityComponent>())
        return pEntity->Id;

    if (auto* pEntity = entity.get<SpawningComponent>())
        return pEntity->Id;

    return 0;
}

flecs::entity NetworkWorldSystem::FindEntity(Red::EntityID aId) const
{
    auto entity = query<EntityComponent>().find(
        [aId](const EntityComponent& component)
        {
            return component.Id == aId;
        });

    if (!entity)
    {
        entity = query<SpawningComponent>().find(
            [aId](const SpawningComponent& component)
            {
                return component.Id == aId;
            });
    }

    return entity;
}

void NetworkWorldSystem::Update(uint64_t aTick)
{
    GTick = aTick;

    // Send whatever the microphone produced since the last frame.
    //
    // HERE, on the game thread, and not from the capture thread that encoded it. The
    // transport is not documented as safe to call from an arbitrary thread, and a voice
    // feature that corrupts the connection would cost the whole session rather than just
    // sounding bad. The queue between the two is what makes that safe - see VoiceClient.h.
    if (m_voiceClient.IsRunning())
    {
        // Say what voice is actually doing, every ten seconds.
        //
        // "Voice does not work" was diagnosed last time by reading a WASAPI error that
        // happened to be logged; there was no way to tell whether a single frame had ever
        // been encoded, sent, or received. These four numbers answer that outright - see
        // VoiceStats for what each zero means.
        if (++m_voiceStatsTicks % 600 == 0)
        {
            const auto stats = m_voiceClient.GetStats();

            spdlog::info("[Voice] mic {} / speakers {} - encoded {}, sent {}, received {}, decoded {}",
                         stats.CaptureAlive ? "ok" : "NOT CAPTURING", stats.PlaybackAlive ? "ok" : "NOT PLAYING",
                         stats.Encoded, m_voiceSequence, stats.Received, stats.Decoded);
        }

        auto frames = m_voiceClient.TakeOutgoing();

        if (!frames.empty())
        {
            const auto pNetworkService = Core::Container::Get<NetworkService>();

            if (pNetworkService && pNetworkService->IsConnected())
            {
                const auto range = m_voiceClient.GetRange();

                for (auto& frame : frames)
                {
                    client::VoiceFrameRequest request;

                    // The generator spells "bytes" as a vector with its own allocator.
                    // Taken from the setter itself rather than written out, so a change to
                    // how the protocol represents bytes does not silently break here.
                    using VoiceBytes = std::decay_t<decltype(request.get_data())>;

                    request.set_data(VoiceBytes(frame.begin(), frame.end()));
                    request.set_sequence(m_voiceSequence++);
                    request.set_range(range);

                    // Unreliable, and declared so on the MESSAGE - VoiceFrameRequest carries
                    // the `unreliable` tag field, which is how this protocol says it (see
                    // MoveEntityRequest). Send() reads that trait and picks the channel, so
                    // passing one here would not compile.
                    //
                    // A voice frame that arrives late is worse than one that never arrives:
                    // retransmitting pushes every frame behind it later still, heard as a
                    // delay that grows and never recovers.
                    pNetworkService->Send(request);
                }
            }
        }
    }

    // Apply the server's possessions as soon as there is somebody to give them to.
    //
    // Retried from the update loop rather than fired once on the spawn response, because
    // that response beats the player's own puppet into existence. A single attempt there
    // found no player and returned, and the only symptom was possessions never arriving.
    if (m_restorePending)
    {
        const auto system = Red::GetGameSystem<Game::PlayerSystem>();
        Red::Handle<Red::GameObject> player;

        if (system)
            system->GetLocalPlayerControlledGameObject(player);

        // Says what it is waiting for, once a second, instead of retrying in silence.
        //
        // The previous version returned quietly on every tick when the player was not
        // there yet - so a retry that never succeeded was indistinguishable from one that
        // was never armed. That is the fourth time tonight a silent path has cost an hour.
        if (++m_restoreTicks % 60 == 0)
        {
            spdlog::info("[Inventory] still waiting to restore - player system {}, player {}",
                         system ? "ready" : "MISSING", player ? "ready" : "not yet");
        }

        // A player handle is not the same as a world that will accept a script call.
        //
        // The first version fired the moment the handle existed - the same millisecond as
        // the spawn response - and the call returned success while the script never ran.
        // No bail message either, because RestorePossessions was never entered. The proof
        // it is timing rather than mechanism: the identical CallVirtual to CaptureInventory
        // works fine eighteen seconds later, from a network handler.
        //
        // So the handle starts a countdown rather than firing. Three seconds at 60 ticks,
        // which is far longer than needed and costs nothing - the alternative is a restore
        // that silently does not happen, and that has now cost two test rounds.
        if (player)
        {
            if (m_restoreReadyAt == 0)
            {
                m_restoreReadyAt = m_restoreTicks;
                spdlog::info("[Inventory] player exists - letting the world settle before applying");
            }
            else if (m_restoreTicks - m_restoreReadyAt >= 180)
            {
                m_restorePending = false;

                spdlog::info("[Inventory] applying possessions now");

                if (!Red::CallVirtual(Red::GetGameSystem<NetworkWorldSystem>(), "RestorePossessions"))
                    spdlog::error("[Inventory] RestorePossessions could not be called");
            }
        }
    }

    const auto delta = std::min(aTick - m_lastTick, 1000ull);
    m_lastTick = aTick;

    // Re-assert the server's clock and sky - the singleplayer simulation drifts them
    // between broadcasts, and thirty seconds of drift at high time scale is visible.
    if (m_worldState &&
        std::chrono::steady_clock::now() - m_lastWorldStateApply > std::chrono::seconds(30))
        ApplyWorldState();

    const auto service = Core::Container::Get<NetworkService>();
    if (service && service->IsConnected())
        progress(static_cast<float>(delta) / 1000.f);
}

void NetworkWorldSystem::OnWorldAttached(RED4ext::world::RuntimeScene* aScene)
{
    if (Settings::IsDisabled())
    {
        return;
    }
    spdlog::info("[NetworkWorldSystem] OnWorldAttached");
    IGameSystem::OnWorldAttached(aScene);

    // Deliberately here rather than on connect.
    //
    // RTTI is loaded with the game and has nothing to do with the server, so requiring a
    // successful connection to read it made the diagnostic depend on the very thing that
    // might be failing. A launch that only reaches the main menu now still answers the
    // question.
    DumpCustomizationApi();

    m_chatSystem->OnWorldAttached(aScene);
    m_appearanceSystem->OnWorldAttached(aScene);
    m_interpolationSystem->OnWorldAttached(aScene);
    m_vehicleSystem->OnWorldAttached(aScene);

    m_ready = true;

    // Replay every spawn that arrived while the world was still loading. Swapped out
    // first so a spawn arriving DURING the replay queues fresh instead of interleaving.
    if (!m_pendingSpawns.empty())
    {
        auto pending = std::exchange(m_pendingSpawns, {});
        spdlog::info("[Spawn] world ready - replaying {} queued spawn(s)", pending.size());

        for (const auto& spawn : pending)
            Spawn(spawn.ServerId, spawn.Position, spawn.Rotation, spawn.Equipment, spawn.Ccstate, spawn.IsMale, spawn.Username, spawn.Record);
    }

    // NO automatic connecting.
    //
    // This used to connect here, and it was wrong twice over. Cyberpunk's MAIN MENU is
    // itself a world, so OnWorldAttached fires about half a second after startup - long
    // before any save exists. The client connected from the menu, the server began
    // streaming players into a world with no game in it, and it died trying to spawn
    // them.
    //
    // The deeper problem is that joining a server should be a decision, not something
    // that happens to you because of how the game was launched. Connecting is now driven
    // explicitly - see the MULTIPLAYER entry in MainMenu.reds.
}

void NetworkWorldSystem::HandleTeleport(const PacketEvent<server::NotifyTeleport>& aMessage)
{
    const auto& destination = aMessage.get_position();

    const Red::Vector4 position{destination.get_x(), destination.get_y(), destination.get_z(), 1.f};

    spdlog::info("[NetworkWorldSystem] teleport to ({:.1f}, {:.1f}, {:.1f})", position.X, position.Y, position.Z);

    // Handed to redscript because the teleportation facility is script-side machinery -
    // it deals with streaming the destination in and putting the camera somewhere sane,
    // which writing a position straight into the entity does not.
    Red::CallVirtual(this, "TeleportLocalPlayer", position, aMessage.get_rotation());
}

void NetworkWorldSystem::HandleInteraction(const PacketEvent<server::NotifyInteraction>& aMessage)
{
    // Only the TARGET's client acts. The pusher and every bystander see the outcome
    // through ordinary movement sync - one machine owns each body, same rule as always.
    if (!m_remotePlayerId || aMessage.get_target_id() != *m_remotePlayerId)
        return;

    // 1 = push. Unknown interactions are ignored, not errored - an older client meeting
    // a newer server's interactions should shrug, not break.
    if (aMessage.get_interaction_id() != 1)
        return;

    if (!m_hasLastPosition)
        return;

    // Direction: away from whoever pushed. Their puppet's last synced position is in
    // the mirror's interpolation buffer.
    const auto actor = GetEntityByServerId(aMessage.get_actor_id());
    const auto* pInterpolation = actor ? actor.get<InterpolationComponent>() : nullptr;
    if (!pInterpolation || !pInterpolation->HasPrevious)
        return;

    const glm::vec3 from = pInterpolation->PreviousFrame.Position;
    glm::vec3 dir = m_lastPosition - from;
    dir.z = 0.f;

    const float length = glm::length(dir);
    if (length < 0.01f)
        return; // standing inside each other - no sane direction to stumble

    dir /= length;

    constexpr float kStumble = 1.5f;
    const glm::vec3 dest = m_lastPosition + dir * kStumble;

    // Face the pusher - anyone shoved turns around to see who did it. The game's yaw
    // convention has facing = (-sin(yaw), cos(yaw)); solving for the vector pointing
    // back along the push gives:
    const float yaw = std::atan2(dir.x, -dir.y);

    spdlog::info("[Interaction] pushed by {:x} - stumbling to ({:.1f}, {:.1f})",
                 aMessage.get_actor_id(), dest.x, dest.y);

    const Red::Vector4 position{dest.x, dest.y, dest.z, 1.f};
    Red::CallVirtual(this, "TeleportLocalPlayer", position, yaw);
}

void NetworkWorldSystem::HandleWorldState(const PacketEvent<server::NotifyWorldState>& aMessage)
{
    const auto total = aMessage.get_game_time_seconds();
    spdlog::info("[WorldState] server clock: day {} {:02}:{:02} (x{}), weather {:x}",
                 total / 86400, (total % 86400) / 3600, (total % 3600) / 60,
                 aMessage.get_time_scale(), aMessage.get_weather_id());

    m_worldState = WorldState{
        static_cast<double>(total),
        aMessage.get_time_scale(),
        aMessage.get_weather_id(),
        aMessage.get_transition_seconds(),
        std::chrono::steady_clock::now(),
    };

    ApplyWorldState();
}

void NetworkWorldSystem::ApplyWorldState()
{
    if (!m_worldState || !m_ready)
        return;

    m_lastWorldStateApply = std::chrono::steady_clock::now();

    // Advance from the snapshot rather than replaying it: a re-assert with the stale
    // value would rewind the sky every thirty seconds.
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - m_worldState->ReceivedAt).count();
    const auto seconds =
        static_cast<int32_t>(m_worldState->GameTimeSeconds + elapsed * m_worldState->TimeScale);

    if (auto* pTimeSystem = Red::GetGameSystem<Red::game::TimeSystem>())
        Red::CallVirtual(pTimeSystem, "SetGameTimeBySeconds", seconds);

    // 0 = natural cycle. Forcing weather is STICKY engine-side, so going back to 0
    // after a forced state must actively release the sky - just not re-asserting left
    // /weather reset doing nothing, forever rain.
    if (m_worldState->WeatherId != 0)
    {
        Red::ScriptGameInstance game;
        Red::Handle<Red::world::WeatherScriptInterface> weatherSystem;

        // SetWeather is Codeware's extension (a hard prerequisite of this mod). The
        // optional blend/priority parameters must still be passed - the invoker
        // enforces exact argument counts.
        if (Red::CallStatic("ScriptGameInstance", "GetWeatherSystem", weatherSystem, game) && weatherSystem)
        {
            bool ok = false;
            Red::CallVirtual(weatherSystem, "SetWeather", ok, Red::CName(m_worldState->WeatherId),
                             m_worldState->TransitionSeconds, 5u);

            if (!ok)
                spdlog::warn("[WorldState] SetWeather refused state {:x}", m_worldState->WeatherId);
            else
                m_weatherForced = true;
        }
    }
    else if (m_weatherForced)
    {
        Red::ScriptGameInstance game;
        Red::Handle<Red::world::WeatherScriptInterface> weatherSystem;

        if (Red::CallStatic("ScriptGameInstance", "GetWeatherSystem", weatherSystem, game) && weatherSystem)
        {
            bool ok = false;
            Red::CallVirtual(weatherSystem, "ResetWeather", ok, true, m_worldState->TransitionSeconds);

            spdlog::info("[WorldState] weather released back to the natural cycle ({})", ok);
            m_weatherForced = false;
        }
    }
}

void NetworkWorldSystem::RequestJoin()
{
    spdlog::info("[NetworkWorldSystem] join requested from the main menu");
    m_joinRequested = true;
}

void NetworkWorldSystem::MarkNewCharacter()
{
    spdlog::info("[Character] NEW CHARACTER chosen - this appearance will replace the stored one");
    m_newCharacterPending = true;
}

uint32_t NetworkWorldSystem::GetRestoreCount() const
{
    return static_cast<uint32_t>(m_restoreInventory.size());
}

uint64_t NetworkWorldSystem::GetRestoreId(uint32_t aIndex) const
{
    return aIndex < m_restoreInventory.size() ? m_restoreInventory[aIndex].get_id() : 0;
}

uint32_t NetworkWorldSystem::GetRestoreQuantity(uint32_t aIndex) const
{
    return aIndex < m_restoreInventory.size() ? m_restoreInventory[aIndex].get_quantity() : 0;
}

int32_t NetworkWorldSystem::GetRestoreMoney() const
{
    return static_cast<int32_t>(m_restoreMoney);
}

void NetworkWorldSystem::ScriptLog(const Red::CString& acText) const
{
    spdlog::info("[script] {}", acText.c_str());
}

void NetworkWorldSystem::AddProficiency(uint32_t aType, int32_t aLevel)
{
    // Level 0 is the game's default for a proficiency nobody has touched. Storing it is
    // harmless but noisy, and restoring it is a no-op, so it is dropped here rather than
    // carried through the record and the wire for nothing.
    if (aLevel <= 0)
        return;

    client::Proficiency prof;
    prof.set_type(aType);
    prof.set_level(aLevel);

    m_capturedProficiencies.push_back(prof);
}

void NetworkWorldSystem::AddAttribute(uint32_t aType, int32_t aValue)
{
    if (aValue <= 0)
        return;

    client::Attribute a;
    a.set_type(aType);
    a.set_value(aValue);
    m_capturedAttributes.push_back(a);
}

void NetworkWorldSystem::AddPerk(uint32_t aType, int32_t aLevel)
{
    if (aLevel <= 0)
        return;

    client::Perk k;
    k.set_type(aType);
    k.set_level(aLevel);
    m_capturedPerks.push_back(k);
}

uint32_t NetworkWorldSystem::GetFactCount() const
{
    return static_cast<uint32_t>(m_worldFacts.size());
}

Red::CString NetworkWorldSystem::GetFactName(uint32_t aIndex) const
{
    return aIndex < m_worldFacts.size() ? Red::CString(m_worldFacts[aIndex].get_name().c_str())
                                        : Red::CString("");
}

int32_t NetworkWorldSystem::GetFactValue(uint32_t aIndex) const
{
    return aIndex < m_worldFacts.size() ? m_worldFacts[aIndex].get_value() : 0;
}

void NetworkWorldSystem::AddVehicle(const Red::CString& acName)
{
    if (acName.Length() == 0)
        return;

    m_capturedVehicles.push_back(String(acName.c_str()));
}

uint32_t NetworkWorldSystem::GetRestoreVehicleCount() const
{
    return static_cast<uint32_t>(m_restoreVehicles.size());
}

Red::CString NetworkWorldSystem::GetRestoreVehicle(uint32_t aIndex) const
{
    return aIndex < m_restoreVehicles.size() ? Red::CString(m_restoreVehicles[aIndex].c_str())
                                             : Red::CString("");
}

uint32_t NetworkWorldSystem::GetRestorePerkCount() const
{
    return static_cast<uint32_t>(m_restorePerks.size());
}

uint32_t NetworkWorldSystem::GetRestorePerkType(uint32_t aIndex) const
{
    return aIndex < m_restorePerks.size() ? m_restorePerks[aIndex].get_type() : 0;
}

int32_t NetworkWorldSystem::GetRestorePerkLevel(uint32_t aIndex) const
{
    return aIndex < m_restorePerks.size() ? m_restorePerks[aIndex].get_level() : 0;
}

uint32_t NetworkWorldSystem::GetRestoreAttributeCount() const
{
    return static_cast<uint32_t>(m_restoreAttributes.size());
}

uint32_t NetworkWorldSystem::GetRestoreAttributeType(uint32_t aIndex) const
{
    return aIndex < m_restoreAttributes.size() ? m_restoreAttributes[aIndex].get_type() : 0;
}

int32_t NetworkWorldSystem::GetRestoreAttributeValue(uint32_t aIndex) const
{
    return aIndex < m_restoreAttributes.size() ? m_restoreAttributes[aIndex].get_value() : 0;
}

void NetworkWorldSystem::SetLifepath(const uint32_t aLifepath)
{
    m_capturedLifepath = aLifepath;
}

void NetworkWorldSystem::BeginInventoryCapture()
{
    m_capturedVehicles.clear();
    m_capturedAttributes.clear();
    m_capturedPerks.clear();
    m_capturedProficiencies.clear();
    m_capturedInventory.clear();
    m_capturedMoney = 0;
    m_hasCapturedPossessions = false;
}

void NetworkWorldSystem::AddInventoryItem(uint64_t aId, uint32_t aQuantity)
{
    // A zero id is not an item and a zero count is not a holding. Both mean the read
    // failed upstream, and storing them would hand the player junk back on their next
    // spawn - which is worse than the item simply being missed.
    if (aId == 0 || aQuantity == 0)
        return;

    client::ItemStack stack;
    stack.set_id(aId);
    stack.set_quantity(aQuantity);

    m_capturedInventory.push_back(stack);
}

Red::TweakDBID NetworkWorldSystem::TdbidFromNumber(uint64_t aValue) const
{
    Red::TweakDBID id;
    id.value = aValue;
    return id;
}

void NetworkWorldSystem::EndInventoryCapture(int64_t aMoney)
{
    m_capturedMoney = aMoney;
    m_hasCapturedPossessions = true;

    spdlog::info("[Inventory] captured {} stack(s) and {} eddies", m_capturedInventory.size(), aMoney);
}

void NetworkWorldSystem::RequestRespawn()
{
    const auto& service = Core::Container::Get<NetworkService>();
    if (!service || !service->IsConnected())
        return;

    spdlog::info("[NetworkWorldSystem] downed - asking the server where to respawn");

    client::RespawnRequest request;
    service->Send(request);
}

/**
 * Asks the game what the character customization system can actually do.
 *
 * Every other way of finding this out has been exhausted. redscript sees two methods on
 * the interface; the type dump has no open event, no mirror class and no creator
 * controller; the SDK's generated class is an opaque blob. All of which establish only
 * that the answer is not written down anywhere WE can read.
 *
 * The game itself knows. RTTI carries every native function on every class, including the
 * ones never exposed to scripts - that is how the engine dispatches them. So rather than
 * guessing at names, this walks the class and its parents and writes down what is really
 * there.
 *
 * Once per session, on connect. It is a few dozen log lines and it is the difference
 * between "the creator cannot be opened" and knowing the name of the function that opens
 * it.
 */
void NetworkWorldSystem::DumpCustomizationApi() const
{
    static bool s_dumped = false;
    if (s_dumped)
        return;

    s_dumped = true;

    auto* pRtti = Red::CRTTISystem::Get();
    if (!pRtti)
        return;

    // Both the concrete class and the interface it inherits - the interesting methods
    // could be on either, and checking one and concluding "not there" is how this took
    // three attempts already.
    for (const char* name : {"gameuiCharacterCustomizationSystem", "gameuiICharacterCustomizationSystem"})
    {
        auto* pClass = pRtti->GetClass(name);

        if (!pClass)
        {
            spdlog::warn("[CCApi] no RTTI class named {}", name);
            continue;
        }

        spdlog::info("[CCApi] === {} : {} function(s) ===", name, pClass->funcs.size);

        for (auto* pFunc : pClass->funcs)
        {
            if (!pFunc)
                continue;

            std::string params;

            for (auto* pParam : pFunc->params)
            {
                if (!pParam || !pParam->type)
                    continue;

                Red::CName typeName;
                pParam->type->GetName(typeName);

                if (!params.empty())
                    params += ", ";

                params += typeName.ToString();
            }

            spdlog::info("[CCApi]   {}({})", pFunc->shortName.ToString(), params);
        }
    }
}

/**
 * Notices when the player has finished changing how they look, and saves it.
 *
 * Nobody should have to type a command to keep their own face. An earlier version made
 * saving explicit because the creator gives no callback when it closes and the
 * customization system has no IsActive to poll - both true, and both the wrong thing to
 * look at.
 *
 * The signal was already written down a few lines up in NetworkService: the customization
 * STATE INSTANCE is null during normal gameplay and non-null only while somebody is
 * editing. So the transition from non-null back to null is exactly "they closed the
 * mirror", which is the event that seemed unavailable.
 *
 * Polled rather than hooked because there is nothing to hook - but it is a null check
 * once a second, not a serialisation, so the cost is nothing until someone is actually
 * standing at a mirror.
 */
void NetworkWorldSystem::PollEquipmentChanges()
{
    const auto& service = Core::Container::Get<NetworkService>();
    if (!service || !service->IsConnected())
        return;

    // Nothing to compare against until the restore has landed.
    //
    // A capture during restore reads the pre-restore loadout, and reporting THAT as a
    // change would tell everyone the player got dressed in whatever the world template was
    // wearing - the same race the possessions autosave already refuses to run into.
    if (m_restorePending)
        return;

    const auto system = Red::GetGameSystem<Game::PlayerSystem>();
    Red::Handle<Red::GameObject> player;

    if (system)
        system->GetLocalPlayerControlledGameObject(player);

    if (!player)
        return;

    const auto apprSystem = GetAppearanceSystem();
    if (!apprSystem)
        return;

    auto items = apprSystem->GetPlayerItems(player);

    // An empty read is "nobody looked", not "wearing nothing" - the same ambiguity the
    // server refuses to act on for possessions. Sending it would strip the player on every
    // other screen the first time the item system was busy.
    if (items.empty())
        return;

    if (items == m_lastSentEquipment)
        return;

    // First read of the session establishes the baseline without sending. What they are
    // wearing on arrival already went out with their spawn, so announcing it again would
    // be a redundant 9KB for every player who simply logged in.
    const bool firstRead = m_lastSentEquipment.empty();
    m_lastSentEquipment = items;

    if (firstRead)
        return;

    client::UpdateAppearanceRequest request;
    request.set_equipment(items);

    // Clothing only. The customization blob is deliberately left empty: it is 9KB, it has
    // not changed, and the server treats absence as "keep the face you have". A ripperdoc
    // visit goes through the character save path, which broadcasts on its own.
    service->Send(request);

    spdlog::info("[Appearance] equipment changed - told the server about {} item(s)", items.size());
}

void NetworkWorldSystem::PollAppearanceChanges()
{
    const auto& service = Core::Container::Get<NetworkService>();
    if (!service || !service->IsConnected())
        return;

    auto ccSystem = Red::GetGameSystem<Red::game::ui::CharacterCustomizationSystem>();
    auto stateHandle = GetCustomizationState(ccSystem);

    const bool customising = stateHandle && stateHandle->instance;

    if (customising)
    {
        // Kept up to date while they edit, so whatever they had at the moment it closed is
        // what gets saved. Serialising here rather than on close matters: by the time the
        // instance is null there is nothing left to read.
        auto writer = CMPWriter();
        CharacterCustomizationState_Serialize(stateHandle->instance, &writer);

        // Implausibly small means half-built, not "a simple face".
        //
        // A real appearance is 7-9KB. A 23-byte one was captured and saved during testing,
        // and then used to spawn that player - the customization state exists for a moment
        // before it is populated, and polling caught it in that window. Rejecting only
        // EMPTY blobs was not enough, because the degenerate case is not empty.
        //
        // Held rather than replaced: if this poll caught a bad moment, the good bytes from
        // the previous one are still what gets saved.
        constexpr size_t kMinPlausibleAppearance = 1024;

        if (writer.bytes.size() >= kMinPlausibleAppearance)
        {
            m_pendingAppearance = writer.bytes;
            m_pendingIsMale = stateHandle->instance->isBodyGenderMale;
        }

        m_wasCustomising = true;
        return;
    }

    if (!m_wasCustomising)
        return;

    // Closed. Send what they finished with.
    m_wasCustomising = false;

    if (m_pendingAppearance.empty())
        return;

    client::SaveCharacterRequest request;
    request.set_ccstate(m_pendingAppearance);
    request.set_is_male(m_pendingIsMale);

    // Ask the script side to read the inventory first.
    //
    // Native cannot read it - the item API is script-only - and script cannot send it, so
    // the capture is requested here, synchronously, and lands in the buffers below before
    // this request is built.
    Red::CallVirtual(Red::GetGameSystem<NetworkWorldSystem>(), "CaptureInventory");

    // Possessions ride along with the appearance save.
    //
    // The same message, rather than one of their own, because they are saved at the same
    // moments and for the same reason: this is the point at which the server is told what
    // this character IS. A separate message would be a second thing to keep in step, and
    // the two could then disagree about which character they described.
    //
    // Sent only when a capture actually ran. An empty list is ambiguous - it reads as
    // "owns nothing" and as "nobody looked" - and the server treats absence as "leave what
    // is stored alone" rather than emptying somebody's pockets.
    // AFTER the capture above, never before it.
    //
    // The first version set this beside set_is_male, several lines ahead of
    // CallVirtual("CaptureInventory") - the call that actually fills it in. So the request
    // always carried the "never reported" sentinel: the server logged "unsupported
    // lifepath (4294967295)" and granted no kit, while the client's own log showed it had
    // read the lifepath correctly (capture: lifepath 1, Nomad) a moment later. Both halves
    // were right; only the order was wrong.
    request.set_lifepath(m_capturedLifepath);

    if (m_hasCapturedPossessions)
    {
        request.set_inventory(m_capturedInventory);
        request.set_money(m_capturedMoney);
        request.set_proficiencies(m_capturedProficiencies);
        request.set_attributes(m_capturedAttributes);
        request.set_perks(m_capturedPerks);
        request.set_vehicles(m_capturedVehicles);

        // Announced, unlike the timer. This fires when somebody has just finished changing
        // their face at a ripperdoc - they did something deliberate and a confirmation
        // that it stuck is worth having. The ninety-second timer is the opposite: nobody
        // asked, and saying so every ninety seconds buries real chat.
        request.set_automatic(false);
    }

    // No name. An appearance save is not an identity change - this used to send the
    // Discord name on every ripperdoc visit, and the server took any non-empty name as a
    // rename: editing your hair as 'Silverhand92' walked you out named after your account,
    // marked NameChosen, and silenced the name prompt forever. Names travel exactly two
    // roads: /name, and /character save <name>.

    service->Send(request);

    spdlog::info("[Character] appearance changed - saved {} bytes to the server",
                 m_pendingAppearance.size());

    m_pendingAppearance.clear();
}

/**
 * Sends the player's current appearance to the server as their character.
 *
 * Kept as a manual override - PollAppearanceChanges is what actually saves in normal use.
 * Useful when something has gone wrong and somebody needs to force it.
 */
void NetworkWorldSystem::SaveCharacterAppearance(bool aAutomatic)
{
    const auto& service = Core::Container::Get<NetworkService>();
    if (!service || !service->IsConnected())
    {
        spdlog::warn("[Character] not connected - cannot save the character");
        return;
    }

    auto ccSystem = Red::GetGameSystem<Red::game::ui::CharacterCustomizationSystem>();
    auto stateHandle = GetCustomizationState(ccSystem);

    // GetCustomizationState() returns a pointer that can never be null; the instance
    // behind it can be, and is during ordinary gameplay. Serialising a null instance
    // crashes the game, so the instance is what gets checked.
    if (!stateHandle || !stateHandle->instance)
    {
        spdlog::error("[Character] no customization state to save");
        return;
    }

    auto writer = CMPWriter();
    CharacterCustomizationState_Serialize(stateHandle->instance, &writer);

    if (writer.bytes.empty())
    {
        spdlog::error("[Character] the customization state serialised to nothing - not saving");
        return;
    }

    client::SaveCharacterRequest request;
    request.set_ccstate(writer.bytes);
    request.set_is_male(stateHandle->instance->isBodyGenderMale);

    // Ask the script side to read the inventory first.
    //
    // Native cannot read it - the item API is script-only - and script cannot send it, so
    // the capture is requested here, synchronously, and lands in the buffers below before
    // this request is built.
    Red::CallVirtual(Red::GetGameSystem<NetworkWorldSystem>(), "CaptureInventory");

    // Possessions ride along with the appearance save.
    //
    // The same message, rather than one of their own, because they are saved at the same
    // moments and for the same reason: this is the point at which the server is told what
    // this character IS. A separate message would be a second thing to keep in step, and
    // the two could then disagree about which character they described.
    //
    // Sent only when a capture actually ran. An empty list is ambiguous - it reads as
    // "owns nothing" and as "nobody looked" - and the server treats absence as "leave what
    // is stored alone" rather than emptying somebody's pockets.
    // AFTER the capture above, never before it.
    //
    // The first version set this beside set_is_male, several lines ahead of
    // CallVirtual("CaptureInventory") - the call that actually fills it in. So the request
    // always carried the "never reported" sentinel: the server logged "unsupported
    // lifepath (4294967295)" and granted no kit, while the client's own log showed it had
    // read the lifepath correctly (capture: lifepath 1, Nomad) a moment later. Both halves
    // were right; only the order was wrong.
    request.set_lifepath(m_capturedLifepath);

    if (m_hasCapturedPossessions)
    {
        request.set_inventory(m_capturedInventory);
        request.set_money(m_capturedMoney);
        request.set_proficiencies(m_capturedProficiencies);
        request.set_attributes(m_capturedAttributes);
        request.set_perks(m_capturedPerks);
        request.set_vehicles(m_capturedVehicles);
        request.set_automatic(aAutomatic);
    }

    // No name here either - same reason as PollAppearanceChanges. This path also serves
    // the first capture of a brand-new character (capture_only), and that case needs no
    // name from us: the server labels a nameless new character with the account username
    // and leaves NameChosen false, which is exactly what triggers the name prompt.

    service->Send(request);

    spdlog::info("[Character] sent {} bytes of appearance to the server", writer.bytes.size());
}

bool NetworkWorldSystem::IsConnected() const
{
    const auto& service = Core::Container::Get<NetworkService>();
    return service && service->IsConnected();
}

void NetworkWorldSystem::SetCharacterStatus(bool aHasCharacter, const char* acName, int32_t aLevel,
                                            bool aSpawnedBefore)
{
    m_characterStatusKnown = true;
    m_hasCharacter = aHasCharacter;
    m_characterName = acName ? acName : "";
    m_characterLevel = aLevel;
    m_characterSpawnedBefore = aSpawnedBefore;

    spdlog::info("[Character] server reports: {}{}", aHasCharacter ? "has a character" : "no character",
                 aHasCharacter ? fmt::format(" - '{}' level {}, {}", m_characterName, aLevel,
                                             aSpawnedBefore ? "played before" : "never spawned")
                               : "");
}

// ---------------------------------------------------------------------------
// Voice
// ---------------------------------------------------------------------------

void NetworkWorldSystem::VoiceRefreshDevices()
{
    m_voiceInputs = m_voice.EnumerateInputDevices();
    m_voiceOutputs = m_voice.EnumerateOutputDevices();

    spdlog::info("[Voice] {} microphone(s), {} output(s)", m_voiceInputs.size(), m_voiceOutputs.size());

    for (const auto& device : m_voiceInputs)
    {
        spdlog::info("[Voice]   in  {}{}{}", device.Name,
                     device.IsDefault ? " (default)" : "",
                     device.LooksLikeLoopback ? " [SYSTEM AUDIO]" : "");
    }
}

Red::CString NetworkWorldSystem::VoiceInputName(uint32_t aIndex) const
{
    return aIndex < m_voiceInputs.size() ? Red::CString(m_voiceInputs[aIndex].Name.c_str()) : Red::CString("");
}

Red::CString NetworkWorldSystem::VoiceInputId(uint32_t aIndex) const
{
    return aIndex < m_voiceInputs.size() ? Red::CString(m_voiceInputs[aIndex].Id.c_str()) : Red::CString("");
}

bool NetworkWorldSystem::VoiceInputIsDefault(uint32_t aIndex) const
{
    return aIndex < m_voiceInputs.size() && m_voiceInputs[aIndex].IsDefault;
}

bool NetworkWorldSystem::VoiceInputIsLoopback(uint32_t aIndex) const
{
    return aIndex < m_voiceInputs.size() && m_voiceInputs[aIndex].LooksLikeLoopback;
}

Red::CString NetworkWorldSystem::VoiceOutputName(uint32_t aIndex) const
{
    return aIndex < m_voiceOutputs.size() ? Red::CString(m_voiceOutputs[aIndex].Name.c_str()) : Red::CString("");
}

Red::CString NetworkWorldSystem::VoiceOutputId(uint32_t aIndex) const
{
    return aIndex < m_voiceOutputs.size() ? Red::CString(m_voiceOutputs[aIndex].Id.c_str()) : Red::CString("");
}

bool NetworkWorldSystem::VoiceStartCapture(const Red::CString& acDeviceId)
{
    return m_voice.StartCapture(acDeviceId.c_str());
}

void NetworkWorldSystem::VoiceStopCapture()
{
    m_voice.StopCapture();
}

bool NetworkWorldSystem::VoiceIsCapturing() const
{
    return m_voice.IsCapturing();
}

float NetworkWorldSystem::VoiceInputLevel()
{
    return m_voice.ReadInputPeak();
}

Red::CString NetworkWorldSystem::VoiceLastError() const
{
    return Red::CString(m_voice.GetLastError().c_str());
}

void NetworkWorldSystem::DeleteCharacter()
{
    const auto& service = Core::Container::Get<NetworkService>();

    if (!service || !service->IsConnected())
    {
        spdlog::warn("[Character] delete requested with no connection");
        return;
    }

    spdlog::info("[Character] asking the server to delete this account's character");

    client::DeleteCharacterRequest request;
    service->Send(request);
}

void NetworkWorldSystem::HandleCharacterList(const PacketEvent<server::NotifyCharacterList>& aMessage)
{
    m_characterError = aMessage.get_error().c_str();

    const auto& characters = aMessage.get_characters();

    if (characters.empty())
    {
        SetCharacterStatus(false, "", 0, false);
    }
    else
    {
        const auto& first = characters[0];
        SetCharacterStatus(true, first.get_name().c_str(), first.get_level(),
                           first.get_spawned_before());
    }

    if (!m_characterError.empty())
        spdlog::warn("[Character] the server refused: {}", m_characterError);
}

void NetworkWorldSystem::EnterWorld()
{
    const auto& service = Core::Container::Get<NetworkService>();

    if (!service || !service->IsConnected())
    {
        spdlog::warn("[Character] EnterWorld with no connection - nothing to announce");
        return;
    }

    // Only when one is actually owed. Entering the world by a route that already spawned -
    // the old in-world connect path - must not send a second announcement, which would
    // tell everybody to build a second puppet for this player.
    if (!service->IsSpawnDeferred())
        return;

    spdlog::info("[Character] entering the world - sending the spawn held back at the selector");
    service->SendSpawnCharacterRequest();
}

bool NetworkWorldSystem::ConsumeJoinRequest()
{
    // Deliberately one-shot. Loading a save from the MULTIPLAYER entry should connect;
    // loading another save afterwards from the pause menu should not silently reconnect
    // someone who never asked for it a second time.
    const bool requested = m_joinRequested;
    m_joinRequested = false;

    return requested;
}

void NetworkWorldSystem::OnAfterWorldDetach()
{
    if (Settings::IsDisabled())
    {
        return;
    }
    spdlog::info("[NetworkWorldSystem] OnAfterWorldDetach");
    m_ready = false;
    // Anything still queued belongs to the world we just left.
    m_pendingSpawns.clear();

    m_interpolationSystem->OnAfterWorldDetach();
    m_chatSystem->OnAfterWorldDetach();
    m_vehicleSystem->OnAfterWorldDetach();

    m_remotePlayerId = std::nullopt;

    IGameSystem::OnAfterWorldDetach();
}

void NetworkWorldSystem::OnBeforeWorldDetach(RED4ext::world::RuntimeScene* aScene)
{
    if (Settings::IsDisabled())
    {
        return;
    }

    // Stand everything down BEFORE the engine starts destroying entities. A quit (or a
    // death-reload, or a server switch) tears the world apart while remote puppets are
    // still live - and until now our animation hook, interpolation pass and vehicle
    // bookkeeping kept driving them straight into that teardown. zeldfep's 2026-08-21
    // 01:30 crash-on-quit ends exactly one line into the detach with no stage named,
    // which is why every stage announces itself here: the next teardown death names
    // its killer instead of ending the log mid-sentence.
    spdlog::info("[Detach] stand-down: silencing the animation hook");
    App::PuppetRegistry::Clear();
    App::PuppetRegistry::ClearDrivers();

    spdlog::info("[Detach] stand-down: stopping interpolation");
    m_interpolationSystem->OnDisconnected();

    spdlog::info("[Detach] stand-down: dropping vehicle state");
    m_vehicleSystem->OnDisconnected();

    spdlog::info("[Detach] engine detach");
    IGameSystem::OnBeforeWorldDetach(aScene);

    spdlog::info("[Detach] appearance detach");
    m_appearanceSystem->OnBeforeWorldDetach(aScene);

    spdlog::info("[Detach] done - the engine owns the teardown from here");
}

void NetworkWorldSystem::HandleVehicleControlAssigned(const PacketEvent<server::NotifyVehicleControlAssigned>& aMessage)
{
    // The server has made this player the driver of a car they are already sitting in.
    // Getting them into the actual seat is script work - the mounting facility is not
    // reachable from here - so this only has to find the local entity and ask.
    const auto entity = GetEntityByServerId(aMessage.get_vehicle_id());

    if (!entity)
    {
        spdlog::warn("[Vehicle] promoted to driver of {} but we have no such vehicle",
                     aMessage.get_vehicle_id());
        return;
    }

    const auto* pEntityComponent = entity.get<EntityComponent>();
    if (!pEntityComponent)
        return;

    spdlog::info("[Vehicle] promoted to driver of {} - taking the seat", aMessage.get_vehicle_id());

    Red::CallVirtual(Red::GetGameSystem<NetworkWorldSystem>(), "TakeDriverSeat", pEntityComponent->Id);
}

void NetworkWorldSystem::SendWeaponEvent(uint32_t aKind, uint64_t aWeaponId, uint32_t aMagazine, uint32_t aReserve)
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();

    if (!pNetworkService || !pNetworkService->IsConnected())
        return;

    client::WeaponEventRequest request;
    request.set_kind(aKind);
    request.set_weapon_id(aWeaponId);
    request.set_magazine_ammo(aMagazine);
    request.set_reserve_ammo(aReserve);
    request.set_sequence(++m_weaponSequence);
    request.set_client_tick(GetTick());

    pNetworkService->Send(request);
}

void NetworkWorldSystem::SendQuickhackRequest(uint64_t aTargetServerId, uint64_t aQuickhackId, float aRamCost)
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();

    if (!pNetworkService || !pNetworkService->IsConnected() || aTargetServerId == 0)
        return;

    client::QuickhackRequest request;
    request.set_target_id(aTargetServerId);
    request.set_quickhack_id(aQuickhackId);
    request.set_ram_cost(aRamCost);
    request.set_sequence(++m_quickhackSequence);

    pNetworkService->Send(request);

    spdlog::info("[Combat] quickhack request on entity {} - reported cost {:.1f}", aTargetServerId, aRamCost);
}

void NetworkWorldSystem::SendStatusEffect(uint64_t aTargetServerId, uint64_t aEffectId, uint32_t aStacks,
                                          uint64_t aSourceId)
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();

    if (!pNetworkService || !pNetworkService->IsConnected() || aTargetServerId == 0 || aEffectId == 0)
        return;

    client::StatusEffectRequest request;
    request.set_target_id(aTargetServerId);
    request.set_effect_id(aEffectId);
    request.set_stacks(aStacks);
    request.set_source_id(aSourceId);
    request.set_sequence(++m_combatSequence);

    pNetworkService->Send(request);

    spdlog::info("[Combat] reported status effect {} on entity {}", aEffectId, aTargetServerId);
}

void NetworkWorldSystem::HandleStatusEffect(const PacketEvent<server::NotifyStatusEffect>& aMessage)
{
    // Is this about US? Everyone in range receives it, because a bystander may want to
    // render something; only the target actually applies it.
    const auto self = GetRemotePlayerId();

    if (!self || *self != aMessage.get_target_id())
        return;

    // Queued rather than applied here. Applying a status effect is a script-side API, and
    // this runs on the network thread - the same reason voice frames are queued rather than
    // decoded where they arrive.
    m_incomingStatusEffects.push_back(aMessage.get_effect_id());

    spdlog::info("[Combat] status effect {} landed on us from entity {}", aMessage.get_effect_id(),
                 aMessage.get_source_id());
}

void NetworkWorldSystem::HandleDamageResult(const PacketEvent<server::NotifyDamageResult>& aMessage)
{
    const auto self = GetRemotePlayerId();

    // Everyone in range receives this so bystanders can render the hit. Only the person it
    // happened to changes their own health from it.
    if (!self || *self != aMessage.get_target_id())
        return;

    // Absolute, not a delta. A dropped delta diverges forever; an absolute figure from the
    // authority cannot, and the server is the only thing that knows what armour actually
    // did to the number.
    m_incomingHealth = aMessage.get_target_health_after();

    if (aMessage.get_knocked_down())
        m_downed = true;

    spdlog::info("[Combat] we took {:.1f} damage from entity {} - health now {:.1f}{}",
                 aMessage.get_final_damage(), aMessage.get_attacker_id(), aMessage.get_target_health_after(),
                 aMessage.get_knocked_down() ? " (DOWNED)" : "");
}

void NetworkWorldSystem::HandleCombatState(const PacketEvent<server::NotifyCombatState>& aMessage)
{
    const auto self = GetRemotePlayerId();

    if (!self || *self != aMessage.get_id())
        return;

    m_incomingHealth = aMessage.get_health();
    m_downed = aMessage.get_life_state() != 0;

    spdlog::info("[Combat] server set our health to {:.1f} (state {})", aMessage.get_health(),
                 aMessage.get_life_state());
}

void NetworkWorldSystem::SendQuickhackUpload(uint64_t aTargetServerId, uint32_t aState, uint64_t aQuickhackId,
                                             float aDuration)
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();

    if (!pNetworkService || !pNetworkService->IsConnected() || aTargetServerId == 0)
        return;

    client::QuickhackUploadRequest request;
    request.set_target_id(aTargetServerId);
    request.set_state(aState);
    request.set_quickhack_id(aQuickhackId);
    request.set_duration(aDuration);
    request.set_sequence(++m_combatSequence);

    pNetworkService->Send(request);
}

void NetworkWorldSystem::HandleQuickhackUpload(const PacketEvent<server::NotifyQuickhackUpload>& aMessage)
{
    // Which local entity is the target? For a remote player that is their puppet; when the
    // target is US there is no puppet, and script uses the local player instead.
    const auto self = GetRemotePlayerId();
    const bool aboutUs = self && *self == aMessage.get_target_id();

    const auto entityId =
        aboutUs ? Red::EntityID{} : GetEntityIdByServerId(aMessage.get_target_id());

    // A hack aimed at somebody whose puppet we do not hold - out of range, or not yet
    // spawned here. Nothing to draw.
    if (!aboutUs && entityId.hash == 0)
        return;

    m_incomingUploads.push_back(aboutUs ? 0 : entityId.hash);
    m_incomingUploadStates.push_back(aMessage.get_state());

    spdlog::info("[Combat] quickhack upload {} on {} from entity {}",
                 aMessage.get_state() == 0 ? "started" : "completed", aboutUs ? "US" : "a remote player",
                 aMessage.get_source_id());
}

uint64_t NetworkWorldSystem::ConsumeIncomingUploadTarget()
{
    if (m_incomingUploads.empty())
    {
        m_incomingUploadState = 0;
        return 0;
    }

    // The state is read through a separate accessor rather than returned alongside, because
    // redscript cannot receive two values from one native call without a struct - and a
    // struct across RTTI is exactly the marshalling this boundary avoids everywhere else.
    // Set BEFORE the id is handed over, so a caller that reads both always sees a pair.
    m_incomingUploadState = m_incomingUploadStates.front();

    const auto target = m_incomingUploads.front();

    m_incomingUploads.erase(m_incomingUploads.begin());
    m_incomingUploadStates.erase(m_incomingUploadStates.begin());

    // Zero means "us", which is a real answer rather than "nothing waiting" - so it is
    // returned as a sentinel the caller distinguishes by having asked at all. An empty
    // queue is the only case that yields zero with state left at zero as well.
    return target == 0 ? std::numeric_limits<uint64_t>::max() : target;
}

float NetworkWorldSystem::ConsumeIncomingHealth()
{
    const auto health = m_incomingHealth;
    m_incomingHealth = -1.f;

    return health;
}

uint64_t NetworkWorldSystem::ConsumeIncomingStatusEffect()
{
    if (m_incomingStatusEffects.empty())
        return 0;

    const auto effect = m_incomingStatusEffects.front();
    m_incomingStatusEffects.erase(m_incomingStatusEffects.begin());

    return effect;
}

void NetworkWorldSystem::HandleVoiceFrame(const PacketEvent<server::NotifyVoiceFrame>& aMessage)
{
    // Straight into the queue. This runs on the network thread, and decoding here would put
    // Opus decoder state under two threads - see the header note on VoiceClient.
    const auto& data = aMessage.get_data();

    m_voiceClient.OnFrameReceived(aMessage.get_id(), data.data(), data.size(), aMessage.get_sequence());
}

bool NetworkWorldSystem::VoiceStart(const Red::CString& acInputDevice, const Red::CString& acOutputDevice)
{
    // The settings meter and voice cannot both hold the microphone. Whoever asks last wins,
    // and the meter is the one that gives way - somebody pressing talk means it.
    m_voice.StopCapture();

    return m_voiceClient.Start(acInputDevice.c_str(), acOutputDevice.c_str());
}

void NetworkWorldSystem::VoiceStop()
{
    m_voiceClient.Stop();
}

bool NetworkWorldSystem::VoiceIsRunning() const
{
    return m_voiceClient.IsRunning();
}

void NetworkWorldSystem::VoiceSetTransmitting(bool aOn)
{
    m_voiceClient.SetTransmitting(aOn);
}

bool NetworkWorldSystem::VoiceIsTransmitting() const
{
    return m_voiceClient.IsTransmitting();
}

void NetworkWorldSystem::VoiceSetRange(uint32_t aRange)
{
    m_voiceClient.SetRange(aRange);
}

uint32_t NetworkWorldSystem::VoiceGetRange() const
{
    return m_voiceClient.GetRange();
}

void NetworkWorldSystem::VoiceSetMicVolume(uint32_t aPercent)
{
    m_voiceClient.SetMicVolume(aPercent);
}

void NetworkWorldSystem::VoiceSetPlaybackVolume(uint32_t aPercent)
{
    m_voiceClient.SetPlaybackVolume(aPercent);
}

uint32_t NetworkWorldSystem::VoiceActiveSpeakerCount() const
{
    return static_cast<uint32_t>(m_voiceClient.GetActiveSpeakers().size());
}

bool NetworkWorldSystem::HackablePuppetsEnabled() const
{
    return Settings::Get().hackablePuppets;
}

uint64_t NetworkWorldSystem::GetServerIdByEntity(Red::EntityID aEntityId) const
{
    const auto entity = FindEntity(aEntityId);

    // Zero rather than an error. Most things hit in this game are ordinary NPCs, props and
    // scenery, and the hit hook runs for all of them - "not one of ours" is the common
    // case, not a failure.
    if (!entity || !entity.is_alive())
        return 0;

    return entity.id();
}

void NetworkWorldSystem::SendCombatEvent(uint64_t aTargetServerId, uint32_t aSourceType, uint32_t aAttackType,
                                         uint64_t aSourceId, uint32_t aDamageType, uint32_t aHitZone, float aDamage,
                                         const Red::Vector4& aPosition, const Red::Vector4& aDirection, bool aCritical,
                                         bool aHeadshot)
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();

    if (!pNetworkService || !pNetworkService->IsConnected() || aTargetServerId == 0)
        return;

    client::CombatEventRequest request;

    // Ours, so the result can be matched back. Connection-scoped - the server pairs it with
    // the connection, so it does not have to be globally unique.
    request.set_event_id(++m_combatEventId);

    request.set_target_id(aTargetServerId);
    request.set_source_type(aSourceType);
    request.set_attack_type(aAttackType);
    request.set_source_id(aSourceId);
    request.set_damage_type(aDamageType);
    request.set_hit_zone(aHitZone);
    request.set_reported_damage(aDamage);
    request.set_critical(aCritical);
    request.set_headshot(aHeadshot);
    request.set_client_tick(GetTick());
    request.set_sequence(++m_combatSequence);

    common::Vector3 position;
    position.set_x(aPosition.X);
    position.set_y(aPosition.Y);
    position.set_z(aPosition.Z);
    request.set_hit_position(position);

    common::Vector3 direction;
    direction.set_x(aDirection.X);
    direction.set_y(aDirection.Y);
    direction.set_z(aDirection.Z);
    request.set_hit_direction(direction);

    // Reliable, unlike movement and voice. A lost hit is a shot that did nothing, which
    // players notice immediately and cannot explain - and unlike a voice frame, a late one
    // is still correct.
    pNetworkService->Send(request);

    spdlog::info("[Combat] reported {:.1f} damage to entity {} (zone {})", aDamage, aTargetServerId, aHitZone);
}

void NetworkWorldSystem::HandleAppearanceUpdate(const PacketEvent<server::NotifyAppearanceUpdate>& aMessage)
{
    const auto entity = GetEntityByServerId(aMessage.get_id());

    if (!entity)
    {
        // Nothing to re-dress. Not an error worth shouting about: a player can change
        // clothes while somebody else is still streaming them in, and their spawn carries
        // the current appearance anyway - so the update is redundant rather than lost.
        spdlog::info("[Appearance] update for id {} arrived before its puppet", aMessage.get_id());
        return;
    }

    const auto* pEntityComponent = entity.get<EntityComponent>();
    if (!pEntityComponent)
        return;

    const auto apprSystem = GetAppearanceSystem();
    if (!apprSystem)
        return;

    // An empty blob means the clothing changed and the face did not, which is most
    // updates. Keeping what is already stored is what stops a wardrobe visit from
    // wiping a ripperdoc one.
    Vector<uint8_t> ccstate = aMessage.get_ccstate();

    if (ccstate.empty())
        ccstate = apprSystem->GetEntityCcstate(pEntityComponent->Id);

    Red::DynArray<Red::TweakDBID> items;
    for (const auto id : aMessage.get_equipment())
        items.PushBack(Red::TweakDBID(id));

    // Replace the stored data, then ask the script side to make the game read it again -
    // ApplyAppearance is script-only, and it is what actually re-dresses the puppet.
    apprSystem->AddEntity(pEntityComponent->Id, items, ccstate);

    Red::CallVirtual(apprSystem, "ReapplyAppearance", pEntityComponent->Id);
}

void NetworkWorldSystem::HandleCharacterLoad(const PacketEvent<server::NotifyCharacterLoad>& aMessage)
{
    auto& pos = aMessage.get_position();
    auto& rot = aMessage.get_rotation();

    const auto& settings = Core::Container::Get<NetworkService>()->GetServerSettings();
    const auto cellSize = settings.get_cell_size();
    const auto expectedCellX = cellSize
                                   ? static_cast<int32_t>(std::floor(pos.get_x() / static_cast<float>(cellSize)))
                                   : 0;
    const auto expectedCellY = cellSize
                                   ? static_cast<int32_t>(std::floor(pos.get_y() / static_cast<float>(cellSize)))
                                   : 0;
    if (cellSize == 0 || aMessage.get_world_revision() != 1 ||
        aMessage.get_cell_x() != expectedCellX || aMessage.get_cell_y() != expectedCellY)
    {
        spdlog::warn("[World] dropped map-invalid character load {}", aMessage.get_id());
        return;
    }

    const Red::Vector4 position{pos.get_x(), pos.get_y(), pos.get_z(), 1.f};
    const auto eulerAngles = glm::vec3(0.f, 0.f, rot);
    const auto quat = glm::quat(eulerAngles);

    const Red::Quaternion rotation{quat.x, quat.y, quat.z, quat.w};

    auto equipment = Red::DynArray<Red::TweakDBID>(this->GetAllocator());
    for (auto item : aMessage.get_equipment())
    {
        // The wire carries the ID's numeric value; TweakDBID is a thin wrapper around
        // exactly that, so this is a reinterpretation rather than a lookup. Constructing
        // one from a STRING would hash the text - which is what used to happen, with an
        // empty string, producing an ID that matches no item at all.
        Red::TweakDBID id;
        id.value = item;

        equipment.EmplaceBack(id);
    }

    auto ccstate = aMessage.get_ccstate();

    Spawn(aMessage.get_id(), position, rotation, equipment, ccstate, aMessage.get_is_male(),
          aMessage.get_username().c_str(), aMessage.get_puppet_record().c_str());
}

void NetworkWorldSystem::HandleEntityUnload(const PacketEvent<server::NotifyEntityUnload>& aMessage)
{
    DeSpawn(aMessage.get_id());
}

/**
 * The server has asked this client to make a character.
 *
 * Handed straight to redscript - the creator is script-side machinery, and driving the
 * game's own UI from native would mean reimplementing what redscript can already call.
 */
void NetworkWorldSystem::HandleOpenCharacterCreator(const PacketEvent<server::OpenCharacterCreator>& aMessage)
{
    if (aMessage.get_capture_only())
    {
        spdlog::info("[Character] the server asked for our current appearance");
        SaveCharacterAppearance();
        return;
    }

    spdlog::info("[Character] the server asked us to open the character creator");

    Red::CallVirtual(this, "OpenCharacterCreator");
}

/**
 * The server wants to know what this character is called.
 *
 * Handed to redscript for the same reason as the creator above: the prompt is a UI widget,
 * and the answer travels back out as an ordinary chat command, both of which are script
 * side already.
 */
void NetworkWorldSystem::HandleRequestCharacterName(const PacketEvent<server::RequestCharacterName>& aMessage)
{
    spdlog::info("[Character] the server asked what our character is called");

    Red::CallVirtual(this, "RequestCharacterName", Red::CString(aMessage.get_current().c_str()));
}

void NetworkWorldSystem::HandleNotifyMoney(const PacketEvent<server::NotifyMoney>& aMessage)
{
    m_targetMoney = aMessage.get_balance();

    spdlog::info("[Money] server says the balance is now {} ({})", m_targetMoney,
                 aMessage.get_reason().empty() ? "no reason given" : aMessage.get_reason().c_str());

    // Applied by script, which is where the transaction system lives. Same split as
    // everything else: native holds what came off the wire, script does the work.
    Red::CallVirtual(Red::GetGameSystem<NetworkWorldSystem>(), "ApplyServerMoney");
}

void NetworkWorldSystem::HandleSpawnCharacterResponse(const PacketEvent<server::SpawnCharacterResponse>& aMessage)
{
    // Step logging, because this path went SILENT and silence was all we had.
    //
    // 2026-08-26, 19:38: the client connected, sent its spawn request, and the server
    // answered - its log shows "Puppet da spawned" and "spawns with 130 stored item
    // stack(s)". The client then printed nothing at all from this handler, sat doing its
    // once-a-second appearance poll for seven seconds, and died. Not a hang and not the
    // restore, which never ran. With no line between "Connected" and death there was no
    // way to tell whether this handler was even entered, let alone where it stopped.
    //
    // The last step printed before the log stops names the killer - the same bisection
    // that found the vehicle-mount crash. Cheap: this runs once per join.
    spdlog::info("[SpawnResponse] entered");

    if (!aMessage.has_id())
    {
        spdlog::error("Failed to spawn our character on the server...");
        return;
    }

    spdlog::info("[SpawnResponse] id {:x} - setting remote player id", aMessage.get_id());
    SetRemotePlayerId(aMessage.get_id());
    spdlog::info("[SpawnResponse] remote player id set");

    // Which doors the server says are open.
    //
    // Applied for everybody, not only characters with stored possessions - an unlocked
    // building belongs to the world, not to a character, and a new player should walk
    // into the same city as everyone else.
    spdlog::info("[SpawnResponse] reading world facts");
    m_worldFacts = aMessage.get_facts();

    if (!m_worldFacts.empty())
        spdlog::info("[World] server sent {} fact(s) to apply", m_worldFacts.size());

    spdlog::info("[SpawnResponse] facts done - has_possessions={}", aMessage.get_has_possessions());

    // Put back what this character owns.
    //
    // has_possessions rather than a non-empty list: empty means both "owns nothing" and
    // "the server has never been told", and applying the second would empty somebody's
    // pockets. The server decides which it is; this only obeys.
    if (aMessage.get_has_possessions())
    {
        m_restoreInventory = aMessage.get_inventory();
        m_restoreMoney = aMessage.get_money();
        m_restoreAttributes = aMessage.get_attributes();
        m_restorePerks = aMessage.get_perks();
        m_restoreVehicles = aMessage.get_vehicles();

        spdlog::info("[Inventory] server sent {} stack(s) and {} eddies - will apply once the player exists",
                     m_restoreInventory.size(), m_restoreMoney);

        // Not applied here. This handler runs before the local player's puppet is built,
        // so script would find no player and return. Update retries until there is one.
        // A REPLACEMENT character has nothing to inherit.
        //
        // Ordering problem, seen live: the server answers the spawn with the OLD
        // character's possessions before the client has told it to retire that character -
        // the retire goes out moments later, once the player is standing in the world. So
        // the client was handed the previous character's inventory, perks and cyberware
        // and dutifully restored all of it onto somebody who had just been created. Cam
        // made a Street Kid and arrived with 9 perks, 14 cyberware and Corpo clothes.
        //
        // Retiring earlier is not available: the connection and the world both have to
        // exist first. But the client already knows this is a replacement - the player
        // pressed MULTIPLAYER - NEW CHARACTER - so it can simply decline the inheritance.
        // A brand new character has nothing legitimate to restore.
        //
        // Their real belongings are captured and stored a moment later by the appearance
        // save, which is what makes the new character persistent from that point on.
        if (m_newCharacterPending)
        {
            m_restoreInventory.clear();
            m_restoreAttributes.clear();
            m_restorePerks.clear();
            m_restoreVehicles.clear();
            m_restoreMoney = 0;

            // STILL PENDING, deliberately. Empty the payload, do not skip the phase.
            //
            // The first version set m_restorePending = false here, which looked harmless -
            // there is nothing to restore, so why wait. But that flag is not only a
            // to-do marker: PollEquipmentChanges and the possessions autosave both check
            // it and stand down while it is set, precisely so nothing reads or writes the
            // player while the world is still assembling around them.
            //
            // Clearing it early let the equipment poll start touching a player who had
            // just been created and was still being built, seconds after spawn, and the
            // client died there twice - both times a few appearance polls after a NEW
            // character landed, with the server healthy and no GPU timeout.
            //
            // Leaving it set keeps the normal settle-then-apply sequence exactly as it is.
            // The restore still runs, finds nothing to give, and clears the flag itself on
            // the way out - which is the same path every other character takes.
            m_restorePending = true;

            spdlog::info("[SpawnResponse] NEW character - possessions discarded, restore will run empty");
        }
        else
        {
            m_restorePending = true;
            spdlog::info("[SpawnResponse] possessions buffered - restore armed");
        }
    }
    else
    {
        // Nothing stored yet, so record what they arrived with, immediately.
        //
        // This is the ONLY spawn on which an automatic save is safe, and the reason is
        // ordering. GiveItemByTDBID and EquipRequest are queued, not immediate - a capture
        // running just after a restore reads the inventory BEFORE the server's items have
        // landed, and would then store that as the character. The server's own copy would
        // be overwritten with whatever the local save happened to contain, every single
        // spawn, while the log read "stored 124 item stacks" and looked perfectly healthy.
        //
        // So: a character the server already knows is restored and never captured here.
        // One the server has never seen is captured once, which is exactly the character
        // that would otherwise be lost if they never visited a ripperdoc.
        spdlog::info("[Inventory] nothing stored for this character - recording what they arrived with");

        SaveCharacterAppearance();
    }

    // A character made through NEW CHARACTER is sent up the moment we are in the world.
    //
    // The server only captures an appearance for a player with NO character, so replacing
    // one was impossible: you went through the creator, connected, and were spawned as the
    // character you had just replaced. hyliangenesis built a male V and stayed female for a
    // day - their stored record was created on the 14th at 21:37 and never changed again,
    // through several attempts.
    //
    // Sent from here because this is the first moment the world is real and the player is
    // standing in it as whoever they just built. Reusing SaveCharacterRequest means no new
    // message and no protocol change - the server already overwrites the stored character
    // with what arrives, keeping level and perks.
    //
    // Cleared either way. A failed save must not leave the flag armed, or the next ordinary
    // join would overwrite their character with whatever save happened to load.
    if (m_newCharacterPending)
    {
        m_newCharacterPending = false;

        // Retire the stored character FIRST, through the same command a player could
        // type. The save below reuses SaveCharacterRequest, and the server builds a save
        // by COPYING the stored record - so without the retire, the old character's name
        // and its named-once lock (and SpawnedBefore) ride along onto the replacement,
        // and the "new" character walks out pre-named after the old one. Retiring makes
        // the save arrive to no record at all: fresh name prompt, arrivals spawn, a
        // genuinely new person. Reliable messages on one connection stay ordered, so the
        // retire always lands before the appearance.
        //
        // "/character new confirm", not "/character new". The bare command became a
        // CONFIRMATION PROMPT on 2026-08-26 - typing it while spawned used to retire the
        // character you were standing in, so it now asks first. This caller is not a
        // person who might have fumbled a command; it is the creator flow, which has
        // already had the player press MULTIPLAYER - NEW CHARACTER and walk through the
        // whole creator. The intent is not in doubt here, so it answers the prompt.
        //
        // Missing this is what made a "new character" arrive wearing the old one's
        // clothes: the retire was refused, the save copied the surviving record, and the
        // player was handed back their previous perks, cyberware and inventory on top of
        // whatever their new lifepath gave them.
        {
            client::ChatMessageRequest retire;
            retire.set_message("/character new confirm");
            Core::Container::Get<NetworkService>()->Send(retire);
        }

        spdlog::info("[Character] new character - sending this appearance to replace the stored one");
        SaveCharacterAppearance();
    }
}

static Core::RawFunc<
    1160782872UL,
    bool (*)(Red::game::mounting::MountingFacility *, const Red::ent::Entity &, const Red::game::mounting::MountingSlotId &, bool)>
    IsMountedToObject;
static Core::RawFunc<
    3120376212UL,
    bool (*)(Red::game::mounting::MountingFacility *, const Red::ent::Entity &, const Red::game::mounting::MountingSlotId &, Red::game::mounting::MountingInfo &)>
    GetMountingInfo;

bool NetworkWorldSystem::AcquirePsmBlackboard(const Red::Handle<Red::GameObject>& acPlayer) const
{
    // PlayerStateMachineDef is a SCRIPTED class - none of its members exist in the
    // generated headers, so both the definition object and its field ids are reached
    // through RTTI reflection. CProperty::GetValue handles the script value holder.
    Red::Handle<Red::gamebbAllScriptDefinitions> allDefs;
    if (!Red::CallGlobal("GetAllBlackboardDefs", allDefs) || !allDefs)
        return false;

    auto* pDefProp = allDefs->GetType()->GetProperty("PlayerStateMachine");
    if (!pDefProp)
        return false;

    const auto psmDef = pDefProp->GetValue<Red::Handle<Red::gamebbScriptDefinition>>(allDefs.instance);
    if (!psmDef)
        return false;

    auto* pDefClass = psmDef->GetType();
    auto* pLocProp = pDefClass->GetProperty("Locomotion");
    auto* pUpProp = pDefClass->GetProperty("UpperBody");
    if (!pLocProp || !pUpProp)
        return false;

    m_psmLocomotionId = pLocProp->GetValue<Red::gamebbScriptID_Int32>(psmDef.instance);
    m_psmUpperBodyId = pUpProp->GetValue<Red::gamebbScriptID_Int32>(psmDef.instance);

    auto* pBbSystem = Red::GetGameSystem<Red::game::BlackboardSystem>();
    if (!pBbSystem)
        return false;

    // PSM is a LOCAL-INSTANCED blackboard, keyed by the player entity - plain Get()
    // is for global boards and answers the wrong thing here.
    if (!Red::CallVirtual(pBbSystem, "GetLocalInstanced", m_psmBlackboard, acPlayer->id, psmDef)
        || !m_psmBlackboard)
        return false;

    m_pPsmGetInt = Red::Detail::GetFunction(m_psmBlackboard->GetType(), "GetInt");
    m_psmOwner = acPlayer->id;

    if (m_pPsmGetInt)
        spdlog::info("[PSM] PlayerStateMachine blackboard acquired for player {:x}", acPlayer->id.hash);

    return m_pPsmGetInt != nullptr;
}

void NetworkWorldSystem::ReadPlayerState(const Red::Handle<Red::GameObject>& acPlayer,
                                         uint32_t& aLocomotion, uint32_t& aUpperBody) const
{
    aLocomotion = 0;
    aUpperBody = 0;

    // Re-acquire when the player entity changed (new save, respawn). Failure is
    // ordinary early in a session - try again next tick, send Default meanwhile.
    if (!m_psmBlackboard || m_psmOwner.hash != acPlayer->id.hash)
    {
        if (!AcquirePsmBlackboard(acPlayer))
            return;
    }

    int32_t locomotion = 0;
    int32_t upperBody = 0;
    Red::Detail::CallFunctionWithArgs(m_pPsmGetInt, m_psmBlackboard, locomotion, m_psmLocomotionId);
    Red::Detail::CallFunctionWithArgs(m_pPsmGetInt, m_psmBlackboard, upperBody, m_psmUpperBodyId);

    aLocomotion = static_cast<uint32_t>(locomotion);
    aUpperBody = static_cast<uint32_t>(upperBody);
}

void NetworkWorldSystem::UpdatePlayerLocation() const
{
    const auto system = Red::GetGameSystem<Game::PlayerSystem>();
    Red::Handle<Red::GameObject> player;
    system->GetLocalPlayerControlledGameObject(player);

    if (!player || !GetRemotePlayerId())
        return;

    auto puppet = Red::Cast<Red::game::Puppet>(player);

    if (auto vehicle_id = GetVehicleSystem()->GetVehicleGameId())
    {
        auto vehicle = Red::Cast<Red::vehicle::BaseObject>(GetEntity(*vehicle_id));
        if (!vehicle)
        {
            spdlog::info("Couldn't find vehicle: {}", (*vehicle_id).hash);
            return;
        }
        if (auto remote_id = GetVehicleSystem()->GetVehicleRemoteId(); remote_id != std::nullopt)
        {
            auto transform = Red::WorldTransform();

            transform = vehicle->rigidBody->currentTransform;

            const auto cEntityPosition = transform.Position;
            const auto cEntityRotation = eulerAngles(Game::ToGlm(transform.Orientation));
            float speed = vehicle->rigidBody->velocity.Magnitude();

            common::Vector3 pos;
            pos.set_x(cEntityPosition.x);
            pos.set_y(cEntityPosition.y);
            pos.set_z(cEntityPosition.z);

            common::Vector3 rot;
            rot.set_x(cEntityRotation.x);
            rot.set_y(cEntityRotation.y);
            rot.set_z(cEntityRotation.z);

            client::MoveEntityRequest request;
            request.set_position(pos);
            request.set_full_rotation(rot);
            request.set_id(*remote_id);
            request.set_speed(speed);
            request.set_tick(GetTick());

            // Which grant of authority this movement belongs to. The server drops
            // anything stale, which is what keeps an ex-driver's in-flight packets from
            // fighting the new simulator after a handoff.
            request.set_epoch(GetVehicleSystem()->GetAuthorityEpoch());

            const auto pNetworkService = Core::Container::Get<NetworkService>();
            pNetworkService->Send(request);

            client::MoveEntityRequest characterRequest;
            characterRequest.set_position(pos);
            characterRequest.set_rotation(cEntityRotation.z);
            characterRequest.set_id(*GetRemotePlayerId());
            characterRequest.set_speed(speed);
            characterRequest.set_tick(GetTick());

            pNetworkService->Send(characterRequest);
        }
    }
    else
    {
        // localTransform is not updated as the player walks (V moves via the character
        // controller), so it stays frozen at its spawn value. Rotation already reads
        // worldTransform on the next line - use it for position too.
        const auto cEntityPosition = puppet->placedComponent->worldTransform.Position;
        const auto cEntityRotation = eulerAngles(Game::ToGlm(puppet->placedComponent->worldTransform.Orientation));

        // Speed is MEASURED, not read from the game.
        //
        // This used to be moveComponent->speed.Magnitude() - a field at a hand-mapped
        // offset. That offset moved on 2.31, so it returned garbage around 3e8. The
        // animation state machine compares against 3 m/s to walk and 5 to run, so an
        // absurd value pinned every remote player past both thresholds forever and their
        // animations never matched what they were doing.
        //
        // Distance over time needs no offsets and cannot break on a game update. The
        // numbers are metres per second by construction, which is exactly what the state
        // machine wants.
        const glm::vec3 here{cEntityPosition.x, cEntityPosition.y, cEntityPosition.z};
        const auto now = std::chrono::steady_clock::now();

        float speed = 0.f;

        if (m_hasLastPosition)
        {
            const float elapsed = std::chrono::duration<float>(now - m_lastPositionAt).count();

            // Guard against a zero or absurdly small interval - dividing by it produces
            // an infinity that behaves exactly like the bug this replaces.
            if (elapsed > 0.001f)
            {
                speed = glm::distance(here, m_lastPosition) / elapsed;

                // A teleport is not sprinting. Without this, /tp or a loading screen
                // registers as several hundred metres per second and slams the puppet
                // into a sprint animation on arrival.
                constexpr float kFastestPlausible = 20.f;
                if (speed > kFastestPlausible)
                    speed = 0.f;
            }
        }

        m_lastPosition = here;
        m_lastPositionAt = now;
        m_hasLastPosition = true;

        common::Vector3 pos;
        pos.set_x(cEntityPosition.x);
        pos.set_y(cEntityPosition.y);
        pos.set_z(cEntityPosition.z);

        client::MoveEntityRequest request;
        request.set_position(pos);
        request.set_rotation(cEntityRotation.z);
        request.set_id(*GetRemotePlayerId());
        request.set_speed(speed);
        request.set_tick(GetTick());

        // What V is DOING, not just where V is. Read only on the on-foot path: seated
        // in a vehicle the PSM just holds its last on-foot value, and the vehicle
        // stream already says everything a passenger needs said.
        uint32_t locomotion = 0;
        uint32_t upperBody = 0;
        ReadPlayerState(player, locomotion, upperBody);
        request.set_locomotion(locomotion);
        request.set_upper_body(upperBody);

        const auto pNetworkService = Core::Container::Get<NetworkService>();
        pNetworkService->Send(request);
    }

    // if (GetEntityByServerId(*GetRemotePlayerId()).get_mut<InterpolationComponent>()->Attached)
    // {
    //     auto vehicle_id = GetVehicleSystem()->GetVehicle(player->id);
    //     if (!vehicle_id)
    //     {
    //         spdlog::warn("No vehicle for player({})", player->id.hash);
    //         return;
    //     }
    //     auto entity = GetEntity(vehicle_id);
    //     if (!entity)
    //     {
    //         spdlog::warn("No entity for vehicle({})", vehicle_id.hash);
    //         return;
    //     }
    //     auto vehicle = Red::Cast<Red::vehicle::BaseObject>(entity);
    //     if (!vehicle)
    //     {
    //         spdlog::warn("Entity is not vehicle");
    //         return;
    //     }
    //     else
    //     {
    //         // if (vehicle->placedComponent) {
    //             // entityPosition = vehicle->placedComponent->localTransform.Position;
    //         // } else {
    //             // entityPosition = vehicle->worldTransform.Position;
    //             entityPosition = vehicle->rigidBody->worldPosition;
    //         // }
    //     }
    // }
}

void NetworkWorldSystem::OnInitialize(const RED4ext::JobHandle& aJob)
{
    spdlog::info("[OnInitialize]");

    IGameSystem::OnInitialize(aJob);

    if (Settings::IsDisabled())
        return;

    // Let flecs speak. Nothing was listening, which is why the crashes are silent.
    //
    // flecs reports its own fatal misuse - "invalid move assignment for %s", the illegal
    // ctor/move handlers in addons/cpp/lifecycle_traits.hpp - through ecs_os_api.log_, and
    // this project never set that callback on either half. So the library was formatting a
    // message that names the exact offending component and writing it into a null handler
    // before aborting. Every one of these crashes ends with the client simply gone and the
    // log stopping mid-line, and this is why.
    //
    // The %s in that message is the type name. Routing this means the next occurrence tells
    // us WHICH component is being moved illegally instead of leaving it to be inferred from
    // a faulting address - which has been wrong three times, because the address is where
    // the damage was touched, never where it was done.
    {
        ecs_os_set_api_defaults();
        ecs_os_api_t api = ecs_os_get_api();

        api.log_ = [](int32_t aLevel, const char* acpFile, int32_t aLine, const char* acpMessage)
        {
            const char* file = acpFile ? acpFile : "?";
            const char* msg = acpMessage ? acpMessage : "";

            // Negative levels are errors and fatals in flecs; everything else is chatter we
            // do not want a line of per frame.
            if (aLevel < 0)
                spdlog::error("[flecs] {}:{} {}", file, aLine, msg);
            else if (aLevel == 0)
                spdlog::warn("[flecs] {}", msg);
        };

        // Put flecs on OUR allocator, which is the one built with guard pages.
        //
        // The heap tripwire was watching the wrong heap. ecs_os_set_api_defaults() leaves
        // flecs on the system allocator, so secure mimalloc never covered a single flecs
        // allocation - including the stack cursors the client keeps dying inside. That is
        // why the run of 13:54 crashed with nothing to say for itself: mimalloc was
        // guarding memory that was never involved.
        //
        // Routed here so a corrupted flecs structure runs into a guard page and reports,
        // instead of being read back later as a small integer pretending to be a pointer.
        api.malloc_ = [](ecs_size_t aSize) -> void*
        { return mi_malloc(static_cast<size_t>(aSize)); };

        api.calloc_ = [](ecs_size_t aSize) -> void*
        { return mi_calloc(1, static_cast<size_t>(aSize)); };

        api.realloc_ = [](void* apPtr, ecs_size_t aSize) -> void*
        { return mi_realloc(apPtr, static_cast<size_t>(aSize)); };

        api.free_ = [](void* apPtr) { mi_free(apPtr); };

        ecs_os_set_api(&api);
    }

    // Components that OWN MEMORY, declared before anything can touch one.
    //
    // NetworkWorldSystem IS a flecs::world, and until now the client registered nothing at
    // all - so every component was registered implicitly, by whichever code path happened
    // to touch it first, and a component discovered that way is treated as plain data.
    // flecs then memcpy's it on an archetype move and never runs a destructor.
    //
    // Two of ours cannot survive that. InterpolationComponent holds a List<Timepoint>
    // (std::list, on every networked entity, growing as position updates arrive) and
    // DriverComponent holds a std::shared_ptr. memcpy'ing either duplicates an owning
    // pointer without telling the owner: the list's nodes get freed twice, the shared_ptr
    // refcount goes wrong, and the heap is quietly corrupted. The crash lands later, in
    // whatever unlucky code next walks a free list - which is why nothing logs near it.
    //
    // That is Cam's crash of 27 August: four dumps, all EXCEPTION_ACCESS_VIOLATION inside
    // this DLL at the same instruction, reading [rax+0x12] where rax held 0x11 and 0x13 -
    // small integers being used as pointers, the signature of freed and reused memory
    // rather than a missing null check.
    //
    // This is the same bug the server hit and the server's World.cpp already carries the
    // matching list, ending with the note that anything non-trivial added later belongs on
    // it. The client simply never had one. The tags and POD components are listed too, so
    // the rule is "every component goes here" and nobody has to make the judgement call
    // again after adding a std::string to one of them.
    component<InterpolationComponent>();
    component<DriverComponent>();
    component<EntityComponent>();
    component<SpawningComponent>();

    // Registering a component is not enough on its own - the type has to be one flecs can
    // actually move, or flecs quietly installs ecs_move_illegal/ecs_move_ctor_illegal and
    // aborts the moment an entity carrying it changes archetype:
    //
    //     template <typename T, if_not_t< std::is_move_assignable<T>::value > = 0>
    //     ecs_move_t move() { return ecs_move_illegal; }
    //
    // Asserting it here means a component that cannot survive an archetype change fails the
    // BUILD, naming itself, instead of killing the client seconds after a player spawns and
    // leaving a faulting address that says nothing about which type was at fault.
    static_assert(std::is_move_assignable_v<InterpolationComponent>,
                  "InterpolationComponent must be move assignable or flecs will abort on archetype change");
    static_assert(std::is_move_constructible_v<InterpolationComponent>,
                  "InterpolationComponent must be move constructible");
    static_assert(std::is_move_assignable_v<DriverComponent>,
                  "DriverComponent must be move assignable or flecs will abort on archetype change");
    static_assert(std::is_move_constructible_v<DriverComponent>,
                  "DriverComponent must be move constructible");
    static_assert(std::is_move_assignable_v<EntityComponent>,
                  "EntityComponent must be move assignable or flecs will abort on archetype change");
    static_assert(std::is_move_constructible_v<EntityComponent>,
                  "EntityComponent must be move constructible");
    static_assert(std::is_move_assignable_v<SpawningComponent>,
                  "SpawningComponent must be move assignable or flecs will abort on archetype change");
    static_assert(std::is_move_constructible_v<SpawningComponent>,
                  "SpawningComponent must be move constructible");
    static_assert(std::is_default_constructible_v<InterpolationComponent> &&
                  std::is_default_constructible_v<DriverComponent> &&
                  std::is_default_constructible_v<EntityComponent> &&
                  std::is_default_constructible_v<SpawningComponent>,
                  "flecs default-constructs components when an entity moves between archetypes");

    const auto pNetworkService = Core::Container::Get<NetworkService>();
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleCharacterLoad>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleEntityUnload>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleTeleport>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleSpawnCharacterResponse>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleOpenCharacterCreator>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleNotifyMoney>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleRequestCharacterName>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleWorldState>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleInteraction>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleAppearanceUpdate>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleVoiceFrame>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleStatusEffect>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleDamageResult>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleCombatState>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleQuickhackUpload>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleVehicleControlAssigned>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleCharacterList>(this);

    m_remotePlayerId = std::nullopt;

    m_pCreatePuppet = Red::Detail::GetFunction(GetType(), "CreatePuppet");
    m_pDeletePuppet = Red::Detail::GetFunction(GetType(), "DeletePuppet");

    m_interpolationSystem = RED4ext::MakeHandle<InterpolationSystem>();
    m_interpolationSystem->OnInitialize(aJob);

    m_appearanceSystem = RED4ext::MakeHandle<AppearanceSystem>();
    m_appearanceSystem->OnInitialize(aJob);

    m_chatSystem = RED4ext::MakeHandle<ChatSystem>();
    m_chatSystem->OnInitialize(aJob);

    m_vehicleSystem = RED4ext::MakeHandle<VehicleSystem>();
    m_vehicleSystem->OnInitialize(aJob);
}

void NetworkWorldSystem::Connect()
{
    auto address = fmt::format("{}:{}", Settings::Get().ip, Settings::Get().port);

    // Log the address we actually dial. The launch arguments must use the
    // --ip=<addr> --port=<n> form; anything else silently leaves these at their
    // defaults (127.0.0.1:11778) and the connection times out against your own PC.
    spdlog::info("Connecting to {}", address);

    // A denial describes the attempt it refused, never the one about to be made.
    ClearConnectionDenial();

    Core::Container::Get<NetworkService>()->Connect(address);
}

void NetworkWorldSystem::SetConnectionDenial(const uint32_t aCode, const std::string& acMessage,
                                             const std::string& acRequiredManifest)
{
    m_denialCode = aCode;
    m_denialMessage = acMessage;
    m_requiredManifest = acRequiredManifest;
}

void NetworkWorldSystem::ClearConnectionDenial()
{
    m_denialCode = 0;
    m_denialMessage.clear();
    m_requiredManifest.clear();
}

void NetworkWorldSystem::Disconnect()
{
    // Save on the way out, before the socket closes.
    //
    // The ninety-second timer is a backstop, not a guarantee: somebody who buys a gun and
    // leaves twenty seconds later would have bought nothing, and would be handed the older
    // inventory on their next join - which reads exactly like the server eating their
    // money. Nobody should have to know a command exists to keep what they earned.
    //
    // Before Close(), obviously - there is no sending anything afterwards. This only
    // covers a deliberate disconnect; a crash or a pulled cable still falls back to the
    // timer, which is why the timer exists as well as this.
    const auto& service = Core::Container::Get<NetworkService>();

    if (service && service->IsConnected() && !m_restorePending)
    {
        spdlog::info("[Inventory] saving on disconnect");
        SaveCharacterAppearance(true);
    }

    Core::Container::Get<NetworkService>()->Close();
}

void NetworkWorldSystem::OnConnected()
{
    RED4ext::StackArgs_t args;
    ExecuteFunction(this, this->GetNativeType()->GetFunction("OnConnected"), nullptr, args);

    // Voice starts with the session, not with the first key press.
    //
    // Opening a microphone and a render endpoint takes long enough to be heard as a clipped
    // first word if it happened when somebody pressed talk. Starting here means the audio
    // path is already warm and push-to-talk only flips a flag.
    //
    // Devices and volumes come from the launcher (Settings::Load). Empty ids follow the
    // Windows defaults, which is what somebody means by not having chosen.
    {
        const auto& settings = Settings::Get();

        m_voiceClient.SetMicVolume(settings.voiceMicVolume);
        m_voiceClient.SetPlaybackVolume(settings.voiceChatVolume);

        if (!m_voiceClient.Start(settings.voiceInputDevice.c_str(), settings.voiceOutputDevice.c_str()))
        {
            // Never fatal. Somebody with no working audio device still gets to play - they
            // just cannot talk, and the log says why rather than leaving it a mystery.
            spdlog::warn("[Voice] not available this session: {}", m_voiceClient.GetLastError());
        }
    }

    const auto pNetworkService = Core::Container::Get<NetworkService>();

    m_updatePlayerLocation = system("Update player location")
        .kind(flecs::OnUpdate)
        .interval(1.f / pNetworkService->GetServerSettings().get_update_rate())
        .run([this](flecs::iter& it)
        {
            UpdatePlayerLocation();
        });

    // Once a second is plenty - somebody adjusting their face is not in a hurry, and this
    // is a null check until they actually are at a mirror.
    m_updateAppearance = system("Appearance watch")
        .interval(1.f)
        .run([this](flecs::iter& it)
        {
            PollAppearanceChanges();
            PollEquipmentChanges();
        });

    // Save what the character owns on a timer, so nobody has to remember a command.
    //
    // Possessions changed constantly - every purchase, every pickup, every sale - and
    // until now the only things that stored them were /character save and a ripperdoc
    // visit. A player who bought a gun and logged off had bought nothing, and would be
    // handed the older inventory back on their next join, which reads as the server
    // eating their money.
    //
    // Ninety seconds is a compromise, not a computed figure: a full capture walks the
    // whole inventory and sends it, so once a second would be waste, and once every ten
    // minutes loses a real session's shopping to a crash.
    m_updatePossessions = system("Possessions autosave")
        .interval(90.f)
        .run([this](flecs::iter& it)
        {
            const auto& service = Core::Container::Get<NetworkService>();
            if (!service || !service->IsConnected())
                return;

            // Never while a restore is outstanding.
            //
            // A capture running before the server's items have landed reads the pre-restore
            // inventory and stores THAT, overwriting the server's copy with a stale one -
            // and it would do it every ninety seconds, quietly, with a healthy-looking
            // number in the log. This is the same race the first-spawn save had to avoid.
            if (m_restorePending)
                return;

            SaveCharacterAppearance(true);
        });

    m_updateSpawningEntities = system<SpawningComponent>("Spawning entity process")
        .interval(0.2f)
        .write<EntityComponent>()
        .write<SpawningComponent>()
        .each([this](flecs::entity aEntity, SpawningComponent& aSpawning)
        {
            const auto pEntity = GetEntity(aSpawning.Id);

            if (!pEntity)
                return;

            if (const auto pOwner = Red::Cast<Red::GameObject>(pEntity))
            {
                if (pOwner->tags.Contains("CyberpunkMP.Vehicle"))
                    return;

                aEntity.emplace<EntityComponent>(aSpawning.Id, false, aSpawning.Controller);

                // Driver puppets get their animation writer bound here, on the main
                // thread, with no engine hook involved - the driver re-binds itself if
                // a mount rebuilds the component later.
                if (aSpawning.UsesDriver)
                {
                    auto pDriver = std::make_shared<PuppetDriver>();
                    pDriver->EnsureAttached(pEntity.GetPtr(), aEntity.raw_id());
                    aEntity.set<DriverComponent>({std::move(pDriver)});
                }

                aEntity.remove<SpawningComponent>();

                pOwner->tags.Add("CyberpunkMP.Puppet");
            }
        });

    m_interpolationSystem->OnConnected();
}

void NetworkWorldSystem::OnDisconnected(Client::EDisconnectReason aReason)
{
    // Release the microphone and the speakers before anything else. Leaving a capture
    // thread running after a disconnect would hold somebody's headset open for a session
    // that no longer exists - and on a reconnect, try to open it twice.
    m_voiceClient.Stop();

    each([this](flecs::entity entity, EntityComponent&)
        {
            DeSpawn(entity.raw_id());
            entity.destruct();
        });

    each([this](flecs::entity entity, SpawningComponent&)
        {
            DeSpawn(entity.raw_id());
            entity.destruct();
        });

    App::PuppetRegistry::Clear();
    App::PuppetRegistry::ClearDrivers();

    if (m_updatePlayerLocation)
        m_updatePlayerLocation.destruct();

    if (m_updateSpawningEntities)
        m_updateSpawningEntities.destruct();

    if (m_updateAppearance)
        m_updateAppearance.destruct();

    m_interpolationSystem->OnDisconnected();
    m_vehicleSystem->OnDisconnected();

    // The session id dies with the session. Only world detach cleared it before, so a
    // disconnect with the world still attached (server restart mid-play) left the old
    // id behind - and the reconnect streamed it at the new session, which the server
    // rejected packet by packet as an invalid entity: a player nobody could see move.
    m_remotePlayerId = std::nullopt;

    // Do not hold a strong blackboard handle across sessions; the next session
    // re-acquires against whatever player entity it actually has.
    m_psmBlackboard = {};
    m_psmOwner = {};
    m_pPsmGetInt = nullptr;

    RED4ext::StackArgs_t args;
    auto reason = (uint32_t)aReason;
    args.emplace_back(RED4ext::CRTTISystem::Get()->GetType("Uint32"), &reason);
    ExecuteFunction(this, this->GetNativeType()->GetFunction("OnDisconnected"), nullptr, args);
}
