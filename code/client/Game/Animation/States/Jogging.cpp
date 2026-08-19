#include "Jogging.h"

#include "Idling.h"
#include "Sprinting.h"
#include "Walking.h"

#include "Game/Animation/AnimationData.h"
#include "Game/Animation/MultiMovementController.h"

namespace States
{
void Jogging::Enter() noexcept
{
    m_timer = 0.f;
    m_duration = m_parent.GetAnimLength("jog_0");
}

void Jogging::GetAnimationData(AnimationData& aData) const
{
    // See Walking::GetAnimationData - a missing clip must degrade to the idle glide,
    // not stall the motion pipeline into a frozen puppet.
    if (m_duration <= 0.f)
    {
        Base::GetAnimationData(aData);
        return;
    }

    aData.action = MTA_Move;
    aData.style = LS_Jog;
    aData.time = m_timer;
    aData.speed = m_parent.GetCurrentSpeed();
}

std::optional<Base::Transition> Jogging::Process(const Update& acEvent) noexcept
{
    if (acEvent.Speed >= kSprintSpeed)
        return Transit<Sprinting>();
    if (acEvent.Speed < kWalkSpeed)
        return Transit<Idling>();
    if (acEvent.Speed < kJogSpeed)
        return Transit<Walking>();

    m_timer += acEvent.Delta;
    // GetAnimLength answers 0 when the template lacks the clip (the male puppet is
    // missing most locomotion anims today) - fmodf by zero is NaN, and a NaN timer
    // poisons the whole graph. Hold at zero instead: wrong pose, sane graph.
    m_timer = m_duration > 0.f ? std::fmodf(m_timer, m_duration) : 0.f;

    return std::nullopt;
}
} // namespace States
