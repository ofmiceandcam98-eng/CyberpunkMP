#include "MovementComponent.h"

#include "GameServer.h"
#include "PlayerComponent.h"
#include "AttachmentComponent.h"

// Interest management.
//
// Every movement update used to go to every connected player, unconditionally. At the old
// cap of four that was 4 x 30 x 3 = 360 sends a second and nobody noticed. At sixteen it is
// 16 x 30 x 15 = 7,200, and a car full of people is worse still because a driver sends two
// updates per tick - one for the vehicle and one for their character.
//
// Almost all of that is wasted. Remote players are not visible across Night City, so the
// far half of it is describing puppets nobody can see.
//
// Rather than a hard cutoff, the rate degrades with distance. A hard cutoff freezes distant
// puppets at their last known spot and makes them jump when someone approaches; a slow
// heartbeat keeps them roughly right, which is what the map pins and the "who is nearby"
// feeling need, at a fraction of the traffic.
namespace
{
// Comfortably past the distance a remote player puppet is legible at.
constexpr float kFullRateRange = 200.f;

// Still close enough to matter - approaching, or in the same district.
constexpr float kReducedRateRange = 600.f;

// Every 4th update inside the middle band, every 16th beyond it. At 30/s that is still
// ~7 and ~2 updates a second, which is enough to keep a distant puppet honest.
constexpr uint32_t kReducedRateDivisor = 4;
constexpr uint32_t kDistantRateDivisor = 16;

// Should this recipient get this particular update?
bool ShouldSendTo(const glm::vec3& acFrom, const MovementComponent* apRecipient, uint32_t aSequence)
{
    // No puppet means they are connected but not standing anywhere yet - there is no
    // distance to measure. They receive the full world state when they spawn, from
    // Level::AddPlayer, so nothing is missed by staying quiet until then.
    if (!apRecipient)
        return false;

    const float distance = glm::distance(acFrom, apRecipient->Position);

    // Not finite means one of the two positions is nonsense. Validation drops those on
    // ingress now, but a comparison against NaN is false either way and would silently
    // stop replicating this pair - better to send and be wrong than to go quiet.
    if (!std::isfinite(distance))
        return true;

    if (distance <= kFullRateRange)
        return true;

    if (distance <= kReducedRateRange)
        return (aSequence % kReducedRateDivisor) == 0;

    return (aSequence % kDistantRateDivisor) == 0;
}
} // namespace

void ReplicateMovementComponent(flecs::entity aEntity, const MovementComponent& aComponent)
{
    // Nothing mounted needs its own position replicated.
    //
    // The client already ignores it: InterpolateEntity returns early for any entity with an
    // AttachedComponent, because the game has parented the puppet to the vehicle and moves
    // it from there. So every one of these was received, buffered and thrown away - and a
    // driver sends two packets per tick, which made half a driving player's traffic pure
    // waste.
    //
    // The server still RECORDS the position, because chat range and jail enforcement need
    // to know where somebody in a car actually is. It just stops telling everyone else.
    if (aEntity.has<AttachmentComponent>())
        return;

    common::Vector3 pos;
    pos.set_x(aComponent.Position.x);
    pos.set_y(aComponent.Position.y);
    pos.set_z(aComponent.Position.z);

    server::NotifyEntityMove message;
    message.set_id(aEntity);

    if (aComponent.Rotation.x == 0.f && aComponent.Rotation.y == 0.f)
    {
        message.set_rotation(aComponent.Rotation.z);
    }
    else
    {
        common::Vector3 rot;
        rot.set_x(aComponent.Rotation.x);
        rot.set_y(aComponent.Rotation.y);
        rot.set_z(aComponent.Rotation.z);

        message.set_full_rotation(rot);
    }

    message.set_position(pos);
    message.set_tick(aComponent.Tick);
    message.set_speed(aComponent.Velocity);

    const auto owner = aEntity.parent();
    const auto& from = aComponent.Position;
    const auto sequence = aComponent.Sequence;

    aEntity.world().each(
        [owner, &from, sequence, &message](flecs::entity player, const PlayerComponent& aPlayerComponent)
        {
            if (!IsDebug() && player == owner)
                return;

            const auto* pTheirs = aPlayerComponent.Puppet
                                      ? aPlayerComponent.Puppet.get<MovementComponent>()
                                      : nullptr;

            if (!ShouldSendTo(from, pTheirs, sequence))
                return;

            GServer->Send(aPlayerComponent.Connection, message);
        });
}

void MovementComponent::Register(flecs::world& aWorld)
{
    aWorld.component<glm::vec3>()
        .member("x", &glm::vec3::x)
        .member("y", &glm::vec3::y)
        .member("z", &glm::vec3::z);

    aWorld.component<MovementComponent>()
        .member("Position", &MovementComponent::Position)
        .member("Rotation", &MovementComponent::Rotation)
        .member("Tick", &MovementComponent::Tick)
        .member("Speed", &MovementComponent::Velocity);

    auto observer = aWorld.observer<const MovementComponent>("Movement Replication")
        .event(flecs::OnSet)
        // .without<AttachmentComponent>()
        .each(
            [](flecs::iter& it, size_t i, const MovementComponent& aComponent)
            {
                const auto entity = it.entity(i);

                ReplicateMovementComponent(entity, aComponent);
            });

    observer.child_of(aWorld.entity("observers"));
}
