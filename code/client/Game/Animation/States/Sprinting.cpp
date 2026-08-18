#include "Sprinting.h"

#include "Idling.h"
#include "Jogging.h"
#include "Walking.h"

#include "Game/Animation/AnimationData.h"
#include "Game/Animation/MultiMovementController.h"

namespace States
{
void Sprinting::Enter() noexcept
{
    m_timer = 0.f;
    m_duration = m_parent.GetAnimLength("sprint_0");
}

void Sprinting::GetAnimationData(AnimationData& aData) const
{
    aData.action = MTA_Move;
    aData.style = LS_Sprint;
    aData.time = m_timer;
    aData.speed = m_parent.m_speed;
}

std::optional<Base::Transition> Sprinting::Process(const Update& acEvent) noexcept
{
    if (acEvent.Speed < kSprintSpeed)
    {
        if (acEvent.Speed >= kJogSpeed)
            return Transit<Jogging>();
        if (acEvent.Speed >= kWalkSpeed)
            return Transit<Walking>();
        return Transit<Idling>();
    }

    m_timer += acEvent.Delta;
    // See Jogging::Process - a missing clip must not fmodf by zero.
    m_timer = m_duration > 0.f ? std::fmodf(m_timer, m_duration) : 0.f;

    return std::nullopt;
}
} // namespace States
