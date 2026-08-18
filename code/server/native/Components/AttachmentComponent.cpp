#include "AttachmentComponent.h"
#include "AuthorityComponent.h"
#include "GameServer.h"
#include "PlayerComponent.h"

void ReplicateSetAttachmentComponent(flecs::entity aEntity, AttachmentComponent& aComponent)
{
    server::NotifyVehicleEnter enter;
    enter.set_vehicle_id(aComponent.Parent);
    enter.set_character_id(aEntity);
    enter.set_sit_id(aComponent.SlotId);

    spdlog::info("AttachmentComponent::ReplicateSetAttachmentComponent: Slot: {}", aComponent.SlotId);

    aEntity.world().each(
        [aEntity, &enter](flecs::entity player, const PlayerComponent& aPlayerComponent)
        {
            if (!IsDebug() && player == aEntity.parent())
                return;

            GServer->Send(aPlayerComponent.Connection, enter);
        });

    if (aComponent.SlotId == 0xb000b1d029d0cea0ULL) // seat_front_left
    {
        // The driver's machine simulates the car. TransferAuthority reparents, bumps the
        // epoch when the owner actually changes, and tells BOTH sides - the revoke half
        // is what was always missing here: the previous driver's client was never told to
        // stop simulating, and two machines fighting over one car is what passengers felt
        // as bouncing.
        TransferAuthority(aComponent.Parent, aEntity.parent());
    }
}

void ReplicateRemoveAttachmentComponent(flecs::entity aEntity, const AttachmentComponent& aComponent)
{
    server::NotifyVehicleExit message;
    message.set_character_id(aEntity);

    spdlog::info("AttachmentComponent::ReplicateRemoveAttachmentComponent");

    aEntity.world().each(
        [aEntity, &message](flecs::entity player, const PlayerComponent& aPlayerComponent)
        {
            if (!IsDebug() && player == aEntity.parent())
                return;

            GServer->Send(aPlayerComponent.Connection, message);
        });
}

void AttachmentComponent::Register(flecs::world& aWorld)
{
    aWorld.component<AttachmentComponent>()
        .member("Parent", &AttachmentComponent::Parent)
        .member("Slot", &AttachmentComponent::SlotId);

    auto observer = aWorld.observer<AttachmentComponent>("Attachment set replication")
        .event(flecs::OnSet)
        .each(
            [](flecs::iter& it, size_t i, AttachmentComponent& aComponent)
            {
                const auto entity = it.entity(i);

                ReplicateSetAttachmentComponent(entity, aComponent);
            });

    observer.child_of(aWorld.entity("observers"));

    observer = aWorld.observer<const AttachmentComponent>("Attachment remove replication")
       .event(flecs::OnRemove)
       .each(
           [](flecs::iter& it, size_t i, const AttachmentComponent& aComponent)
           {
               const auto entity = it.entity(i);

               ReplicateRemoveAttachmentComponent(entity, aComponent);
           });

    observer.child_of(aWorld.entity("observers"));
}
