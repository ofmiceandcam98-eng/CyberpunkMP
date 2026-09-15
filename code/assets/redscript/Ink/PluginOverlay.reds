module CyberpunkMP.Ink

import CyberpunkMP.*

// CLIENT EXTENSION-POINT PROOF (2026-09-15). See the strategy roadmap Phase 1 finding.
//
// Today every HUD overlay (server list, emote wheel, job list, delivery list) is a hardcoded,
// copy-pasted block inside MultiplayerGameController: its own field, Show*, On*Spawned,
// On*Action and a named branch in OnAction. Adding one means editing the controller in half a
// dozen places - which, for a plugin, is a core modification (a license 1.3 derivative that
// cannot be closed or sold).
//
// This is the first step off that: an overlay becomes a self-contained MpPluginOverlay
// subclass, and the controller DISPATCHES to a registry of them in OnAction instead of naming
// each feature. Adding an overlay is then a new subclass plus one line in the registry seed
// below - the controller's input handling never grows another branch.
//
// What this proof does NOT yet do, tracked in /mnt/vol/projects/_strategy/extraction-roadmap.md:
//   * Overlays still live in this (Ink) module and receive the concrete controller, because
//     redscript single-inheritance blocks a host-interface mixin and the controller already
//     extends inkHUDGameController. A generic MpOverlayHost surface is the next step, and is
//     what lets an overlay move to Plugins/ (§2.1 plugin-shaped, importing no core).
//   * Registration is a seed list here. Native RTTI auto-discovery (the way ClientRpc
//     subclasses are already found) is the last mile to a zero-core-edit registry.
//   * The spawn and behaviour of each overlay still live in the controller; this moves the
//     TRIGGER only. Migrating the behaviour into the overlay is the per-feature follow-up.
public abstract class MpPluginOverlay extends IScriptable {
    // A stable id, for the host to key its open/close/state on.
    public func GetId() -> CName {
        return n"";
    }

    // React to a HUD input action. Return true if this overlay consumed it, exactly as the
    // hardcoded branch it replaces did. The controller is passed so the overlay can drive the
    // existing Show* path - a narrower host interface replaces this once single-inheritance is
    // worked around (see the note above).
    public func HandleAction(controller: ref<MultiplayerGameController>, actionName: CName,
                             actionType: gameinputActionType) -> Bool {
        return false;
    }
}

// The registry seed - one line per overlay. A free function so the list has one home instead
// of being scattered through the controller; native auto-discovery replaces it later.
public func MpBuildOverlayRegistry() -> array<ref<MpPluginOverlay>> {
    let overlays: array<ref<MpPluginOverlay>>;
    ArrayPush(overlays, new MpEmoteOverlay());
    return overlays;
}
