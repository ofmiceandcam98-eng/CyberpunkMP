#pragma once

#include "Base.h"
#include "Game/Animation/Base.h" // Locomotion_Style

namespace States
{
struct Jogging : Base
{
    Jogging(ILocomotionHost& aParent)
        : Base(aParent)
    {
    }

    ~Jogging() override {}
    void Enter() noexcept override;
    void GetAnimationData(AnimationData& aData) const override;

    std::optional<Transition> Process(const Update& acEvent) noexcept override;

private:

    float m_timer = 0.f;
    float m_duration = 0.f;
    Locomotion_Style m_style{LS_Jog};
};
} // namespace States
