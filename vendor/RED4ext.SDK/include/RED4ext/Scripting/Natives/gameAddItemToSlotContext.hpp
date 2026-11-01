#pragma once

#include <RED4ext/NativeTypes.hpp>
#include <RED4ext/Scripting/Natives/Generated/ERenderingPlane.hpp>

namespace RED4ext::game
{

struct AddItemToSlotContext 
{
  game::Object *object; // 00
  TweakDBID slotID; // 08
  ItemID itemID; // 10
  uint8_t unk20 = 0; // 20
  uint8_t highPriority = 1; // 21
  uint16_t unk22[3]; // 22
  WeakHandle<game::Object> itemObject; // 28
  ERenderingPlane renderingPlane; // 38
  uint8_t keepWorldTransform; // 3C
  uint8_t ignoreRestrictions; // 3D
  uint8_t dontCacheEquippedItem = 1; // 3E
  uint8_t unk3F = 0;
  CName garmentAppearanceName; // 40
  ItemID appearanceItem; // 48
};

RED4EXT_ASSERT_OFFSET(AddItemToSlotContext, garmentAppearanceName, 0x40);

}