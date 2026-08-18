#include "AuthorityComponent.h"
#include "PlayerComponent.h"

#include "GameServer.h"

void TransferAuthority(flecs::entity aEntity, flecs::entity aNewOwnerPlayer)
{
    if (!aEntity || !aEntity.is_alive())
        return;

    if (!aEntity.has<AuthorityComponent>())
        aEntity.set<AuthorityComponent>({});

    auto* pAuthority = aEntity.get_mut<AuthorityComponent>();

    const auto oldOwner = aEntity.parent();
    const bool ownerChanged = oldOwner != aNewOwnerPlayer;

    if (ownerChanged)
    {
        ++pAuthority->Epoch;

        if (aNewOwnerPlayer)
            aEntity.child_of(aNewOwnerPlayer);
        else
            aEntity.remove(flecs::ChildOf, flecs::Wildcard);
    }

    // Revoke before assign. If both messages are in flight at once, the failure mode of
    // this order is a moment with NO simulator - the car freezes briefly. The other order
    // risks a moment with TWO, which is the bouncing this whole mechanism exists to end.
    if (ownerChanged && oldOwner)
    {
        if (const auto* pPlayer = oldOwner.get<PlayerComponent>())
        {
            server::NotifyAuthorityRevoked revoked;
            revoked.set_entity_id(aEntity);
            revoked.set_epoch(pAuthority->Epoch);

            GServer->Send(pPlayer->Connection, revoked);
        }
    }

    if (aNewOwnerPlayer)
    {
        if (const auto* pPlayer = aNewOwnerPlayer.get<PlayerComponent>())
        {
            server::NotifyAuthorityAssigned assigned;
            assigned.set_entity_id(aEntity);
            assigned.set_epoch(pAuthority->Epoch);

            GServer->Send(pPlayer->Connection, assigned);
        }
    }

    spdlog::info("Authority over {:x} moved to player {:x} (epoch {})", aEntity.id(),
                 aNewOwnerPlayer ? aNewOwnerPlayer.id() : 0, pAuthority->Epoch);
}

void AuthorityComponent::Register(flecs::world& aWorld)
{
    aWorld.component<AuthorityComponent>()
        .member("Epoch", &AuthorityComponent::Epoch);
}
