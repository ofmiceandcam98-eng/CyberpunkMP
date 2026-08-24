#pragma once

#include "AnimationDriver.h"
#include "RED4ext/Scripting/Natives/Generated/Vector4.hpp"
#include "RED4ext/Scripting/Natives/Generated/Quaternion.hpp"
#include "States/Base.h"

namespace RED4ext
{
namespace move
{
struct Component;
}
}

struct AnimationData;

// implements IdleController-like interface
struct MultiMovementController
{
    enum Type
    {
        kMulti = 1 << 10
    };

    using AllocatorType = RED4ext::Memory::AI_MovementAllocator;
    static inline Red::CName NAME{"Multiplayer Controller"};

    virtual RED4ext::Memory::PoolAI_Movement* GetMemoryPool();
    virtual ~MultiMovementController();
    virtual void PreTick(float delta);
    virtual void Tick(float delta);
    virtual void GetDeltaTransform(Red::Vector4& positionDelta, Red::Quaternion& rotationDelta);
    virtual void sub_28(bool& unk);
    virtual void sub_30(Red::Vector4& positionDelta, Red::Quaternion& rotationDelta);
    virtual void sub_38(Red::Vector4& position, Red::Quaternion& rotation, bool& unk1, bool& unk2);
    virtual void SendAnimationParameters(float delta, Red::Vector4& position, Red::Quaternion& orientation);
    virtual void sub_48(RED4ext::Vector4& somePosition1, RED4ext::Quaternion& someRotation1, RED4ext::Vector4& somePosition2, RED4ext::Quaternion& someRotation2);
    virtual void sub_50(RED4ext::Vector4& somePosition1, RED4ext::Quaternion& someRotation1, float unk, RED4ext::Vector4& somePosition2, RED4ext::Quaternion& someRotation2);
    virtual Red::CName GetName() const;
    virtual bool sub_60();
    virtual void sub_68();
    virtual void sub_70();
    virtual void Mount(RED4ext::move::Component& movable, Red::Handle<Red::ent::Entity> owner);
    virtual void Attach(Red::move::Component& movable);
    virtual void Detach(Red::move::Component& movable);
    virtual void GetAnimationParameters(AnimationData& animationData);

    void SetTransform(const Red::Vector4& aPosition, float angle, float speed, uint32_t aLocomotion = 0);

    float GetAnimLength(Red::CName aName) const;

    // States reach their host through this adapter member instead of this class
    // inheriting ILocomotionHost: the controller's OWN vtable is an engine ABI (the
    // idle-controller slots, called by index) and any base class would reorder it.
    struct Host final : States::ILocomotionHost
    {
        explicit Host(MultiMovementController* apParent) : Parent(apParent) {}
        float GetAnimLength(Red::CName aName) const override;
        float GetCurrentSpeed() const override;
        MultiMovementController* Parent;
    };

    void Reset();

    Type m_type = kMulti;
    bool m_attached = false;
    bool m_highPriotiry = false;
    Red::move::Component* m_pComponent;
    Red::ent::EntityID m_id;
    Red::Vector4 m_position;
    float m_angle;
    float m_speed = 0.f;
    // Sender's gamePSMLocomotionStates value, written from the main thread by
    // SetTransform and read on the animation thread by Tick. An aligned 32-bit
    // store/load cannot tear on x64, matching how m_speed already crosses.
    uint32_t m_locomotion = 0;
    AnimationDriver m_animationDriver;
    UniquePtr<States::Base> m_pState;
    Host m_host{this};
    // One-shot diagnostic flags for the vehicle-passenger crash hunt: each condition
    // logs once per controller, on the animation thread, then stays quiet.
    bool m_nullPlacementLogged = false;
    bool m_runawayLogged = false;
};
