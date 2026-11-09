#pragma once

// clang-format off

// This file is generated from the Game's Reflection data

#include <cstdint>
#include <RED4ext/Common.hpp>
#include <RED4ext/Scripting/IScriptable.hpp>
#include <RED4ext/ResourcePath.hpp>

namespace RED4ext
{
    namespace world::EntityAppearanceChangeParameter {
    struct Key
    {
        ResourcePath resource;
        CName definition;
    };

    }

namespace game::ui
{
struct ICharacterCustomizationState : IScriptable
{
    static constexpr const char* NAME = "gameuiICharacterCustomizationState";
    static constexpr const char* ALIAS = NAME;

    // Slots decoded on 2.2-2.31 and UNVERIFIED for 2.5. 2.5 added cyberware customization, and
    // any virtual it inserted shifts every slot after it - a call through a shifted slot runs an
    // unrelated function. CyberpunkMP calls Get{Head,Body,Arms}Customization (AppearanceSystem);
    // verify those three against the 2.5 executable before shipping appearance sync on 2.5.
    virtual bool IsFinalized(void) const; // 108
    virtual TweakDBID const & GetLifePath(void) const; // 110 maybe?
    virtual void SetLifePath(TweakDBID const &); // 118 maybe?
    virtual bool IsBodyGenderMale(void) const; // 120
    virtual void SetIsBodyGenderMale(bool); // 128
    virtual bool IsBrainGenderMale(void) const; // 130
    virtual void SetIsBrainGenderMale(bool); // 138
    virtual void sub_140(); // 140
    virtual void sub_148(); // 148
    virtual unsigned int GetAttributePointsAvailable(void) const; // 150 maybe?
    virtual void SetAttributePointsAvailable(unsigned int); // 158
    virtual unsigned int GetAttribute(uint32_t) const; // 160
    virtual void SetAttribute(uint32_t, unsigned int); // 168
    virtual DynArray<uint64_t> GetAttributes(void) const; // 170
    virtual void ClearAttributes(void); // 178
    virtual void sub_180(); // 180
    virtual void sub_188(); // 188
    virtual bool GetHeadCustomization(CName aGroup, bool aIsTPP, DynArray<world::EntityAppearanceChangeParameter::Key> &) const; // 190
    virtual bool GetHeadCustomization(CName aGroup, bool aIsTPP, DynArray<uint64_t> &) const; // 198
    virtual bool GetBodyCustomization(CName aGroup, bool aIsTPP, DynArray<world::EntityAppearanceChangeParameter::Key> &) const; // 1A0
    virtual bool GetBodyCustomization(CName aGroup, bool aIsTPP, DynArray<uint64_t> &) const; // 1A8
    virtual bool GetArmsCustomization(CName aGroup, bool aIsTPP, DynArray<world::EntityAppearanceChangeParameter::Key> &) const; // 1B0
    virtual bool GetArmsCustomization(CName aGroup, bool aIsTPP, DynArray<uint64_t> &) const; // 1B8
    virtual void ResetEntityWeights(Handle<ent::Entity> &); // 1C0 ?
    virtual bool ApplyHead(CName aGroup, bool aIsTPP, Handle<game::Object> const &); // 1C8 ?
    virtual bool ApplyBody(CName aGroup, bool aIsTPP, Handle<game::Object> const &); // 1D0 ?
    virtual bool ApplyArms(CName aGroup, bool aIsTPP, Handle<game::Object> const &); // 1D8 ?
    virtual bool MorphHead(CName aGroup, bool aIsTPP, Handle<game::Object> const &, bool aEnabled) const; // 1E0
    virtual bool MorphBody(CName aGroup, bool aIsTPP, Handle<game::Object> const &, bool aEnabled) const; // 1E8
    virtual bool MorphArms(CName aGroup, bool aIsTPP, Handle<game::Object> const &, bool aEnabled) const; // 1F0
    virtual bool IsNewGame(void) const; // 1F8
    virtual void sub_200(); // 200
    virtual bool IsJudyRomanceable(void) const; // 208
    virtual bool IsRiverRomanceable(void) const; // 210
    virtual bool IsPanamRomanceable(void) const; // 218
    virtual bool IsKerryRomanceable(void) const; // 220
    virtual bool HasOption(CName, bool, CName) const; // 228
    virtual bool HasDifferentPerspectives(CName, bool) const; // 230
    virtual bool HasTag(CName) const; // 238


};
RED4EXT_ASSERT_SIZE(ICharacterCustomizationState, 0x40);
} // namespace game::ui
using gameuiICharacterCustomizationState = game::ui::ICharacterCustomizationState;
} // namespace RED4ext

// clang-format on
