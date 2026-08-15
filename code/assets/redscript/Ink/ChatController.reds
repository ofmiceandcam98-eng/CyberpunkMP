module CyberpunkMP.Ink

// import Codeware.UI.*
import CyberpunkMP.*
import CyberpunkMP.World.*

public class ChatController extends inkHUDGameController {
    private let m_messagesRef: inkWidgetRef;
    private let m_inputRef: inkTextInputRef;
    private let m_scrollRef: inkScrollAreaRef;

    private let m_uiSystem: wref<UISystem>;
    private let m_player: wref<PlayerPuppet>;
    private let m_repeatingScrollActionEnabled: Bool = false;
    private let m_messageController: ref<ListController>;
    private let m_chatInputOpen: Bool;
    private let m_input: wref<inkTextInput>;
    private let m_lastMessageData: ref<ChatMessageData>;

    // The input box is currently asking for a character name, not a chat message.
    //
    // A mode flag rather than a second widget. Everything a name prompt needs - a focused
    // text field, a modal input context, Enter to commit, Escape to leave - is what the
    // chat input already is, and all of it took real debugging to get working. A separate
    // prompt would be a second copy of that, able to break on its own.
    private let m_namePromptOpen: Bool;
    private let m_nameLabel: wref<inkText>;

    protected cb func OnInitialize() -> Bool {
        FTLog(s"[ChatController] OnInitialize");
        this.m_player = this.GetPlayerControlledObject() as PlayerPuppet;
        if !IsDefined(this.m_player) {
            FTLog(s"[ChatController] NO PLAYER");
        }
        this.m_uiSystem = GameInstance.GetUISystem(this.m_player.GetGame());
        this.m_messageController = inkWidgetRef.GetController(this.m_messagesRef) as ListController;
        this.m_input = inkWidgetRef.Get(this.m_inputRef) as inkTextInput;
        this.m_player.RegisterInputListener(this, n"UIEnterChatMessage");
        // get last messages & populate list

        this.UpdateInputHints();

        // The chat widget is forced visible rather than trusted to be.
        //
        // Cam's session proved the logic works and the rendering does not: the controller
        // initialised, messages arrived (OnChatMessageUIEvent fired repeatedly), typed text
        // reached SendChat, and the server logged the commands he sent - all with no chat
        // box on screen at any point. So every widget resolves and every handler runs; only
        // the pixels are missing.
        //
        // Nothing in OnInitialize made the widget visible. Its on-screen state came purely
        // from whatever the .inkwidget authored, plus the to_input/from_input animations -
        // and neither animation has played when the HUD first appears. If the authored
        // default is transparent, chat is invisible forever and every handler still works
        // perfectly, which is exactly the shape of what happened.
        //
        // Setting it explicitly costs nothing if it was already visible.
        this.ForceVisible();

        let messageData = new ChatMessageUIEvent();
        messageData.author = "SERVER";
        messageData.message = "Connected to...";
        this.QueueEvent(messageData);
    }

    // Makes the chat widget and its parts visible and opaque, and reports what they were.
    //
    // The log lines matter as much as the assignments. If chat is still invisible after
    // this, the values say which widget is wrong and how - a null means the path is wrong,
    // a zero size means layout, and correct-looking values everywhere mean the problem is
    // a parent or the render order rather than anything in this file. Guessing again
    // without them would cost another round trip.
    private func ForceVisible() -> Void {
        let root = this.GetRootWidget();
        if IsDefined(root) {
            FTLog(s"[ChatController] root visible=\(root.IsVisible()) opacity=\(root.GetOpacity())");
            root.SetVisible(true);
            root.SetOpacity(1.0);
        } else {
            FTLogError(s"[ChatController] no root widget");
        }

        let names = ["wrapper", "wrapper/chat", "wrapper/chat/bg", "wrapper/input_box"];
        for name in names {
            let widget = this.GetWidget(StringToName(name));
            if IsDefined(widget) {
                let size = widget.GetSize();
                FTLog(s"[ChatController] '\(name)' visible=\(widget.IsVisible()) opacity=\(widget.GetOpacity()) size=\(size.X)x\(size.Y)");
                widget.SetVisible(true);
            } else {
                FTLogWarning(s"[ChatController] '\(name)' does not resolve");
            }
        }
    }

    protected cb func OnUninitialize() -> Bool {
        FTLog(s"[ChatController] OnUninitialize");
        this.m_player.UnregisterInputListener(this, n"UIEnterChatMessage");
    }

