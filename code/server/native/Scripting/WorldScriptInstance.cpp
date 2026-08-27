#include "WorldScriptInstance.h"

#include "GameServer.h"

TP_EXPORT IWorld* IWorld::Get()
{
    return GServer->GetWorld()->GetScriptInstance();
}

void WorldScriptInstance::SetUpdateCallback(TUpdateCallback apCallback)
{
    m_callback = apCallback;
}

WorldScriptInstance::WorldScriptInstance()
{

}

WorldScriptInstance::~WorldScriptInstance()
{
}

void WorldScriptInstance::Initialize()
{
    m_updateSystem = GServer->GetWorld()->system("Script Update")
    .kind(flecs::OnUpdate)
    .run([this](flecs::iter& iter)
    {
        // delta_time is read BEFORE draining, deliberately - once the iterator finalises,
        // reading it back is no longer safe.
        const float delta = iter.delta_time();

        // Drained for the same reason as the world clock: an un-iterated .run() iterator is
        // never finalised and leaks its flecs stack cursor, every frame. See WorldClock.cpp
        // for what that leak eventually does.
        while (iter.next())
        {
        }

        if (m_callback != nullptr)
            m_callback(delta);
    });

    m_updateSystem.child_of(GServer->GetWorld()->entity("systems"));
}

