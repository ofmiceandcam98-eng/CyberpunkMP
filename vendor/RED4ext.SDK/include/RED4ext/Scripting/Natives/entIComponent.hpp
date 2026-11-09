#pragma once

// clang-format off

// This file is generated from the Game's Reflection data

#include <cstdint>
#include <RED4ext/Common.hpp>
#include <RED4ext/CName.hpp>
#include <RED4ext/Common.hpp>
#include <RED4ext/NativeTypes.hpp>
#include <RED4ext/Scripting/IScriptable.hpp>

namespace RED4ext
{
namespace ent
{
struct Entity;
struct IComponent : IScriptable
{
    static constexpr const char* NAME = "entIComponent";
    static constexpr const char* ALIAS = "IComponent";

    CName name; // 40
    CName appearance; // 48
    Entity* owner; // 50
    void* replication; // 58
    CRUID id; // 60
    uint8_t unk68[0x8B - 0x68]; // 68
    bool isEnabled; // 8B
    bool isReplicable; // 8C
    uint8_t unk8D[0x90 - 0x8D]; // 8D
};
// CyberpunkMP's names over upstream's 2.5 layout (appearance = upstream appearanceName,
// replication = unk58) - every offset below matches it.
RED4EXT_ASSERT_SIZE(IComponent, 0x90);
RED4EXT_ASSERT_OFFSET(IComponent, owner, 0x50);
RED4EXT_ASSERT_OFFSET(IComponent, id, 0x60);
RED4EXT_ASSERT_OFFSET(IComponent, isEnabled, 0x8B);
} // namespace ent
} // namespace RED4ext

// clang-format on