    private func UpdateInputHints() -> Void {
        let evt = new UpdateInputHintMultipleEvent();
        evt.targetHintContainer = n"GameplayInputHelper";
        evt.AddInputHint(CreateInputHint(n"Chat", n"UIEnterChatMessage", false), !this.m_chatInputOpen);
        evt.AddInputHint(CreateInputHint(n"Cancel", n"back", false), this.m_chatInputOpen);
        evt.AddInputHint(CreateInputHint(n"Send", n"EnterChat", false), this.m_chatInputOpen);
        evt.AddInputHint(CreateInputHint(n"Scroll up", n"navigate_up", false), this.m_chatInputOpen);
        evt.AddInputHint(CreateInputHint(n"Scroll down", n"navigate_down", false), this.m_chatInputOpen);
        evt.AddInputHint(CreateInputHint(n"Top of chat", n"ChatTop", false), this.m_chatInputOpen);
        evt.AddInputHint(CreateInputHint(n"End of chat", n"ChatBottom", false), this.m_chatInputOpen);
        this.m_uiSystem.QueueEvent(evt);
    }

    protected cb func OnChatMessageUIEvent(evt: ref<ChatMessageUIEvent>) -> Bool {
        FTLog(s"[ChatController] OnChatMessageUIEvent");
        let messageData = new ChatMessageData();
        messageData.m_author = evt.author;
        messageData.m_message = evt.message;
        messageData.m_channel = evt.channel;
        messageData.m_isSelf = Equals(StringToName(evt.author), StringToName(GameInstance.GetNetworkWorldSystem().GetChatSystem().GetUsername()));
        if IsDefined(this.m_lastMessageData) {
            messageData.m_needsAuthorLabel = NotEquals(StringToName(this.m_lastMessageData.m_author), StringToName(messageData.m_author));
        } else {
            messageData.m_needsAuthorLabel = true;
        }
        this.m_lastMessageData = messageData;
        inkScrollAreaRef.ScrollVertical(this.m_scrollRef, 0.0);
        this.m_messageController.PushData(messageData, true);

        let targets = new inkWidgetsSet();
        targets.Select(this.m_messageController.GetItemAt(this.m_messageController.Size() - 1));
        this.PlayLibraryAnimationOnTargets(n"new_message", targets);

        inkScrollAreaRef.ScrollVertical(this.m_scrollRef, 1.0);
    }

    // The server wants a name for this character.
    protected cb func OnCharacterNameRequest(evt: ref<CharacterNameRequest>) -> Bool {
        FTLog(s"[ChatController] character name requested (currently '\(evt.m_current)')");

        this.m_namePromptOpen = true;

        this.ShowNameLabel(true);
        this.ShowChatInput(true);

        // Pre-filled when they already have a name, so editing it does not mean retyping
        // it. Empty for a brand new character, where there is nothing to preserve.
        this.m_input.SetText(evt.m_current);

        // Said in chat as well as shown on the label.
        //
        // The label is built at runtime and reparented into a widget from a compiled ink
        // asset, which is the one part of this that cannot be checked without running the
        // game. If it lands somewhere invisible, this line is still there and still says
        // what the box wants. Cheap insurance against a silent failure that would look
        // like the box appearing for no reason.
        let notice = new ChatMessageUIEvent();
        notice.author = "SERVER";
        notice.message = "What is your character called? Type a name and press Enter.";
        this.QueueEvent(notice);
    }

    // Creates the CHARACTER NAME label the first time it is needed, then shows or hides it.
    //
    // Built in script rather than added to multiplayer_ui.inkwidget because that file is a
    // compiled binary - editing it needs WolvenKit and a rebuild of the archive, which is a
    // much longer road for one line of text.
    //
    // Every step is guarded and failure is silent on purpose: a missing label leaves a
    // working prompt with no caption, which is far better than a script error, and one bad
    // script takes the whole mod's compilation down with it.
    private func ShowNameLabel(show: Bool) -> Void {
        if !IsDefined(this.m_nameLabel) {
            if !show {
                return;
            }

            let parent = this.GetWidget(n"wrapper/input_box") as inkCompoundWidget;
            if !IsDefined(parent) {
                FTLogWarning(s"[ChatController] no input_box to hang the name label on");
                return;
            }

            let label = new inkText();
            label.SetName(n"mp_character_name_label");
            label.SetText("CHARACTER NAME");
            label.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
            label.SetFontStyle(n"Medium");
            label.SetFontSize(28);
            label.SetLetterCase(textLetterCase.UpperCase);
            label.SetAnchor(inkEAnchor.TopLeft);

            // Sits above the box rather than inside it - a negative top margin lifts it
            // clear of the input, which already fills its own row.
            label.SetMargin(new inkMargin(12.0, -34.0, 0.0, 0.0));

            // The same yellow the game uses for prompts, so it reads as the game asking.
            label.SetTintColor(new HDRColor(2.0, 1.75, 0.25, 1.0));

            label.Reparent(parent);

            this.m_nameLabel = label;
        }

        this.m_nameLabel.SetVisible(show);
    }

