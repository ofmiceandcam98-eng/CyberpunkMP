module CyberpunkMP.Ink

import CyberpunkMP.*

// The emote wheel, expressed as a registry overlay instead of a hardcoded UIEmote branch in
// MultiplayerGameController.OnAction. First customer of the client extension point, and step
// one of moving emotes server-side (Atlas: emotes-should-become-a-server-side-plugin).
//
// DELIBERATELY behaviour-preserving. The wheel's spawn, its radial mouse input, the pause /
// time-dilation / blur / rumble and the EmoteServer.TriggerEmote call all stay exactly where
// they were, in the controller's ShowEmoteSelector / OnEmoteSelectorSpawned /
// OnEmoteSelectorClosed. This class moves only the OPEN/CLOSE TRIGGER: press UIEmote to open,
// release to close - the same two conditions the old inline branches used
// (BUTTON_PRESSED + !open, BUTTON_RELEASED + open). Migrating the behaviour itself into the
// overlay is a later, per-feature step once a generic host surface exists.
public class MpEmoteOverlay extends MpPluginOverlay {
    public func GetId() -> CName {
        return n"emote";
    }

    public func HandleAction(controller: ref<MultiplayerGameController>, actionName: CName,
                             actionType: gameinputActionType) -> Bool {
        if Equals(actionName, n"UIEmote")
            && Equals(actionType, gameinputActionType.BUTTON_PRESSED)
            && !controller.IsEmoteSelectorOpen() {
            controller.ShowEmoteSelector(true);
            return true;
        }

        if Equals(actionName, n"UIEmote")
            && Equals(actionType, gameinputActionType.BUTTON_RELEASED)
            && controller.IsEmoteSelectorOpen() {
            controller.ShowEmoteSelector(false);
            return true;
        }

        return false;
    }
}
