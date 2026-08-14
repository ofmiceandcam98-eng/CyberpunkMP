#pragma once

#include "Core/Stl.hpp"
#include "Core/Hooking/HookingAgent.hpp"
#include "RED4ext/Scripting/Natives/Generated/Vector4.hpp"
#include "RED4ext/Scripting/Natives/Generated/Quaternion.hpp"
#include "Network/Client.h"


struct AppearanceSystem : RED4ext::IScriptable
{
    RTTI_IMPL_TYPEINFO(AppearanceSystem);
    RTTI_IMPL_ALLOCATOR();

    AppearanceSystem();

    void OnInitialize(const RED4ext::JobHandle& aJob);

    Red::DynArray<Red::TweakDBID> GetEntityItems(Red::EntityID &);
    void AddEntity(const Red::EntityID entityID, const Red::DynArray<Red::TweakDBID>& items, const Vector<uint8_t> ccstate);

    // Who a puppet belongs to, so its nameplate can say so. Empty for anything that is
    // not a player - server-spawned characters have no Discord account behind them.
    void SetEntityName(Red::EntityID entityID, const std::string& acName);
    std::string GetEntityName(Red::EntityID entityID) const;
    Vector<uint64_t> GetPlayerItems(Red::Handle<Red::GameObject> player);
    bool ApplyAppearance(Red::Handle<Red::GameObject> object);

    void OnWorldAttached(RED4ext::world::RuntimeScene* aScene);
    void OnBeforeWorldDetach(RED4ext::world::RuntimeScene* aScene);

private:
    Core::Map<Red::EntityID, Red::DynArray<Red::TweakDBID>> m_playerEquipment;
    Core::Map<Red::EntityID, Vector<uint8_t>> m_playerCcstate;
    Core::Map<Red::EntityID, std::string> m_playerNames;
};

RTTI_DEFINE_CLASS(AppearanceSystem, { 
    RTTI_ALIAS("CyberpunkMP.World.AppearanceSystem");
    RTTI_METHOD(GetEntityItems);
    RTTI_METHOD(ApplyAppearance);
});