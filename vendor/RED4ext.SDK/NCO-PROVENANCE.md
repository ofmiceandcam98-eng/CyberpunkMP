# Where this copy of RED4ext.SDK comes from

This directory is a vendored, modified copy of [RED4ext.SDK](https://github.com/WopsS/RED4ext.SDK)
(MIT, see `LICENSE.md`). It used to be a git submodule pointing at
[tiltedphoques/RED4ext.SDK](https://github.com/tiltedphoques/RED4ext.SDK); it was brought in-tree on
2026-11-01 for the Cyberpunk 2077 patch 2.5 port, which flattened its git history. This file is that
history, kept as knowledge: **the next game patch's port starts by diffing this tree against the
upstream base named below.**

## Bases

| | repository | commit | date |
|---|---|---|---|
| Upstream base | WopsS/RED4ext.SDK | `5246e659` "Update types for patch 2.5 (#232)" | 2026-10-24 |
| Merged in | tiltedphoques/RED4ext.SDK | `f52af6a3` "Update HashMap.hpp" | 2024-11-26 |
| Common ancestor | WopsS/RED4ext.SDK | `455d9b8b` | 2024-11-15 |

To rebuild the history: clone WopsS/RED4ext.SDK, add tiltedphoques/RED4ext.SDK as a remote, and
`git merge f52af6a3` onto `5246e659`; the resolutions below reproduce this tree.

## The CyberpunkMP decodes that were merged in

The tiltedphoques fork carried the original CyberpunkMP team's hand-decoded engine structures, which
upstream never had. Authored commits (merge commits omitted):

- `f52af6a3` 2024-11-26 maximegmd — Update HashMap.hpp
- `e8b77f14` 2024-09-27 Jack Humbert — more vehicle stuff
- `bd9008f3` 2024-09-25 maximegmd — Update moveComponent.hpp
- `fc2c8821` 2024-09-19 Jack Humbert — constructors
- `d5dbb62c` 2024-09-19 Jack Humbert — mounting slot cast
- `996720f4` 2024-09-18 Jack Humbert — veh stuff
- `4103917e` 2024-09-17 Jack Humbert — some vehicle stuff
- `46c826d7` 2024-09-15 maximegmd — fix: missing hash & functions
- `61715539` 2024-09-15 maximegmd — Update entAnimationControllerComponent.hpp
- `0bd19641` 2024-09-15 maximegmd — red4ext build fix
- `f09736c7` 2024-04-03 Jack Humbert — inventory stuff
- `dd782d8e` 2024-03-29 Jack Humbert — typedefs
- `1d8115d1` 2024-03-25 Jack Humbert — update rtti args
- `91db3a5d` 2024-02-28 Jack Humbert — add ccState virtuals
- `bc64976e` 2024-02-23 maximegmd — performance issues
- `7ac4bbb3` 2024-02-17 maximegmd — feat: puppet's move component
- `df3b2212` 2024-02-17 maximegmd — Update Vector3.hpp
- `12c828aa` 2024-02-13 Vincent Hengel — inventory related reverse
- `a97b9790` 2024-02-13 maximegmd — Update entEntity.hpp
- `07feb668` 2024-02-13 maximegmd — ignore: component storage
- `a81f5503` 2024-02-12 Jack Humbert — add queueevent to uisystem
- `955c3155` 2024-02-12 maximegmd — Update entEntity.hpp
- `2bf9da8a` 2024-02-12 maximegmd — const correctness for fixedpoints
- `bb3c0765` 2024-02-10 maximegmd — autormatic conversion of fixed point to float
- `ee5a2980` 2024-02-09 maximegmd — some reverse
- `fa1def5a` 2024-02-03 maximegmd — Disable CI
- `2d29605a` 2024-01-27 Jack Humbert — add widget stuff

## How the merge was resolved

**Infrastructure → upstream.** Where the fork's edits sat on code upstream later rewrote, upstream's
2.5 version was kept: `NativeTypes`, `RTTITypes` (now `rtti::IType`), `DynArray`, `Callback` and its
`Detail/` pieces (the 2.5 64-byte inline buffer), `JobQueue` (refcounted `JobHandle`),
`Detail/AddressHashes`, `Detail/Memory`, `Relocation-inl`, `ResourceLoader`, `src/Buffer.cpp`,
`src/Package.cpp`. The fork had also moved `GlobalNodeRef` into its own header; that was reverted to
upstream's `NodeRef.hpp`, which is where RedLib expects it.

**Decodes → carried to 2.5 offsets, each pinned with `RED4EXT_ASSERT_SIZE` / `_OFFSET`:**

| struct | 2.5 placement and evidence |
|---|---|
| `ink::TextWidget`, `ink::TextInputWidget` | +0x30; every reflected gap identical to the generated 2.5 header. `caretPosition` 0x464 is inferred (unreflected tail kept its 0x40 size). |
| `ent::Entity`, `ent::IComponent` | fork names over upstream's 2.5 layout - offsets identical. |
| `ent::ComponentsStorage` | identical to upstream 2.5 (0x40). |
| `move::Component` | upstream's 2.5 decode plus the fork's `MovementRepresentation` at 0x138 (size and every upstream anchor unchanged). |
| `vehicle::BaseObject`, `vehicle::WheeledBaseObject` | placed from Jack Humbert's own 2.5 update (jackhumbert/RED4ext.SDK `155d285a`, 2026-10-25): `rigidBody` 0x2D8 (his `physicsData`), `worldTransform` 0x350, `runtimeData` 0x5F0, `engineData` 0xBD0; `RigidBody::currentTransform` 0x150. |

**Carried forward UNVERIFIED for 2.5** (opaque growth, insertion point unknown - marked in place):
`game::ui::CharacterCustomizationState` (0xF0 → 0x110) and the `ICharacterCustomizationState` vtable;
`game::data::Item_Record` (0x478 → 0x488).

**Retired:** `Scripting/Natives/entPuppet.hpp` - its `moveComponent` handle offset was already wrong on
2.31 and nothing read it; `game::Puppet` is upstream's 2.5 struct.

## Local patches to upstream code

- `Scripting/Natives/gameIEntityStubSystem.hpp`: includes `Containers/StaticArray.hpp`, which it uses
  (upstream compiled only by include order).

Outside this directory, the same port touched: `vendor/xmake.lua` (exposes the bundled
`vendor/D3D12MemAlloc` include path, as upstream's CMake does) and the client's RedLib copy in
`code/client/Red/` (upstream RedLib `a88797f`, minus the `TDataBuffer` wrapper mapping, which needs a
type only psiberx's SDK fork has; `code/client/stdafx.h` pins `Red::SharedPtr` to the engine's).
