#include "MultiMovementController.h"

#include "AnimationData.h"
#include "Game/Utils.h"

#include <RED4ext/Scripting/Natives/moveComponent.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimFeature_NPCExploration.hpp>

#include "States/Spawning.h"

Core::RawFunc<316482401, void (*)(Red::AnimationControllerComponent* AnimationController, Red::CName Name, Red::Handle<Red::anim::AnimFeature> Feature)> ApplyFeature;

RED4ext::Memory::PoolAI_Movement* MultiMovementController::GetMemoryPool()
{
    return RED4ext::Memory::PoolAI_Movement::Get();
}

MultiMovementController::~MultiMovementController() = default;

void MultiMovementController::PreTick(float delta)
{
}

void MultiMovementController::Tick(float delta)
{
    States::Base::Update update;
    update.Delta = delta;
    update.Speed = m_speed;

    while (auto transition = m_pState->Process(update))
    {
        m_pState = std::move(transition->State);  // NOLINT(bugprone-unchecked-optional-access)
    }
}

void MultiMovementController::GetDeltaTransform(Red::Vector4& positionDelta, Red::Quaternion& rotationDelta)
{
    // Runs every animation frame on the ANIMATION thread, and mounting a puppet into a
    // vehicle REPARENTS its entity - the placement chain below can go away mid-seat.
    // These were unchecked dereferences until a driver's game died ~25s after a remote
    // puppet mounted into their moving car; a zero delta is always a safe answer.
    if (!m_pComponent || !m_pComponent->owner || !m_pComponent->owner->placedComponent)
    {
        if (!m_nullPlacementLogged)
        {
            m_nullPlacementLogged = true;
            spdlog::warn("[MultiMovement] GetDeltaTransform with missing placement chain - entity reparented under us (mounted?)");
        }

        positionDelta = {0.f, 0.f, 0.f, 0.f};
        rotationDelta = {0.f, 0.f, 0.f, 1.f};
        return;
    }

    const auto& rawPosition = m_pComponent->owner->placedComponent->localTransform.Position;
    const auto& rawRotation = m_pComponent->owner->placedComponent->localTransform.Orientation;
    const glm::vec3 pos = Game::ToGlm(rawPosition);
    const auto rot = Game::ToGlm(rawRotation);

    auto angles = eulerAngles(rot);
    angles.z = m_angle - angles.z;

    const Red::Vector4 position{pos.x, pos.y, pos.z, 0.f};

    positionDelta = {m_position.X - position.X, m_position.Y - position.Y, m_position.Z - position.Z, 0.f};
    rotationDelta = Game::ToRed(glm::quat(angles));

    // Interpolation stops feeding SetTransform while a puppet is seated (AttachedComponent
    // gates it), so m_position freezes at the mount spot. If the engine still asks this
    // controller for deltas while the car drives away, the answer grows without bound -
    // exactly the kind of impulse that hurls entities. Log the first sighting; the
    // timestamp against the mount line names the guilty tick.
    const float dx = positionDelta.X, dy = positionDelta.Y, dz = positionDelta.Z;
    if (dx * dx + dy * dy + dz * dz > 100.f * 100.f && !m_runawayLogged)
    {
        m_runawayLogged = true;
        spdlog::warn("[MultiMovement] delta runaway ({:.0f}m) - frozen target queried while the entity moves (seated in a driving car?)",
                     std::sqrt(dx * dx + dy * dy + dz * dz));
    }
}

void MultiMovementController::sub_28(bool& unk)
{
    unk = false;
}

void MultiMovementController::sub_30(Red::Vector4& positionDelta, Red::Quaternion& rotationDelta)
{
    
}

void MultiMovementController::sub_38(Red::Vector4& position, Red::Quaternion& rotation, bool& unk1, bool& unk2)
{
    
}

void MultiMovementController::SendAnimationParameters(float delta, Red::Vector4& position, Red::Quaternion& orientation)
{
    AnimationData data;
    data.controller = GetName();

    GetAnimationParameters(data);

    m_animationDriver.SendParameters(data);
}

void MultiMovementController::sub_48(RED4ext::Vector4& somePosition1, RED4ext::Quaternion& someRotation1, RED4ext::Vector4& somePosition2, RED4ext::Quaternion& someRotation2)
{
    
}

void MultiMovementController::sub_50(RED4ext::Vector4& somePosition1, RED4ext::Quaternion& someRotation1, float unk, RED4ext::Vector4& somePosition2, RED4ext::Quaternion& someRotation2)
{
    
}

Red::CName MultiMovementController::GetName() const
{
    return NAME;
}

bool MultiMovementController::sub_60()
{
    return true;
}

void MultiMovementController::sub_68()
{
    
}

void MultiMovementController::sub_70()
{
    
}

void MultiMovementController::Mount(RED4ext::move::Component& movable, Red::Handle<Red::ent::Entity> owner)
{
    
}

void MultiMovementController::Attach(Red::move::Component& movable)
{
    m_pComponent = &movable;

    const auto& pos = movable.owner->placedComponent->localTransform.Position;
    const auto& rot = movable.owner->placedComponent->localTransform.Orientation;

    const auto quat = Game::ToGlm(rot);
    SetTransform(Red::Vector4{pos.x, pos.y, pos.z, 0.f}, eulerAngles(quat).z, 0.f);

    m_animationDriver.Attach(movable.owner);

    m_pState = MakeUnique<States::Spawning>(*this);
    m_pState->Enter();
}

void MultiMovementController::Detach(Red::move::Component& movable)
{
    m_animationDriver.Detach();

    m_pState.reset();

    m_pComponent = nullptr;
}

void MultiMovementController::GetAnimationParameters(AnimationData& animationData)
{
    m_pState->GetAnimationData(animationData);
}

void MultiMovementController::SetTransform(const Red::Vector4& aPosition, float aAngle, float speed)
{
    m_position = aPosition;
    m_angle = aAngle;
    m_speed = speed;
}

float MultiMovementController::GetAnimLength(Red::CName aName) const
{
    return m_animationDriver.GetAnimLength(aName);
}

void MultiMovementController::Reset()
{
    m_pComponent->representation.stack.Clear();
    m_pComponent->representation.activeIndex = -1;
    m_pComponent->representation.activeEntry = nullptr;
    m_pComponent->representation.active = false;
}
