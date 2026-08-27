#include "World.h"

#include "Config.h"
#include "PlayerManager.h"
#include "Level.h"
#include "WorldClock.h"

#include "Components/MovementComponent.h"
#include "Components/PlayerComponent.h"
#include "Components/AttachmentComponent.h"
#include "Components/VehicleComponent.h"
#include "Components/AuthorityComponent.h"
#include "Components/AppearanceComponent.h"
#include "Components/NpcComponent.h"

#include "Systems/ChatSystem.h"
#include "Systems/NpcSystem.h"
#include "Systems/ServerListSystem.h"

World::World(const FlecsConfig& acFlecsConfig)
{
    set_entity_range(1, 5'000'000);

    emplace<Level>(this);
    emplace<PlayerManager>(this);
    emplace<WorldClock>(this);
    emplace<ChatSystem>(this);
    emplace<NpcSystem>(this);
    emplace<ServerListSystem>(this);

    if (acFlecsConfig.IsEnabled())
    {
        set<flecs::Rest>({
            .port = acFlecsConfig.GetPort(),
            .ipaddr = const_cast<char*>(acFlecsConfig.GetIpAddress()),
            .impl = nullptr
        });
        spdlog::info("Running Flecs REST API on {}:{}", acFlecsConfig.IpAddress, acFlecsConfig.Port);
    }

    this->import<flecs::units>();
    this->import<flecs::stats>();

    component<std::string>()
        .opaque(flecs::String)
        .serialize(
            [](const flecs::serializer* s, const std::string* data)
            {
                const char* str = data->c_str();
                return s->value(flecs::String, &str); // Forward to serializer
            })
        .assign_string(
            [](std::string* data, const char* value)
            {
                *data = value; // Assign new value to std::string
            });

    MovementComponent::Register(*this);
    AttachmentComponent::Register(*this);
    PlayerComponent::Register(*this);
    VehicleComponent::Register(*this);
    AuthorityComponent::Register(*this);

    // Components that OWN MEMORY must be declared here, before anything uses them.
    //
    // These two have no Register() of their own and were never named here, so flecs only
    // learned about them the first time something happened to touch one - and a component
    // discovered that way is registered as plain data. AppearanceComponent holds two
    // Vectors and NpcComponent two std::strings, none of which can be memcpy'd, so the
    // moment an entity carrying one changed archetype flecs called a constructor it had
    // marked illegal and killed the server.
    //
    // That is the ecs_ctor_illegal crash in Level::Add <- AddPlayer <-
    // HandleSpawnCharacterRequest: adding LevelActorTag moves the player between
    // archetypes, and the player carries an AppearanceComponent.
    //
    // It was intermittent for the worst possible reason - whichever code path touched the
    // type first decided how it got registered, so it depended on timing rather than on
    // anything anyone changed. It survived a full day of being blamed on other things.
    //
    // Naming them here is the whole fix: flecs::world::component<T>() registers the real
    // C++ type and installs proper construct/destruct/move hooks. Anything added later
    // with a non-trivial member belongs on this list too.
    component<AppearanceComponent>();
    component<NpcComponent>();
}

World::~World()
{
}

void World::Update(float aDelta)
{
    progress(aDelta);
}

gsl::not_null<WorldScriptInstance*> World::GetScriptInstance() noexcept
{
    return &m_scriptInstance;
}

gsl::not_null<const WorldScriptInstance*> World::GetScriptInstance() const noexcept
{
    return &m_scriptInstance;
}
