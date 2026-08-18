#pragma once

struct MultiMovementController;

namespace States
{
struct Base
{
    // Bands around the game's REAL locomotion speeds: walk ~1.8 m/s, jog ~5.5,
    // sprint ~7.5+. kWalkSpeed sat at 3.0 for the project's whole life - above actual
    // walking speed - so a walking player never left Idling and appeared to glide.
    static inline float kWalkSpeed = 1.2f;
    static inline float kJogSpeed = 4.f;
    static inline float kSprintSpeed = 6.5f;

    struct Update
    {
        float Delta;
        float Speed;
    };

    struct Jump
    {
    };

    struct Move
    {
    };

    struct Idle
    {
    };

    struct Transition
    {
        UniquePtr<Base> State;
    };

    Base(MultiMovementController& aParent)
        : m_parent(aParent)
    {
    }

    virtual ~Base(){}
    virtual void Enter() noexcept {}
    virtual void Exit() noexcept {}

    virtual void GetAnimationData(AnimationData& aData) const;

    [[nodiscard]] virtual std::optional<Transition> Process(const Update& acEvent) noexcept;
    [[nodiscard]] virtual std::optional<Transition> Process(const Jump& acEvent) noexcept { return std::nullopt; }
    [[nodiscard]] virtual std::optional<Transition> Process(const Move& acEvent) noexcept { return std::nullopt; }
    [[nodiscard]] virtual std::optional<Transition> Process(const Idle& acEvent) noexcept { return std::nullopt; }

    template <class T> std::optional<Transition> Transit() noexcept
    {
        Exit();
        auto pNewState = MakeUnique<T>(m_parent);
        pNewState->Enter();

        return Transition{std::move(pNewState)};
    }

protected:
    MultiMovementController& m_parent;
};
}

