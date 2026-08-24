#include "PuppetDriver.h"

#include "AnimationData.h"
#include "States/Spawning.h"

void PuppetDriver::EnsureAttached(Red::Entity* apEntity, uint64_t aServerId)
{
    if (!apEntity)
        return;

    // Alive component = attached and healthy. The weak handle expiring is exactly the
    // signal that the engine rebuilt the puppet's components (mounts do this) - the
    // failure class that permanently killed the old controller path.
    if (!m_animationDriver.component.Expired())
        return;

    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastAttachTry < std::chrono::seconds(1))
        return;
    m_lastAttachTry = now;

    m_animationDriver.Attach(apEntity);

    if (m_animationDriver.component.Expired())
    {
        spdlog::warn("[PuppetDriver] puppet {:x} has no animation controller component yet - will retry",
                     aServerId);
        return;
    }

    if (!m_pState)
    {
        m_pState = MakeUnique<States::Spawning>(*this);
        m_pState->Enter();
    }

    // The clip-hole detector: 0 means this template ships no walk clip and the puppet
    // will idle-glide (the states degrade on purpose). Says so once, at attach, so a
    // template problem is one grep away instead of a mystery.
    spdlog::info("[PuppetDriver] attached to puppet {:x} ({}) - walk_0 length {:.2f}s", aServerId,
                 m_everAttached ? "re-attach" : "first", m_animationDriver.GetAnimLength("walk_0"));

    m_everAttached = true;
}

void PuppetDriver::Detach()
{
    m_animationDriver.Detach();
    m_pState.reset();
}

void PuppetDriver::Tick(float aDeltaSeconds, float aSpeed, uint32_t aLocomotion)
{
    if (!m_pState || m_animationDriver.component.Expired())
        return;

    m_speed = aSpeed;

    States::Base::Update update{aDeltaSeconds, States::Base::BandSpeed(aSpeed, aLocomotion)};
    while (auto transition = m_pState->Process(update))
    {
        m_pState = std::move(transition->State); // NOLINT(bugprone-unchecked-optional-access)
    }

    AnimationData data;
    data.controller = NAME;
    m_pState->GetAnimationData(data);
    m_animationDriver.SendParameters(data);
}

float PuppetDriver::GetAnimLength(Red::CName aName) const
{
    return m_animationDriver.GetAnimLength(aName);
}