    // Commits whatever is in the box as the character's name.
    private func SendName() -> Void {
        let wanted = this.m_input.GetText();

        if Equals(wanted, "") {
            let notice = new ChatMessageUIEvent();
            notice.author = "SERVER";
            notice.message = "A character needs a name - use /name when you have picked one.";
            this.QueueEvent(notice);
            return;
        }

        // Sent as the ordinary /name command. The server already validates, truncates to 32
        // and writes it through to the character record, so this reuses that rather than
        // adding a second path to the same field.
        FTLog(s"[ChatController] naming this character '\(wanted)'");
        GameInstance.GetNetworkWorldSystem().GetChatSystem().Send("/name " + wanted);
    }

    private func ShowChatInput(show: Bool) -> Void {
        let blackboardSystem: ref<BlackboardSystem> = GameInstance.GetBlackboardSystem(GetGameInstance());
        let uiBlackboard: ref<IBlackboard> = blackboardSystem.Get(GetAllBlackboardDefs().UIGameData);
        if show {
            let targets = new inkWidgetsSet();
            targets.Select(this.GetWidget(n"wrapper/input_box"));
            targets.Select(this.GetWidget(n"wrapper/input_box/size_provider"));
            targets.Select(this.GetWidget(n"wrapper/chat/mask"));
            targets.Select(this.GetWidget(n"wrapper/chat/bg"));
            this.PlayLibraryAnimationOnTargets(n"to_input", targets);

            // Same reasoning as ForceVisible: the animation is what was RELIED on to
            // reveal the input box, and the box demonstrably never appeared even though
            // everything downstream of it worked. Setting the state directly means the
            // box shows whether or not the animation plays, so this no longer depends on
            // an asset that cannot be inspected without WolvenKit.
            this.ForceVisible();

            let inputBox = this.GetWidget(n"wrapper/input_box");
            if IsDefined(inputBox) {
                inputBox.SetVisible(true);
                inputBox.SetOpacity(1.0);
            }

            this.RequestSetFocus(inkTextInputRef.Get(this.m_inputRef));
            uiBlackboard.SetBool(GetAllBlackboardDefs().UIGameData.UIChatInputContextRequest, true, true);
            this.m_player.RegisterInputListener(this, n"OpenPauseMenu");
            this.m_player.RegisterInputListener(this, n"back");
            this.m_player.RegisterInputListener(this, n"navigate_up");
            this.m_player.RegisterInputListener(this, n"navigate_down");
            this.m_player.RegisterInputListener(this, n"ChatTop");
            this.m_player.RegisterInputListener(this, n"ChatBottom");
            this.m_player.RegisterInputListener(this, n"EnterChat");

            // Mouse wheel, registered only while the chat input is open. Bound globally
            // it would fight weapon switching during normal play; while the input is up
            // the game is already in a modal context and the wheel has nothing else to do.
            //
            // These are OUR action names, declared in assets/Inputs/CyberpunkMP.xml
            // against IK_MouseWheelUp and IK_MouseWheelDown. There is no "mouse_wheel"
            // action in Cyberpunk - registering for one silently listens for something
            // that never fires, which is what the first attempt at this did.
            this.m_player.RegisterInputListener(this, n"ChatScrollUp");
            this.m_player.RegisterInputListener(this, n"ChatScrollDown");
        } else {
            this.m_repeatingScrollActionEnabled = false;
            let targets = new inkWidgetsSet();
            targets.Select(this.GetWidget(n"wrapper/input_box"));
            targets.Select(this.GetWidget(n"wrapper/input_box/size_provider"));
            targets.Select(this.GetWidget(n"wrapper/chat/mask"));
            targets.Select(this.GetWidget(n"wrapper/chat/bg"));
            this.PlayLibraryAnimationOnTargets(n"from_input", targets);

            uiBlackboard.SetBool(GetAllBlackboardDefs().UIGameData.UIChatInputContextRequest, false, true);
            this.m_player.UnregisterInputListener(this, n"OpenPauseMenu");
            this.m_player.UnregisterInputListener(this, n"back");
            this.m_player.UnregisterInputListener(this, n"navigate_up");
            this.m_player.UnregisterInputListener(this, n"navigate_down");
            this.m_player.UnregisterInputListener(this, n"ChatTop");
            this.m_player.UnregisterInputListener(this, n"ChatBottom");
            this.m_player.UnregisterInputListener(this, n"EnterChat");
            this.m_player.UnregisterInputListener(this, n"ChatScrollUp");
            this.m_player.UnregisterInputListener(this, n"ChatScrollDown");
            this.m_input.SetText("");
            this.RequestSetFocus(null);

            // Whatever the box was being used for, it is a chat box again now. Cleared
            // here rather than at each call site so no route out can leave the mode set
            // and turn somebody's next message into a rename.
            this.m_namePromptOpen = false;
            this.ShowNameLabel(false);
        }
        this.m_chatInputOpen = show;
        this.UpdateInputHints();
    }

