#include "Walking.h"

#include "Idling.h"
#include "Jogging.h"
#include "Sprinting.h"

#include "Game/Animation/AnimationData.h"
#include "Game/Animation/MultiMovementController.h"

namespace States
{
void Walking::Enter() noexcept
{
    m_timer = 0.f;
    m_duration = m_parent.GetAnimLength("walk_0");
}

void Walking::GetAnimationData(AnimationData& aData) const
{
    // No walk clip on this template (the male puppet ships almost no locomotion
    // anims). Demanding a clip that does not exist does not just fail to animate -
    // it stalls the motion pipeline and the puppet FREEZES in place, which read as
    // "we can't see each other move" in live testing. Degrade to the pre-state
    // behaviour instead: idle pose, position still slides. Templates that have the
    // clip (the female puppet) animate properly.
    if (m_duration <= 0.f)
    {
        Base::GetAnimationData(aData);
        return;
    }

    aData.action = MTA_Move;
    aData.style = LS_Walk;
    aData.time = m_timer;
    // The graph's blend parameter. It was never written by any state - permanently
    // zero - which flattened every locomotion blend toward the idle pose.
    aData.speed = m_parent.GetCurrentSpeed();
}

std::optional<Base::Transition> Walking::Process(const Update& acEvent) noexcept
{
    if (acEvent.Speed >= kSprintSpeed)
        return Transit<Sprinting>();
    if (acEvent.Speed >= kJogSpeed)
        return Transit<Jogging>();
    if (acEvent.Speed < kWalkSpeed)
        return Transit<Idling>();

    m_timer += acEvent.Delta;
    // See Jogging::Process - a missing clip must not fmodf by zero.
    m_timer = m_duration > 0.f ? std::fmodf(m_timer, m_duration) : 0.f;

    return std::nullopt;
}
}
