#pragma once

struct AnimationData;

namespace States
{
// What a state actually needs from whoever hosts it. Two hosts exist: the legacy
// MultiMovementController (engine-attached, NPC records only - via an adapter member,
// because its own vtable is an ENGINE ABI and must not gain a base class) and
// PuppetDriver (mod-owned, record-agnostic).
struct ILocomotionHost
{
    virtual ~ILocomotionHost() = default;
    virtual float GetAnimLength(Red::CName aName) const = 0;
    virtual float GetCurrentSpeed() const = 0;
};

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

    Base(ILocomotionHost& aParent)
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
    ILocomotionHost& m_parent;
};
}