    private final func SendChat() -> Void {
        let textEntered: String = this.m_input.GetText();
        if NotEquals(textEntered, "") {
            FTLog(s"[ChatController] SendChat \"\(textEntered)\"");

            GameInstance.GetNetworkWorldSystem().GetChatSystem().Send(textEntered);
        };
    }

    private func Scroll(up: Bool) {
        let contentSize = inkScrollAreaRef.GetContentSize(this.m_scrollRef);
        // let viewportSize = this.m_scrollArea.GetViewportSize();
        let delta = 100.0 / contentSize.Y;
        // if contentSize.Y > viewportSize.Y {
        //     delta = contentSize.Y;
        // }
        let current = inkScrollAreaRef.GetVerticalScrollPosition(this.m_scrollRef);
        if up {
            inkScrollAreaRef.ScrollVertical(this.m_scrollRef, current - delta);
        } else {
            inkScrollAreaRef.ScrollVertical(this.m_scrollRef, current + delta);
        }
    }

    protected cb func OnChatInputAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
        let actionName: CName = ListenerAction.GetName(action);
        let actionType: gameinputActionType = ListenerAction.GetType(action);
        if Equals(actionType, gameinputActionType.REPEAT) {
            if !this.m_repeatingScrollActionEnabled {
                return false;
            };
            switch actionName {
            case n"navigate_up":
                this.Scroll(true);
                return true;
                break;
            case n"navigate_down":
                this.Scroll(false);
                return true;
                break;
            };
        } else if Equals(actionType, gameinputActionType.BUTTON_PRESSED) {
            if !this.m_repeatingScrollActionEnabled {
                this.m_repeatingScrollActionEnabled = true;
            };
            switch actionName {
            case n"EnterChat":
                // The flag is read here, before ShowChatInput clears it below.
                if this.m_namePromptOpen {
                    this.SendName();
                } else {
                    this.SendChat();
                }
                this.ShowChatInput(false);
                return true;
                break;
            case n"OpenPauseMenu":
                ListenerActionConsumer.DontSendReleaseEvent(consumer);
                return true;
                break;
            case n"navigate_up":
                this.Scroll(true);
                return true;
                break;
            case n"navigate_down":
                this.Scroll(false);
                return true;
                break;
            case n"ChatScrollUp":
                this.Scroll(true);
                return true;
                break;
            case n"ChatScrollDown":
                this.Scroll(false);
                return true;
                break;
            case n"ChatTop":
                inkScrollAreaRef.ScrollVertical(this.m_scrollRef, 0.0);
                return true;
                break;
            case n"ChatBottom":
                inkScrollAreaRef.ScrollVertical(this.m_scrollRef, 1.0);
                return true;
                break;
            case n"back":
                // Escape leaves the name prompt too.
                //
                // Trapping somebody in a box until they type something is worse than
                // letting them out with the wrong name, and they may well want to look at
                // their character before deciding. Nothing is lost - they keep the
                // fallback name and the line below says how to change it.
                if this.m_namePromptOpen {
                    let notice = new ChatMessageUIEvent();
                    notice.author = "SERVER";
                    notice.message = "No name set - you can pick one any time with /name <name>.";
                    this.QueueEvent(notice);
                }
                this.ShowChatInput(false);
                return true;
                break;
            };
        };
        return false;
    }

    protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
        let actionName: CName = ListenerAction.GetName(action);
        let actionType: gameinputActionType = ListenerAction.GetType(action);

        if !this.m_chatInputOpen {
            if Equals(actionName, n"UIEnterChatMessage") && Equals(actionType, gameinputActionType.BUTTON_RELEASED) {
                // let targets = new inkWidgetsSet();
                // targets.Select(this.m_phoneIconWidget);
                // this.PlayLibraryAnimationOnTargets(n"onUse", targets);
                this.ShowChatInput(true);
                return true;
            } else {
                return false;
            }
        } else {
            return this.OnChatInputAction(action, consumer);
        }
    }
}