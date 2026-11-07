#pragma once

#include <RED4ext/CName.hpp>
#include <RED4ext/Common.hpp>
#include <RED4ext/Containers/DynArray.hpp>
#include <RED4ext/Handle.hpp>
#include <RED4ext/Scripting/IScriptable.hpp>
#include <RED4ext/Scripting/Natives/Generated/HDRColor.hpp>
#include <RED4ext/Scripting/Natives/Generated/RectF.hpp>
#include <RED4ext/Scripting/Natives/Generated/Vector2.hpp>
#include <RED4ext/Scripting/Natives/Generated/ink/UITransform.hpp>
#include <RED4ext/Scripting/Natives/Generated/ink/WidgetLayout.hpp>

namespace RED4ext::ink
{
struct IEffect;
struct LayerProxy;
struct PropertyManager;
struct StyleResourceWrapper;
struct UserData;
struct WidgetLogicController;

struct Widget : IScriptable
{
    static constexpr const char* NAME = "inkWidget";
    static constexpr const char* ALIAS = NAME;
    
    virtual void sub_108();                // 108 OnInitialize
    virtual void sub_110();                // 110
    virtual void sub_118();                // 118
    virtual void sub_120();                // 120
    virtual void sub_128();                // 128
    virtual void sub_130(CName parameter) { }; // 130 update parameter?
    virtual void sub_138();                // 138
    virtual void sub_140();                // 140
    virtual void sub_148();                // 148
    virtual void sub_150();                // 150
    virtual void OnTintColorChanged() { }; // 158
    virtual void sub_160();                // 160 gets unk178
    virtual void FlexCalc();               // 168
    virtual void sub_170();                // 170 calls sub_168
    virtual void OnFocusReceived();        // 178
    virtual void OnFocusLost();            // 180
    virtual void OnCharacterKey();         // 188
    virtual void OnPress();                // 190
    virtual void OnHold();                 // 198
    virtual void OnRepeat();               // 1A0
    virtual void OnRelease();              // 1A8
    virtual void OnClick();                // 1B0
    virtual void OnReleaseOutside();       // 1B8
    virtual void OnDoubleClick();          // 1C0
    virtual void OnEnter();                // 1C8
    virtual void OnLeave();                // 1D0
    virtual void OnHoverOver();            // 1D8
    virtual void OnHoverOut();             // 1E0
    virtual void OnAxis();                 // 1E8
    virtual void OnRelative();             // 1F0
    virtual void OnSetCursorVisiblity();   // 1F8
    virtual void OnSetCursorPosition();    // 200
    virtual void OnSetCursorScale();       // 208
    virtual void OnSetCursorContext();     // 210
    virtual void OnSetCursorType();        // 218
    virtual void OnSetCursorForDevice();   // 220
    virtual void OnDpadCursorMove();       // 228
    virtual void sub_230(uint64_t) { };    // 230
    virtual void sub_238() { };            // 238
    virtual void sub_240();                // 240

    /// @pattern 40 53 48 83 EC 30 0F 29 74 24 20 48 8B D9 0F 28 F1 F3 0F 10 89 F0 01 00 00 0F 28 C1 F3 0F 5C C6
    /// @version 2.10
    /// @rva 0x262F94
    void __fastcall SetOpacity(float opacity);

    /// @pattern 40 53 48 83 EC 20 F3 0F 10 0A 48 8B D9 0F 2E 89 98 01 00 00 75 14 F3 0F 10 42 04 0F 2E 81 9C 01
    /// @version 2.10
    /// @rva 0x1C6E80
    void __fastcall SetSize(Vector2 *size);

    /// @pattern 40 53 48 83 EC 20 48 8B D9 3A 91 FA 01 00 00 74 17 80 3D 05 04 F9 02 00 88 91 FA 01 00 00 74 0E
    /// @version 2.10
    /// @rva 0x422720
    void __fastcall SetFitToContent(bool fitToContent);

    /// @pattern 48 89 5C 24 10 48 89 7C 24 18 55 48 8B EC 48 83 EC 60 80 89 FD 01 00 00 10 48 8D 55 F0 E8 CA 02
    /// @version 2.10
    /// @rva 0x14197C
    void __fastcall Invalidate();

    Handle<PropertyManager> propertyManager;                      // 40
    HashMap<CName, CBaseFunction> scriptCallbacks;                // 50
    SharedSpinLock scriptCallbacksMutex;                          // 80
    uint32_t unk84;                                               // 84  
    WidgetLayout layout;                                          // 88
    UITransform renderTransform;                                  // B8
    uint8_t unkE8[0xF8 - 0xE8];                                   // E8
    DynArray<Handle<UserData>> userData;                          // F8
    DynArray<Handle<WidgetLogicController>> secondaryControllers; // 108
    DynArray<Handle<IEffect>> effects;                            // 118
    Handle<WidgetLogicController> logicController;                // 128
    Handle<StyleResourceWrapper> style;                           // 138
    Handle<LayerProxy> layerProxy;                                // 148
    uint8_t unk158[0x168 - 0x158];                                // 158
    WeakHandle<Widget> parentWidget;                              // 168
    uint8_t unk178[0x188 - 0x178];                                // 178
    CName name;                                                   // 188
    CName state;                                                  // 190
    Vector2 size;                                                 // 198
    Vector2 desiredSize;                                          // 1A0
    Vector2 renderTransformPivot;                                 // 1A8
    uint8_t unk1B0[0x1D0 - 0x1B0];                                // 1B0
    DynArray<void*> unk1D0;                                       // 1D0 size of 0x60
    HDRColor tintColor;                                           // 1E0
    float opacity;                                                // 1F0
    SharedSpinLock parentLock;                                    // 1F4
    SharedSpinLock unk1F5;                                        // 1F5
    SharedSpinLock unk1F6;                                        // 1F6
    SharedSpinLock userDataLock;                                  // 1F7
    bool visible;                                                 // 1F8
    bool canSupportFocus;                                         // 1F9
    bool fitToContent;                                            // 1FA
    bool isInteractive;                                           // 1FB
    bool affectsLayoutWhenHidden;                                 // 1FC
    uint8_t flags;                                                // 1FD
    uint8_t unk1FE[0x210 - 0x1FE];                                // 1FE
};
RED4EXT_ASSERT_SIZE(Widget, 0x210);
RED4EXT_ASSERT_OFFSET(Widget, tintColor, 0x1E0);
RED4EXT_ASSERT_OFFSET(Widget, opacity, 0x1F0);
RED4EXT_ASSERT_OFFSET(Widget, visible, 0x1F8);
RED4EXT_ASSERT_OFFSET(Widget, flags, 0x1FD);
} // namespace RED4ext::ink
