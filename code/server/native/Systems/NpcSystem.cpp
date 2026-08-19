#include "NpcSystem.h"
#include "Game/World.h"
#include "Game/Level.h"

#include "Components/MovementComponent.h"
#include "Components/AppearanceComponent.h"
#include "Components/NpcComponent.h"

#include "Core/Filesystem.h"

#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

NpcSystem::NpcSystem(gsl::not_null<World*> apWorld)
    : m_pWorld(apWorld)
    , m_statePath(GetPath() / "config" / "npcs.json")
{
    Load();
}

flecs::entity NpcSystem::Spawn(const std::string& acRecord, const std::string& acName,
                               const glm::vec3& acPosition, float aYaw) noexcept
{
    auto entity = m_pWorld->entity()
                      .set<MovementComponent>({acPosition, {0.f, 0.f, aYaw}, {}})
                      .set<AppearanceComponent>({})
                      .set<NpcComponent>({acRecord, acName});

    // The same door players walk through: Add tags it as a level actor, broadcasts the
    // load to everyone online, and the join-time sweep hands it to everyone who comes
    // later. One declaration, many renderers.
    if (auto* pLevel = m_pWorld->get_mut<Level>())
        pLevel->Add(entity);

    spdlog::info("NPC '{}' ({}) declared at ({:.1f}, {:.1f}, {:.1f})", acName, acRecord,
                 acPosition.x, acPosition.y, acPosition.z);

    return entity;
}

size_t NpcSystem::Clear() noexcept
{
    Vector<flecs::entity> npcs;

    m_pWorld->each([&npcs](flecs::entity aEntity, const NpcComponent&) { npcs.push_back(aEntity); });

    auto* pLevel = m_pWorld->get_mut<Level>();
    for (auto npc : npcs)
    {
        if (pLevel)
            pLevel->Remove(npc); // broadcasts the unload, then destructs
    }

    Save();

    return npcs.size();
}

size_t NpcSystem::Count() const noexcept
{
    size_t count = 0;
    m_pWorld->each([&count](flecs::entity, const NpcComponent&) { ++count; });
    return count;
}

void NpcSystem::Save() noexcept
{
    try
    {
        json list = json::array();

        m_pWorld->each(
            [&list](flecs::entity aEntity, const NpcComponent& aNpc)
            {
                const auto* pMovement = aEntity.get<MovementComponent>();
                if (!pMovement)
                    return;

                list.push_back({
                    {"record", aNpc.Record},
                    {"name", aNpc.Name},
                    {"x", pMovement->Position.x},
                    {"y", pMovement->Position.y},
                    {"z", pMovement->Position.z},
                    {"yaw", pMovement->Rotation.z},
                });
            });

        std::ofstream file(m_statePath);
        file << list.dump(2);
    }
    catch (const std::exception& e)
    {
        spdlog::warn("could not save npcs.json: {}", e.what());
    }
}

void NpcSystem::Load() noexcept
{
    try
    {
        std::ifstream file(m_statePath);
        if (!file.is_open())
            return; // no NPCs declared yet

        const auto list = json::parse(file);
        for (const auto& entry : list)
        {
            Spawn(entry.value("record", std::string{}), entry.value("name", std::string{"NPC"}),
                  {entry.value("x", 0.f), entry.value("y", 0.f), entry.value("z", 0.f)},
                  entry.value("yaw", 0.f));
        }

        if (!list.empty())
            spdlog::info("{} persistent NPC(s) restored", list.size());
    }
    catch (const std::exception& e)
    {
        spdlog::warn("npcs.json unreadable ({}) - no NPCs restored", e.what());
    }
}
