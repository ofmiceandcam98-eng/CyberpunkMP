#pragma once

// clang-format off

// This file is generated from the Game's Reflection data

#include <cstdint>
#include <RED4ext/Common.hpp>
#include <RED4ext/Scripting/IScriptable.hpp>

namespace RED4ext
{
namespace game
{
struct ItemData : IScriptable
{
    static constexpr const char* NAME = "gameItemData";
    static constexpr const char* ALIAS = NAME;

    virtual ~ItemData() = 0;

    virtual void m1() = 0;
    virtual void m2() = 0;

    virtual void m3() = 0;
    virtual void m4() = 0;

    virtual bool IsStackable() = 0;
    virtual void SetQuantity(uint32_t value) = 0;
    virtual void ModifyQuantity(int32_t amount) = 0;
    virtual uint32_t GetQuantity() = 0;

    ItemID itemID; // 40

    uint8_t unk40[0x78 - 0x40 - sizeof(ItemID)]; // 40
};
RED4EXT_ASSERT_SIZE(ItemData, 0x78);
} // namespace game
using gameItemData = game::ItemData;
} // namespace RED4ext

// clang-format on
