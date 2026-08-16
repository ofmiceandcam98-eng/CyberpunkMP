#include "App/World/AppearanceSystem.h"

#include "RED4ext/Scripting/Natives/gameIEntityStubSystem.hpp"
#include "RED4ext/Scripting/Natives/gameITransactionSystem.hpp"
#include "RED4ext/Scripting/Natives/gameItemModParams.hpp"
#include "RED4ext/Scripting/Natives/gameAddItemToSlotContext.hpp"
#include "RED4ext/Scripting/Natives/Generated/game/EntityStubComponentPS.hpp"
#include "RED4ext/Scripting/Natives/Generated/game/data/Item_Record.hpp"
#include "RED4ext/NativeTypes.hpp"
#include "RED4ext/ResourceLoader.hpp"


#include "NetworkWorldSystem.h"
#include "Game/Utils.h"
#include "Game/CharacterCustomizationSystem.h"
#include "RED4ext/Scripting/Natives/Generated/game/PuppetPS.hpp"

AppearanceSystem::AppearanceSystem()
{

}

void AppearanceSystem::OnInitialize(const RED4ext::JobHandle& aJob)
{
    spdlog::info("[AppearanceSystem] OnInitialize");
}

void AppearanceSystem::OnWorldAttached(RED4ext::world::RuntimeScene* aScene)
{    
    spdlog::info("[AppearanceSystem] OnWorldAttached");
    Red::CallVirtual(this, "OnWorldAttached");
}

void AppearanceSystem::OnBeforeWorldDetach(RED4ext::world::RuntimeScene* aScene)
{
    spdlog::info("[AppearanceSystem] OnBeforeWorldDetach");
    Red::CallVirtual(this, "OnBeforeWorldDetach");
}

