#pragma once

#include <RED4ext/Scripting/Natives/Generated/game/ItemObject.hpp>

namespace RED4ext::game
{

struct ItemModParams
{
    virtual void * GetMemoryPool(void) { return Memory::PoolEntityAppearance::Get(); }
    virtual ~ItemModParams(void) {}

    ItemID itemID;
    int32_t quantity = 1;
    uint32_t unk1C = 0;
    uint8_t unk20 = 0;
    uint8_t unk21 = 0;
    uint8_t unk22 = 0;
    uint8_t unk23 = 0;
    uint8_t unk24[3];
    float unk28 = FLT_MAX;
    uint32_t unk2C;
    DynArray<uint32_t> unk30;
    DynArray<CName> dynamicTags;
    DynArray<uint32_t> customPartsToInstall;
    uint8_t unk60;
};

RED4EXT_ASSERT_OFFSET(ItemModParams, unk60, 0x60);

}