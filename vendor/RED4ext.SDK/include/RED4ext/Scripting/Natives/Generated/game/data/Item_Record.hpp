#pragma once

// clang-format off

// This file is generated from the Game's Reflection data

#include <cstdint>
#include <RED4ext/Common.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/data/BaseObject_Record.hpp>

namespace RED4ext
{
namespace game::data
{
struct Item_Record : game::data::BaseObject_Record
{
    static constexpr const char* NAME = "gamedataItem_Record";
    static constexpr const char* ALIAS = "Item_Record";

    // UNVERIFIED FOR 2.5. This is the 2.2-2.31 slot order (82 x 12-byte Vals, 0xA0-0x478). 2.5
    // grew the record by 0x10 - not a whole number of 12-byte slots, so the region changed shape,
    // not just length - and nothing reflected says where. CyberpunkMP reads appearanceName
    // (AppearanceSystem); the layout-proof fix is a TweakDB flat lookup by name instead.
    game::data::Val friendlyName;
    game::data::Val iconPath;
    game::data::Val localizedName;
    game::data::Val movementSound;
    game::data::Val crosshair;
    game::data::Val equipArea;
    game::data::Val CraftingData;
    game::data::Val itemType;
    game::data::Val blueprint;
    game::data::Val npcRPGData;
    game::data::Val quality;
    game::data::Val parentAttachmentType;
    game::data::Val itemStructure;
    game::data::Val equivalent;
    game::data::Val itemCategory;
    game::data::Val cpoItemCategory;
    game::data::Val icon;
    game::data::Val counterpart;
    game::data::Val dropSettings;
    game::data::Val itemSecondaryAction;
    game::data::Val minigameInstance;
    game::data::Val nextUpgradeItem;
    game::data::Val sideUpgradeItem;
    game::data::Val onEquipStats;
    game::data::Val equipPrereqs;
    game::data::Val slotPartListPreset;
    game::data::Val equipAreas;
    game::data::Val appearanceSuffixes;
    game::data::Val previewBBoxOverride;
    game::data::Val gameplayRestrictions;
    game::data::Val appearanceSuffixesOwnerOverride;
    game::data::Val OnEquip;
    game::data::Val tags;
    game::data::Val OnAttach;
    game::data::Val sellPrice;
    game::data::Val visualTags;
    game::data::Val buyPrice;
    game::data::Val OnLooted;
    game::data::Val placementSlots;
    game::data::Val slotPartList;
    game::data::Val hairSkinnedMeshComponents;
    game::data::Val variants;
    game::data::Val attachmentSlots;
    game::data::Val requiredSlots;
    game::data::Val parts;
    game::data::Val connections;
    game::data::Val animationParameters;
    game::data::Val displayName;
    game::data::Val dropObject;
    game::data::Val audioName;
    game::data::Val equipSoundMetadata;
    game::data::Val audioSwitchValue;
    game::data::Val entityName;
    game::data::Val appearanceName;
    game::data::Val audioSwitchName;
    game::data::Val animFeatureName;
    game::data::Val stateMachineName;
    game::data::Val appearanceResourceName;
    game::data::Val animName;
    game::data::Val animSetResource;
    game::data::Val localizedDescription;
    game::data::Val movementPattern;
    game::data::Val cameraForward;
    game::data::Val cameraUp;
    game::data::Val upgradeCostMult;
    game::data::Val garmentOffset;
    game::data::Val mass;
    game::data::Val powerLevelDeterminedByParent;
    game::data::Val useHeadgearGarmentAggregator;
    game::data::Val isSingleInstance;
    game::data::Val isPart;
    game::data::Val deprecated;
    game::data::Val isGarment;
    game::data::Val isCustomizable;
    game::data::Val enableNpcRPGData;
    game::data::Val isCached;
    game::data::Val qualityRestrictedByParent;
    game::data::Val canDrop;
    game::data::Val replicateWhenNotActive;
    game::data::Val usesVariants;
    game::data::Val isCoreCW;
    game::data::Val useNewSpawnMethod;
    uint8_t unk478[0x488 - 0x478]; // 478 - grew in 2.5, see above
};
RED4EXT_ASSERT_SIZE(Item_Record, 0x488);
RED4EXT_ASSERT_OFFSET(Item_Record, placementSlots, 0x268);
} // namespace game::data
using gamedataItem_Record = game::data::Item_Record;
using Item_Record = game::data::Item_Record;
} // namespace RED4ext

// clang-format on
