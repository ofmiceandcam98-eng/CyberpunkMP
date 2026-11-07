#pragma once

// clang-format off

#include <cstdint>

#include <RED4ext/Common.hpp>
#include <RED4ext/DynArray.hpp>
#include <RED4ext/Scripting/Natives/Generated/WorldTransform.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/IMoverComponent.hpp>
#include <RED4ext/Scripting/Natives/Vector3.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>

namespace RED4ext
{
namespace move
{
struct Component;

// CyberpunkMP's decode (maximegmd, 2024), merged onto upstream's 2.5 layout below.
struct MovementRepresentation
{
    Component* parent;
    bool active;
    DynArray<void*> stack;
    int32_t activeIndex;
    void* activeEntry;
};
RED4EXT_ASSERT_SIZE(MovementRepresentation, 0x30);
RED4EXT_ASSERT_OFFSET(MovementRepresentation, activeEntry, 0x28);

struct Component : ent::IMoverComponent
{
    static constexpr const char* NAME = "moveComponent";
    static constexpr const char* ALIAS = NAME;

    uint8_t unk90[0x138 - 0x90];              // 90
    // Upstream leaves 0x90-0x1C0 opaque. 2.5 kept this struct at 0x2C0 and every upstream anchor
    // below where it was, so the representation is taken as still at 0x138 - inferred, not
    // reflected. CyberpunkMP WRITES it (MultiMovementController) when taking over a remote puppet.
    MovementRepresentation representation;    // 138
    uint8_t unk168[0x1C0 - 0x168];            // 168
    WorldTransform worldTransform;            // 1C0
    uint8_t unk1E0[0x1E8 - 0x1E0];            // 1E0
    Vector4 position;                         // 1E8
    uint8_t unk1F8[0x220 - 0x1F8];            // 1F8
    Vector3 speed;                            // 220
    float deltaFrame;                         // 22C
    uint8_t unk230[0x2C0 - 0x230];            // 230
};
RED4EXT_ASSERT_SIZE(Component, 0x2C0);
RED4EXT_ASSERT_OFFSET(Component, representation, 0x138);
RED4EXT_ASSERT_OFFSET(Component, worldTransform, 0x1C0);
RED4EXT_ASSERT_OFFSET(Component, position, 0x1E8);
RED4EXT_ASSERT_OFFSET(Component, speed, 0x220);
RED4EXT_ASSERT_OFFSET(Component, deltaFrame, 0x22C);
} // namespace move
using moveComponent = move::Component;
} // namespace RED4ext

// clang-format on
