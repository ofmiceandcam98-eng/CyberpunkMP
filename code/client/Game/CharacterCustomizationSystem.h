#pragma once

#include <RED4ext/Scripting/Natives/Generated/game/ui/CharacterCustomizationSystem.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/ui/CharacterCustomizationState.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/ui/CharacterCustomizationPreset.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/data/Item_Record.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/data/AttachmentSlot_Record.hpp>
#include <RED4ext/Scripting/Natives/Generated/ink/GameSettingsResource.hpp>
#include <RED4ext/Scripting/Natives/CResource.hpp>
#include <RED4ext/Scripting/Natives/Generated/world/RuntimeSystemEntityAppearanceChanger.hpp>
#include "Game/Utils.h"

using namespace Red;

constexpr auto CreateHandle_CharacterCustomizationState = Core::RawFunc<2710710832UL, 
    Handle<game::ui::CharacterCustomizationState> * (*)(Handle<game::ui::CharacterCustomizationState> *)>();


constexpr auto CharacterCustomizationState_InitFromPreset = Core::RawFunc<1993027039UL, 
    Handle<game::ui::CharacterCustomizationState> * (*)(game::ui::CharacterCustomizationState *, Handle<game::ui::CharacterCustomizationPreset> *)>();

constexpr auto CharacterCustomizationState_Serialize = Core::RawFunc<108403442UL, 
    void (*)(game::ui::CharacterCustomizationState *, CMPStream *)>();

// constexpr auto gameTransactionSystem_AddItemToSlot = Core::RawFunc<56564470UL, 
//     void (*)(game::TransactionSystem *, const AddItemToSlotContext *)>();

constexpr auto GetGameSettings = Core::RawFunc<394336209UL, 
    ink::GameSettingsResource const * (*)(void)>();

// constexpr auto ItemID_SetUniqueCounterValue = Core::RawFunc<1713639304UL, void (*)(ItemID *)>();

constexpr auto ItemID_Create = Core::RawFunc<3020231866UL, ItemID * (*)(ItemID *, TweakDBID, int64_t seed)>();

constexpr auto GetItemRecord = Core::RawFunc<3236045910UL, 
    Handle<game::data::Item_Record> * (Handle<game::data::Item_Record>::*)(TweakDBID)>();

constexpr auto GetPlacementSlot = Core::RawFunc<1934369954UL, 
    Handle<game::data::AttachmentSlot_Record> * (game::data::Item_Record::*)(Handle<game::data::AttachmentSlot_Record> *, int32_t)>();

constexpr auto EvalArrayTweakDBID = Core::RawFunc<523315084UL, 
    DynArray<TweakDBID> const & (game::data::Val::*)(void)>();

constexpr auto EvalCName = Core::RawFunc<3648983380UL, 
    CName const & (game::data::Val::*)(void)>();

constexpr auto ResolveSuffix = Core::RawFunc<4124714348UL, 
    CString * (*)(void *, CString *, Handle<game::data::Item_Record> &, bool isMale)>();

constexpr auto ResolveVisualTags = Core::RawFunc<1337337421UL, 
    CString * (*)(CName, CString *, Handle<game::data::Item_Record> &)>();

constexpr auto GetItemAppearanceName = Core::RawFunc<3029088864UL, 
    CString * (*)(CString *, Handle<game::Object> const &, Handle<game::Object> const &, Handle<game::data::Item_Record> const &, ItemID const &)>();

template <class T>
struct Span 
{
    T * start;
    T * end;
};

constexpr auto ScheduleSynchronizedAppearanceChanges = Core::RawFunc<386815609UL, 
    void (*)(world::RuntimeSystemEntityAppearanceChanger *, 
        WeakHandle<game::Object> &, 
        Span<world::EntityAppearanceChangeParameter::Key> * old_keys,
        Span<world::EntityAppearanceChangeParameter::Key> * new_keys,
        std::function<void (void)> const &,
        uint32_t //io::EAsyncPriority
    )>();

