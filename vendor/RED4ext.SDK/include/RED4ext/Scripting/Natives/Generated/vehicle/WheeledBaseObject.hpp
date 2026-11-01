#pragma once

// clang-format off

// This file is generated from the Game's Reflection data

#include <cstdint>
#include <RED4ext/Common.hpp>
#include <RED4ext/Scripting/Natives/Generated/vehicle/BaseObject.hpp>

namespace RED4ext
{
namespace vehicle
{
struct Transmission {
    uint64_t unk00;
};
struct Engine {
    uint8_t unk00[0x61]; // 00
    uint8_t unk61; // 61
};
struct __declspec(align(0x10)) WheeledBaseObject : vehicle::BaseObject
{
    static constexpr const char* NAME = "vehicleWheeledBaseObject";
    static constexpr const char* ALIAS = "WheeledObject";

    // 2.5: vehicle::BaseObject grew 0xB90 -> 0xBC0 while this struct's own region stayed exactly
    // 0x50 bytes, so these 2.2-2.31 decodes are taken as unchanged and shifted +0x30. Inferred, not
    // reflected. CyberpunkMP WRITES through engineData (VehicleSystem MakeRemoteDriven), and the
    // Engine struct's unk61 is its own unverified decode - confirm both before trusting them on 2.5.
    void * interactionsMaybe;           // BC0
    Transmission * transmissionData;    // BC8
    Engine * engineData;                // BD0
    void * burnoutData;                 // BD8

    uint8_t unkBE0[0xC10 - 0xBE0]; // BE0
};
RED4EXT_ASSERT_SIZE(WheeledBaseObject, 0xC10);
RED4EXT_ASSERT_OFFSET(WheeledBaseObject, engineData, 0xBD0);
} // namespace vehicle
using vehicleWheeledBaseObject = vehicle::WheeledBaseObject;
using WheeledObject = vehicle::WheeledBaseObject;
} // namespace RED4ext

// clang-format on