// FNV-1a. Used only for the [Identity] log lines below: a cheap fingerprint of the
// appearance data, so a session log can show whether the bytes ADDED for an entity are
// the same bytes APPLIED to it. When two players render as each other, these hashes say
// on which side of the map the swap happened - distinct at add + swapped at apply means
// our maps; identical at both means the engine's appearance changer.
static uint64_t HashBytes(const void* apData, size_t aSize)
{
    const auto* p = static_cast<const uint8_t*>(apData);
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < aSize; ++i)
    {
        hash ^= p[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static uint64_t HashEquipment(const Red::DynArray<Red::TweakDBID>& aItems)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const auto& item : aItems)
    {
        hash ^= item.value;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

Red::DynArray<Red::TweakDBID> AppearanceSystem::GetEntityItems(Red::EntityID & entityID)
{
    std::lock_guard lock(m_mapLock);

    // find, not operator[] - a read must not insert an empty entry for an unknown entity.
    const auto it = m_playerEquipment.find(entityID);
    return it == m_playerEquipment.end() ? Red::DynArray<Red::TweakDBID>() : it->second;
}

Vector<uint64_t> AppearanceSystem::GetPlayerItems(Red::Handle<Red::GameObject> player)
{
    // Numbers, not names.
    //
    // This used to collect TDBID.ToStringDEBUG output. That helper reads TweakDB's debug
    // name table, which release builds of the game do not ship, so on 2.31 every entry
    // was an empty string. The empty strings serialised fine, crossed the wire fine, and
    // then failed on the far side as eight "ItemID creation failed" warnings per spawn -
    // which is why remote players had no clothes or weapons.
    auto equipment = Vector<uint64_t>();

    Red::DynArray<uint64_t> items;
    Red::CallVirtual(this, "GetPlayerItems", items);

    for (auto item : items)
    {
        spdlog::info("Getting: {:#x}", item);
        equipment.push_back(item);
    }

    return equipment;
}

void AppearanceSystem::AddEntity(const Red::EntityID entityID, const Red::DynArray<Red::TweakDBID>& items, const Vector<uint8_t> ccstate)
{
    spdlog::info("Logging Entity Appearance: {}", entityID.hash);

    // TEMPORARY [PROBE] lines - see the matching block in NetworkWorldSystem::Spawn.
    spdlog::info("[PROBE 2] AddEntity: writing equipment map ({} items)", items.size);
    spdlog::info("[PROBE 3] AddEntity: writing ccstate map ({} bytes)", ccstate.size());

    {
        std::lock_guard lock(m_mapLock);
        m_playerEquipment[entityID] = items;
        m_playerCcstate[entityID] = ccstate;
    }

    // The identity fingerprint for this entity, at the moment its data arrives. Compare
    // against the matching 'apply' line - see HashBytes above.
    spdlog::info("[Identity] add   entity {:x}: ccstate {} bytes hash={:016x}, equipment {} items hash={:016x}",
                 entityID.hash, ccstate.size(), HashBytes(ccstate.data(), ccstate.size()),
                 items.size, HashEquipment(items));

    spdlog::info("[PROBE 4] AddEntity: both map writes done");
}

void AppearanceSystem::SetEntityName(Red::EntityID entityID, const std::string& acName)
{
    std::lock_guard lock(m_mapLock);
    m_playerNames[entityID] = acName;
}

std::string AppearanceSystem::GetEntityName(Red::EntityID entityID) const
{
    std::lock_guard lock(m_mapLock);
    const auto it = m_playerNames.find(entityID);
    return it == m_playerNames.end() ? std::string() : it->second;
}

void AddItems(Red::Handle<Red::game::Object> & object, Red::DynArray<Red::TweakDBID> const & items, game::ui::CharacterCustomizationState const * state)
{
    auto system = Red::GetGameSystem<Red::game::ITransactionSystem>();
    for (auto item : items)
    {
        auto itemID = Red::ItemID();
        ItemID_Create(&itemID, item, -1);
        if (!itemID.IsValid())
        {
            spdlog::warn("ItemID creation failed");
            continue;
        }

        auto item_record = Red::Handle<Red::game::data::Item_Record>();
        GetItemRecord(&item_record, item);
        if (!item_record)
        {
            spdlog::warn("Item record not found");
            continue;
        }
        
        auto placementSlotHandle = Red::Handle<Red::game::data::AttachmentSlot_Record>();
        GetPlacementSlot(item_record.instance, &placementSlotHandle, 0);
        if (!placementSlotHandle)
        {
            spdlog::warn("Placement slot not found... kinda weird");
            continue;
        }

        auto placementSlot = placementSlotHandle.instance->recordID;

        auto appearanceName = EvalCName(&item_record.instance->appearanceName);
        std::string appearance = appearanceName.ToString();
        // auto suffixes = EvalArrayTweakDBID(&item_record.instance->appearanceSuffixes);
        // for (auto const suffix : suffixes) {
        //     if (suffix == Red::TweakDBID("itemsFactoryAppearanceSuffix.Gender")) {
        //         if (state->isBodyGenderMale) {
        //             appearance.append("&Male");
        //         } else {
        //             appearance.append("&Female");
        //         }
        //     } else if (suffix == Red::TweakDBID("itemsFactoryAppearanceSuffix.Camera")) {
        //         appearance.append("&TPP");
        //     } else if (suffix == Red::TweakDBID("itemsFactoryAppearanceSuffix.Partial")) {
        //         appearance.append("&Full");
        //     } else if (suffix == Red::TweakDBID("itemsFactoryAppearanceSuffix.HairType")) {
        //         if (state->tags.Contains("Short")) {
        //             appearance.append("&Short");
        //         } else if (state->tags.Contains("Long")) {
        //             appearance.append("&Long");
        //         } else if (state->tags.Contains("Dreads")) {
        //             appearance.append("&Dreads");
        //         } else if (state->tags.Contains("Buzz")) {
        //             appearance.append("&Buzz");
        //         } else {
        //             appearance.append("&Bald");
        //         }
        //     }
        // }
        // The two ToStringDEBUG calls that used to log the item and slot names here are
        // gone. TweakDB's debug name table is stripped from release builds, so both
        // returned an empty string on 2.31 - two TweakDB lookups per item per spawn to
        // print nothing.
        Red::CString redAppStr;
        GetItemAppearanceName(&redAppStr, object, object, item_record, itemID);
        spdlog::info("[Appearance] {}{}", appearance, redAppStr.c_str());
        appearance.append(redAppStr.c_str());

        appearanceName = appearance.c_str();

        auto params = Red::game::ItemModParams();
        params.quantity = 1;
        params.itemID = itemID;

        auto context = Red::game::AddItemToSlotContext();
        context.object = object.instance;
        context.slotID = placementSlot;
        context.itemID = itemID;
        // context.itemID.flags |= 1;
        // context.ignoreRestrictions = 1;
        context.renderingPlane = Red::ERenderingPlane::RPl_Scene;
        // context.renderingPlane = Red::ERenderingPlane::RPl_Weapon;
        context.garmentAppearanceName = appearanceName;

        auto given = system->GiveItem(*object, params);
        if (given)
        {
            auto added = system->AddItemToSlot(context);
            if (!added)
            {
                spdlog::info("AddItemToSlot failed");
            }
        } else
        {
            spdlog::info("GiveItem failed");
        }
    }
}

bool AppearanceSystem::ApplyAppearance(Red::Handle<Red::game::Object> object)
{
    // TEMPORARY [PROBE 21]. Tells us whether the Entity/Attached script callback fired at
    // all for a puppet that crashed - the surviving spawn reaches this ~40ms after Spawn()
    // returns, the crashing one never logs anything after [PROBE 10].
    spdlog::info("[PROBE 21] ApplyAppearance entered");

    if (!object.instance)
    {
        spdlog::error("[Appearance] ApplyAppearance called with a null object - aborting");
        return false;
    }

    // One locked copy-out for everything this function needs from the maps. The rest of
    // the function works on the copies, so nothing engine-facing runs under the lock -
    // and GetEntityName is NOT called here because it takes the same (non-recursive)
    // lock.
    Vector<uint8_t> bytes;
    Red::DynArray<Red::TweakDBID> items;
    std::string name;
    {
        std::lock_guard lock(m_mapLock);

        const auto ccIt = m_playerCcstate.find(object.instance->id);
        if (ccIt == m_playerCcstate.end())
        {
            // not our entity
            return false;
        }
        bytes = ccIt->second;

        const auto eqIt = m_playerEquipment.find(object.instance->id);
        if (eqIt != m_playerEquipment.end())
            items = eqIt->second;

        const auto nameIt = m_playerNames.find(object.instance->id);
        if (nameIt != m_playerNames.end())
            name = nameIt->second;
    }

    if (bytes.size() == 0)
    {
        spdlog::info("no bytes for {}", object->id.hash);
        return false;
    }

    // The identity fingerprint at the moment of application. If two players render as
    // each other and this line's hashes MATCH their 'add' lines, the swap happened
    // downstream of us - in the engine's appearance changer. If they differ, it is ours.
    spdlog::info("[Identity] apply entity {:x}: ccstate {} bytes hash={:016x}, equipment {} items hash={:016x}, name '{}'",
                 object.instance->id.hash, bytes.size(), HashBytes(bytes.data(), bytes.size()),
                 items.size, HashEquipment(items), name);

    // The name on the puppet.
    //
    // This was hardcoded to "Test" upstream. Left alone, the nameplate falls back to the
    // TweakDB record the puppet was built from - Character.MaMuppet - which is why every
    // remote player appeared as "Panam" regardless of who they were.
    //
    // The name comes from the server, which got it from Discord. A client never says who
    // it is; it is told.
    if (!name.empty())
        object.instance->displayName.unk08 = Red::CString(name.c_str());

    spdlog::info("Loaded bytes: {}", bytes.size());

    Red::Handle<game::ui::CharacterCustomizationState> stateHandle;
    CreateHandle_CharacterCustomizationState(&stateHandle);

    if (!stateHandle.instance)
    {
        spdlog::error("[Appearance] CreateHandle_CharacterCustomizationState returned null - aborting");
        return false;
    }

    if (bytes.empty())
    {
        spdlog::warn("[Appearance] no ccstate bytes for this entity - appearance will be default");
    }
    else
    {
        auto reader = CMPReader(bytes);
        CharacterCustomizationState_Serialize(stateHandle.instance, &reader);
    }

    // shouldn't be needed
    // Red::CallVirtual(this, "AddBodyParts", object, stateHandle.instance->isBodyGenderMale);

    auto ps = reinterpret_cast<Red::game::PuppetPS*>(object.instance->persistentState.instance);
    if (!ps)
    {
        spdlog::error("[Appearance] puppet persistentState is null - aborting before writing unk72");
        return false;
    }
    // checked during item adding process for &TPP
    ps->unk72[0] = 1;

    // old method
    // Red::CallVirtual(this, "AddItems", object);

    // c++ method - items were copied out under the lock at the top of this function
    // if (stateHandle.instance->isBodyGenderMale) {
    //     // items.PushBack("Items.PlayerMaTppHead");
    //     items.PushBack("Items.MuppetMaHead");
    // } else {
    //     // items.PushBack("Items.PlayerWaTppHead");
    //     items.PushBack("Items.MuppetWaHead");
    // }
    // items.PushBack("Items.MuppetArms");
    AddItems(object, items, stateHandle.instance);

    // spdlog::info("head groups read:");
    // for (auto &group : stateHandle.instance->unk70) 
    // {
    //     spdlog::info(group.name.ToString());
    // }
    // ma
    //  TPP
    //  FPP
    //  hairs
    //  character_customization
    //  TPP_proxy
    //  FPP_proxy
    //  FPP_hairs
    //  TPP_photomode
    //  beards
    //  face
    //  finalSceneBruises
    // wa
    //  TPP
    //  FPP
    //  hairs
    //  character_customization
    //  TPP_proxy
    //  FPP_proxy
    //  FPP_hairs
    //  TPP_photomode
    //  face
    //  finalSceneBruises

    // spdlog::info("body groups read:");
    // for (auto &group : stateHandle.instance->unk80) 
    // {
    //     spdlog::info(group.name.ToString());
    // }
    // FPP_Body
    // TPP_Body
    // character_creation
    // genitals
    // breast

    // FPP_Body
    // TPP_Body
    // character_creation
    // genitals
    // breast
    // lifted_feet
    // flat_feet

    // spdlog::info("arm groups read:");
    // for (auto &group : stateHandle.instance->unk90) 
    // {
    //     spdlog::info(group.name.ToString());
    // }
    // holstered_default
    // holstered_strong
    // unholstered_strong
    // holstered_nanowire
    // unholstered_nanowire
    // character_customization
    // personal_link_simple
    // personal_link_advanced      
    // holstered_launcher
    // unholstered_launcher
    // holstered_mantis
    // unholstered_mantis
    // nails

    // holstered_default_tpp
    // holstered_default_fpp
    // holstered_strong_tpp
    // holstered_strong_fpp
    // unholstered_strong
    // holstered_nanowire_tpp
    // holstered_nanowire_fpp
    // unholstered_nanowire
    // character_customization
    // personal_link_simple
    // personal_link_advanced
    // holstered_launcher_tpp
    // holstered_launcher_fpp
    // unholstered_launcher
    // holstered_mantis_tpp
    // holstered_mantis_fpp
    // unholstered_mantis
    // nails

    if (stateHandle)
    {
        spdlog::info("Scheduling change");

        Red::DynArray<Red::world::EntityAppearanceChangeParameter::Key> keys;
        stateHandle.instance->GetHeadCustomization("TPP", true, keys);
        stateHandle.instance->GetHeadCustomization("face", true, keys);
        stateHandle.instance->GetHeadCustomization("hairs", true, keys);
        if (stateHandle.instance->isBodyGenderMale) 
        {
            stateHandle.instance->GetHeadCustomization("beards", true, keys);
        }

        // does all of them
        // stateHandle.instance->GetHeadCustomization("character_customization", true, keys);

        stateHandle.instance->GetBodyCustomization("TPP_Body", true, keys);
        // make genitals appear as well
        // stateHandle.instance->GetBodyCustomization("character_creation", true, keys);
        // pops through clothes lol
        // stateHandle.instance->GetBodyCustomization("genitals", true, keys);
        stateHandle.instance->GetBodyCustomization("breast", true, keys);
        stateHandle.instance->GetBodyCustomization("lifted_feet", true, keys);
        stateHandle.instance->GetBodyCustomization("flat_feet", true, keys);

        // stateHandle.instance->GetArmsCustomization("character_customization", true, keys);
        stateHandle.instance->GetArmsCustomization("holstered_default", true, keys);
        stateHandle.instance->GetArmsCustomization("nails", true, keys);

        auto changer = Red::GetRuntimeSystem<Red::world::RuntimeSystemEntityAppearanceChanger>();
        Red::WeakHandle<Red::game::Object> weakHandle = object;
        Span<Red::world::EntityAppearanceChangeParameter::Key> old_keys = {
            .start = nullptr,
            .end = nullptr
        };
        Span<Red::world::EntityAppearanceChangeParameter::Key> new_keys = {
            .start = keys.entries,
            .end = &keys.entries[keys.size]
        };
        const std::function<void (void)> callback = [stateHandle = std::move(stateHandle), object = std::move(object)](void) 
        {
            // AddItems(object, items, stateHandle.instance);

            // this might not be needed? - JACK
            // also makes wa faces disappear :/

            // spdlog::info("Changed callback");
            // // TransactionSystem::sub_440(object, std::function);
            // // hide some components based on Get*Customization
            // // MorphHead;
            
            // stateHandle.instance->MorphHead("TPP", true, object, true);
            // // stateHandle.instance->MorphHead("face", true, object, true);
            // stateHandle.instance->MorphHead("hairs", true, object, true);
            
            // if (stateHandle.instance->isBodyGenderMale) 
            // {
            //     stateHandle.instance->MorphHead("beards", true, object, true);
            // }

            // stateHandle.instance->MorphBody("TPP_Body", true, object, true);
            // // stateHandle.instance->MorphBody("genitals", true, object, true);
            // // stateHandle.instance->MorphBody("breast", true, object, true);

            // stateHandle.instance->MorphArms("holstered_default", true, object, true);
            // // stateHandle.instance->MorphArms("nails", true, object, true);

            // stateHandle.instance->ApplyHead("TPP", true, object);
            // // stateHandle.instance->ApplyHead("face", true, object);
            // stateHandle.instance->ApplyHead("hairs", true, object);
        
            // if (stateHandle.instance->isBodyGenderMale) 
            // {
            //     stateHandle.instance->ApplyHead("beards", true, object);
            // }

            // stateHandle.instance->ApplyBody("TPP_Body", true, object);
            // // stateHandle.instance->ApplyBody("genitals", true, object);
            // // stateHandle.instance->ApplyBody("breast", true, object);

            // stateHandle.instance->ApplyArms("holstered_default", true, object);
            // // stateHandle.instance->ApplyArms("nails", true, object);
        
            // // auto apprSystem = Red::GetGameSystem<AppearanceSystem>();
            // // Red::CallVirtual(apprSystem, "AddItems", object);
            // // Red::CallVirtual(apprSystem, "AddBodyParts", object, stateHandle.instance->isBodyGenderMale);
            
        };
        ScheduleSynchronizedAppearanceChanges(changer, weakHandle, &old_keys, &new_keys, callback, 0);
    }
    else
    {
        spdlog::warn("CustomizationState was null");
    }

    return true;
}