// The appearance keys a customization state resolves to, as one list for the changer above.
//
// The group set the remote dressing path has used since 2.31, and the set that dresses remote
// players correctly on 2.5. Shared with the local-body experiment
// (NetworkWorldSystem::DressLocalBodyFromStoredLook) so the two cannot drift apart: whatever
// dresses somebody else properly is exactly what the experiment tries on yourself.
inline void CollectCustomizationKeys(game::ui::CharacterCustomizationState* apState,
                                     DynArray<world::EntityAppearanceChangeParameter::Key>& aKeys)
{
    apState->GetHeadCustomization("TPP", true, aKeys);
    apState->GetHeadCustomization("face", true, aKeys);
    apState->GetHeadCustomization("hairs", true, aKeys);
    if (apState->isBodyGenderMale)
    {
        apState->GetHeadCustomization("beards", true, aKeys);
    }

    // does all of them
    // apState->GetHeadCustomization("character_customization", true, aKeys);

    apState->GetBodyCustomization("TPP_Body", true, aKeys);
    // make genitals appear as well
    // apState->GetBodyCustomization("character_creation", true, aKeys);
    // pops through clothes lol
    // apState->GetBodyCustomization("genitals", true, aKeys);
    apState->GetBodyCustomization("breast", true, aKeys);
    apState->GetBodyCustomization("lifted_feet", true, aKeys);
    apState->GetBodyCustomization("flat_feet", true, aKeys);

    // apState->GetArmsCustomization("character_customization", true, aKeys);
    apState->GetArmsCustomization("holstered_default", true, aKeys);
    apState->GetArmsCustomization("nails", true, aKeys);
}

// CharacterCustomizationSystem
// THandle<game::ui::CharacterCustomizationState> unk78

// could use game::ui::CharacterCustomizationState::Serialize to pass data to server

// Whether this is the game build the client's hand-decoded internals were read from.
//
// Every raw offset, hand-numbered vtable slot and engine-struct mimic in the client was
// decoded on 2.31 and carried into the 2.5 SDK merge unverified - and on 2.5 they have
// overwritten a character, crashed every client dressing a remote player, and crashed every
// client moving one. There is no direct way to ask the engine which build it is, so this
// uses a fingerprint: gameuiCharacterCustomizationSystem is 0x388 bytes on 2.31 and 0x3D8
// on 2.5. A code path that depends on a 2.31 decode asks this and takes a safer route when
// the answer is no.
inline bool IsGame231Layout()
{
    static const bool s_is231 = []
    {
        const auto* pClass = Red::GetClass("gameuiCharacterCustomizationSystem");
        return pClass && pClass->size == 0x388;
    }();

    return s_is231;
}

// The creator's state, read raw at +0x78 - an offset decoded on the 2.31 layout (class size 0x388)
// and never re-decoded. Game 2.5 grew the class to 0x3D8 and the slot stopped being empty in
// ordinary play: the 90-second automatic save, which relies on "no state outside the creator",
// serialised the default body and stored it over the player's character every time it ran
// (zeldfep's Pajeet, 2026-11-01). On any other layout this answers "no state" - creator and
// ripperdoc saves stop, which is recoverable; overwriting somebody's face is not.
inline Handle<game::ui::CharacterCustomizationState> * GetCustomizationState(game::ui::CharacterCustomizationSystem * self)
{
    // static: the lambda below has no captures, and passing this to spdlog binds a reference to
    // it, which needs an object it can reach. The CI compiler refuses a plain local (C3493).
    static constexpr uint32_t kDecodedLayoutSize = 0x388;

    static Handle<game::ui::CharacterCustomizationState> s_none;
    static const bool s_layoutKnown = []
    {
        const auto* pClass = Red::GetClass("gameuiCharacterCustomizationSystem");
        const auto size = pClass ? pClass->size : 0u;
        const bool known = size == kDecodedLayoutSize;
        if (!known)
            spdlog::warn("[Character] CharacterCustomizationSystem is 0x{:X} bytes, the +0x78 creator-state read was "
                         "decoded on 0x{:X} - creator and ripperdoc appearance saves are OFF on this game version",
                         size, kDecodedLayoutSize);
        return known;
    }();

    if (!self || !s_layoutKnown)
        return &s_none;

    return reinterpret_cast<Handle<game::ui::CharacterCustomizationState> *>((uintptr_t)self + 0x78);
}
