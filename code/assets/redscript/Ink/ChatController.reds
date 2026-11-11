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
    // The name last read back for confirmation. Empty until the first Enter; cleared
    // whenever the prompt opens or closes, so a stale one can never confirm a new name.
    private let m_pendingName: String;
    // ---- the trade panel (TradeScreen.reds draws it, TradePopup.reds gives it the mouse) ----
    //
    // State lives HERE because this is the controller that exists for the whole session and
    // owns chat, and only a real field on the class can hold it (annotations cannot attach to
    // our own classes). The popup comes and goes; the trade does not go with it.
    //
    // The HUD chip's canvas, on this controller's root - what stays beside chat while the
    // mouse is given back, or before an invitation is opened.
    private let m_trRoot: wref<inkCanvas>;
    // Where the panel sits, as fractions of the SCREEN (x, y) and a zoom - seeded from the MpTr*
    // defaults, nudged with /tr*, kept with /trsave.
    private let m_trFracX: Float;
    private let m_trFracY: Float;
    private let m_trZoom: Float;
    private let m_trTuned: Bool;
    // The trade as last told by the server (or the /tradeui preview), and what only this side
    // knows about it - scroll, an amount on its way, a line of feedback.
    private let m_trView: ref<MpTrView>;
    private let m_trUi: ref<MpTrUi>;
    // The popup while it is up. Null means the mouse is the game's.
    private let m_trPopup: ref<MpTradePopup>;
    // Whether the Enter listener ("bring the panel back") is registered.
    private let m_trKeys: Bool;
    // Bumped whenever a new trade view arrives, so a delayed "take the chip down" for an old
    // ending cannot take down a new trade's chip.
    private let m_trEpoch: Int32;
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
        // Reported through ScriptLog, NOT FTLog.
        //
        // These diagnostics were written precisely so nobody would have to guess which
        // widget is wrong - and then sent to FTLog, which reaches no file this project
        // collects. So they have been running invisibly ever since, and the guessing
        // continued anyway. Cam spent an evening locked out of his own character by an
        // input box that had focus but never appeared, with this answer being printed into
        // the void the whole time.
        //
        // Reading them: a null means the widget path is wrong; a zero size means layout; and
        // correct-looking values everywhere mean the problem is a PARENT or the render
        // order rather than anything in this file. That last case is the live suspect -
        // the box becomes visible the moment the player enters a vehicle, which changes
        // HUD context without this code running at all.
        let network = GameInstance.GetNetworkWorldSystem();

        let root = this.GetRootWidget();
        if IsDefined(root) {
            if IsDefined(network) {
                // Position and affine state, not just visible/opacity.
                //
                // The earlier round of this printed visible=true opacity=1 on the root and
                // size=0x0 on the wrappers, and I read the zero size as the fault. That was
                // probably wrong: an ink canvas that sizes to its children reports 0x0 as a
                // matter of course, and a zero-sized canvas does not clip what is inside it.
                //
                // What actually needs explaining is a widget that says it is visible and is
                // not drawn. That is position, scale, or the layer it sits on - none of
                // which the old line showed. Cam reports the whole custom HUD appears the
                // moment he enters a vehicle, so the useful measurement is this same line
                // before and after that, and whichever number changes is the answer.
                let pos = root.GetTranslation();
                let scale = root.GetScale();
                let sz = root.GetSize();
                network.ScriptLog(s"[ChatVisible] root visible=\(root.IsVisible()) opacity=\(root.GetOpacity())" +
                    s" pos=(\(pos.X), \(pos.Y)) scale=(\(scale.X), \(scale.Y)) size=(\(sz.X)x\(sz.Y))" +
                    s" affectsLayout=\(root.GetAffectsLayoutWhenHidden())");
            }
            // The measurement this bug has been missing.
            //
            // Everything above reports the chat root as correct, and decoding the assets
            // confirms that IS how it was authored - so a correct, visible widget is not
            // being drawn. The comment above already named the remaining suspect: a PARENT
            // or the render order. This is the first probe that can actually look there,
            // because walking up needs a native helper.
            //
            // Read it as a chain outwards from chat. The first ancestor with visible=0,
            // opacity=0, a zero scale or layer=NONE is the answer.
            if IsDefined(network) {
                network.LogWidgetAncestry(root, "chat root");
            }

            root.SetVisible(true);
            root.SetOpacity(1.0);
        } else {
            if IsDefined(network) {
                network.ScriptLog("[ChatVisible] NO ROOT WIDGET - the controller is not attached");
            }
        }

        let names = ["wrapper", "wrapper/chat", "wrapper/chat/bg", "wrapper/input_box"];
        for name in names {
            let widget = this.GetWidget(StringToName(name));
            if IsDefined(widget) {
                let size = widget.GetSize();
                if IsDefined(network) {
                    network.ScriptLog(s"[ChatVisible] '\(name)' visible=\(widget.IsVisible()) opacity=\(widget.GetOpacity()) size=\(size.X)x\(size.Y)");
                }
                widget.SetVisible(true);
            } else {
                if IsDefined(network) {
                    network.ScriptLog(s"[ChatVisible] '\(name)' DOES NOT RESOLVE - wrong path");
                }
            }
        }
    }

    protected cb func OnUninitialize() -> Bool {
        FTLog(s"[ChatController] OnUninitialize");
        this.m_player.UnregisterInputListener(this, n"UIEnterChatMessage");
        // A HUD being rebuilt must not leave a cursor popup behind with nobody to answer it.
        // The trade itself is the server's; the next NotifyTrade puts the chip back.
        this.MpTrKeys(false);
        if IsDefined(this.m_trPopup) {
            this.m_trPopup.Close();
        }
    }

    private func UpdateInputHints() -> Void {
        let evt = new UpdateInputHintMultipleEvent();
        evt.targetHintContainer = n"GameplayInputHelper";
        // The game draws this one, in its own style and with the CURRENT binding, beside the
        // other prompts - the ink-ui rule: a key prompt is an input hint, not a widget.
        evt.AddInputHint(CreateInputHint(n"Trade", n"TradeAccept", false),
                         !this.m_chatInputOpen && this.MpTrChipShowing());
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

        // Re-asserted on every message, not just at init.
        //
        // Cam's report after the first fix was that chat now appears but LATE - typically
        // once he got into a vehicle. That fits: OnInitialize runs while the HUD is still
        // being assembled, and something later in that assembly puts the widget back the
        // way the asset authored it. Anything that rebuilds the HUD would do the same.
        //
        // Setting it again here means the first message to arrive brings chat back, so a
        // late reset costs one message rather than the rest of the session. It is cheap
        // and it does not depend on knowing which part of the HUD is responsible.
        this.ForceVisible();
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
        // ScriptLog, NOT FTLog, for the same reason as ForceVisible above: FTLog reaches no
        // file this project collects. These three naming lines (prompt opened / read back /
        // committed) are the evidence chain for the name-mangling diagnosis - comparing them
        // with what the server stored is how a typo is told apart from dropped keystrokes,
        // and on FTLog they were printing into the void.
        let network = GameInstance.GetNetworkWorldSystem();
        if IsDefined(network) {
            network.ScriptLog(s"[ChatController] character name requested (currently '\(evt.m_current)')");
        }

        this.m_namePromptOpen = true;
        this.m_pendingName = "";

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
        notice.message = "What is your character called? Type a name and press Enter - it will be read back to you before it is kept.";
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

    // True once the text in the box has been read back to the player and Enter was pressed
    // again on the SAME text. The first Enter - or any Enter after an edit - only shows it.
    //
    // Empty is passed straight through: SendName already answers it with a notice, and
    // asking somebody to confirm nothing would be a second prompt about the same mistake.
    private func ConfirmName() -> Bool {
        let wanted = this.m_input.GetText();

        if Equals(wanted, "") || Equals(wanted, this.m_pendingName) {
            return true;
        }

        this.m_pendingName = wanted;

        // Spelled out as well as shown whole, because the two faults actually reported are
        // a missing letter and a wrong capital - both easy to read straight past in a word,
        // and hard to miss one character at a time.
        let spelled = "";
        let i = 0;
        while i < StrLen(wanted) {
            if i > 0 {
                spelled += " ";
            }
            spelled += StrMid(wanted, i, 1);
            i += 1;
        }

        let notice = new ChatMessageUIEvent();
        notice.author = "SERVER";
        notice.message = s"You typed \"\(wanted)\" (\(spelled)). Names are permanent - press Enter again to keep it, or fix it first.";
        this.QueueEvent(notice);

        // Collected log (see OnCharacterNameRequest) - this is the middle of the evidence chain.
        let network = GameInstance.GetNetworkWorldSystem();
        if IsDefined(network) {
            network.ScriptLog(s"[ChatController] name read back for confirmation '\(wanted)'");
        }
        return false;
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
        // Collected log (see OnCharacterNameRequest) - the end of the evidence chain: this
        // exact string went to the server, so any difference from what the server stored
        // happened on the wire or in ChatSystem, not in the input box.
        let network = GameInstance.GetNetworkWorldSystem();
        if IsDefined(network) {
            network.ScriptLog(s"[ChatController] naming this character '\(wanted)'");
        }
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
            this.m_pendingName = "";
            this.ShowNameLabel(false);
        }
        this.m_chatInputOpen = show;
        this.UpdateInputHints();
    }

    // ------------------------------------------------------------------ the trade panel
    //
    // Three surfaces, one trade:
    //   - the PANEL, in a cursor popup (TradePopup.reds) - the whole deal, clickable;
    //   - the CHIP, on this HUD root - a line beside chat while the mouse is the game's;
    //   - CHAT, which keeps working throughout, because trades are haggled in text.
    // Everything shown comes from NotifyTrade (OnMpTradeChanged); everything done is a /trade
    // command (MpTrAct). Nothing here can make a deal the server does not hold.

    public final func MpTrViewNow() -> ref<MpTrView> {
        return this.m_trView;
    }

    public final func MpTrUiNow() -> ref<MpTrUi> {
        if !IsDefined(this.m_trUi) {
            this.m_trUi = new MpTrUi();
            this.m_trUi.pending = -1;
        }
        return this.m_trUi;
    }

    // (screen fraction x, screen fraction y, zoom, unused) - the shape /trsave keeps.
    public final func MpTrPlacement() -> Vector4 {
        if !this.m_trTuned {
            this.m_trFracX = MpTrScreenFracX();
            this.m_trFracY = MpTrScreenFracY();
            this.m_trZoom = MpTrZoom();
            // A /trsave'd placement overrides the default; zoom 0 means nothing was saved.
            let saved = GameInstance.GetNetworkWorldSystem().GetChatSystem().LoadTradePlacement();
            if saved.Z > 0.0 {
                this.m_trFracX = saved.X;
                this.m_trFracY = saved.Y;
                this.m_trZoom = saved.Z;
            }
            this.m_trTuned = true;
        }
        return new Vector4(this.m_trFracX, this.m_trFracY, this.m_trZoom, 0.0);
    }

    private final func MpTrLog(line: String) -> Void {
        // ScriptLog, not FTLog: FTLog reaches no file this project collects (the naming saga).
        let network = GameInstance.GetNetworkWorldSystem();
        if IsDefined(network) {
            network.ScriptLog(s"[TradeScreen] \(line)");
        }
    }

    /**
     * The server says the trade changed. Raised by NetworkWorldSystem.OnTradeStateChanged.
     *
     * The panel opens BY ITSELF only when a trade becomes Open: both people chose it at that
     * point (one asked, one accepted), so the mouse coming up is what they are waiting for. An
     * invitation does NOT take the mouse - somebody mid-fight or mid-drive gets a chip and a
     * key, not a cursor they did not ask for.
     */
    protected cb func OnMpTradeChanged(evt: ref<MpTradeChanged>) -> Bool {
        let network = GameInstance.GetNetworkWorldSystem();
        if !IsDefined(network) {
            return false;
        }

        let previous = this.m_trView;
        let view = MpTrViewFromNetwork(network);
        let ui = this.MpTrUiNow();

        // Every money command gets one answer (the server pushes on a refusal too), so when no
        // amount is left in flight the server's figure is the true one and the guess is done.
        if ui.inFlight > 0 {
            ui.inFlight -= 1;
        }
        if ui.inFlight == 0 {
            ui.pending = -1;
        }

        let wasOpen = IsDefined(previous) && !previous.mock && previous.live && previous.state == 1u;
        let newTrade = !IsDefined(previous) || previous.mock || NotEquals(previous.partner, view.partner) || !previous.live;

        if newTrade {
            ui.listTop = 0;
            ui.theirTop = 0;
            ui.notice = "";
        }

        this.m_trView = view;
        this.m_trEpoch += 1;

        if view.live {
            this.MpTrKeys(true);

            if view.state == 1u && !wasOpen && !this.m_chatInputOpen {
                this.MpTrShowPanel();
                return true;
            }
        } else {
            // An ending stays readable - on the panel if it is up, as the chip if not - and goes
            // by itself a few seconds later. Either one can also be closed straight away.
            ui.pending = -1;
            ui.inFlight = 0;
            let done = new MpTrHideChipCallback();
            done.owner = this;
            done.epoch = this.m_trEpoch;
            GameInstance.GetDelaySystem(this.m_player.GetGame()).DelayCallback(done, 4.0, false);
        }

        this.MpTrRefresh();
        return true;
    }

    // Called a few seconds after an ending, unless something newer has happened since.
    public final func MpTrEndingExpired(epoch: Int32) -> Void {
        if epoch != this.m_trEpoch || !IsDefined(this.m_trView) || this.m_trView.live {
            return;
        }
        this.MpTrFinish();
    }

    // The panel, with the mouse. Reused if it is already up.
    public final func MpTrShowPanel() -> Void {
        if !IsDefined(this.m_trView) {
            return;
        }

        if IsDefined(this.m_trPopup) {
            this.m_trPopup.Redraw();
            return;
        }

        /*
         * If Codeware's popup manager never attached (it hooks PopupsManager.OnPlayerAttach),
         * opening would silently do nothing - the worst failure here, a trade nobody can see.
         * So say so, keep the chip up, and point at the typed commands, which always work.
         */
        if !MpTradePopup.CanOpen() {
            this.MpTrLog("panel cannot open - Codeware's popup manager is not initialised");
            let notice = new ChatMessageUIEvent();
            notice.author = "SERVER";
            notice.message = "The trade panel could not open. Type /trade view to see the deal, /trade confirm or /trade cancel.";
            this.QueueEvent(notice);
            this.MpTrRefresh();
            return;
        }

        let popup = MpTradePopup.Create(this);
        this.m_trPopup = popup;
        popup.Open(this);
        this.MpTrRefresh();
    }

    // Give the mouse back. The trade carries on; only CANCEL walks away.
    public final func MpTrHidePanel() -> Void {
        if IsDefined(this.m_trPopup) {
            this.m_trPopup.Close();
        }
    }

    public final func MpTrPopupShown(popup: ref<MpTradePopup>) -> Void {
        let size = popup.RootSize();
        let p = this.MpTrPlacement();
        // The measurement the placement rests on - see MpTrScreenFracX.
        this.MpTrLog(s"panel open - root \(size.X)x\(size.Y), panel at \(p.X),\(p.Y) of the screen, scale \(popup.Scale()), zoom \(p.Z)");
    }

    public final func MpTrPopupClosed(popup: ref<MpTradePopup>) -> Void {
        if Equals(this.m_trPopup, popup) {
            this.m_trPopup = null;
            this.MpTrLog("panel closed - the mouse is the game's again");
        }
        this.MpTrRefresh();
    }

    // Everything back to no trade at all.
    public final func MpTrFinish() -> Void {
        this.m_trView = null;
        this.MpTrKeys(false);
        this.MpTrHidePanel();
        this.MpTrRefresh();
    }

    // Redraw whatever is showing: the panel if it is up, otherwise the chip.
    public final func MpTrRefresh() -> Void {
        if IsDefined(this.m_trPopup) {
            this.m_trPopup.Redraw();
        }
        this.MpTrDrawChip();
        this.UpdateInputHints();
    }

    public final func MpTrChipShowing() -> Bool {
        return IsDefined(this.m_trView) && !IsDefined(this.m_trPopup);
    }

    private final func MpTrDrawChip() -> Void {
        let root = this.GetRootCompoundWidget();
        if !IsDefined(root) {
            return;
        }

        if !IsDefined(this.m_trRoot) {
            let canvas = new inkCanvas();
            canvas.SetName(n"mp_trade_chip");
            canvas.SetAnchor(inkEAnchor.TopLeft);
            canvas.SetAnchorPoint(new Vector2(0.0, 0.0));
            // NOT interactive: this is the HUD, and an interactive canvas here swallows input.
            canvas.SetInteractive(false);
            canvas.Reparent(root);
            this.m_trRoot = canvas;
        }

        this.m_trRoot.RemoveAllChildren();

        if !this.MpTrChipShowing() {
            this.m_trRoot.SetVisible(false);
            return;
        }

        /*
         * Placed the way the v2 overlay was - 1.04 and 0.12 of the chat root, scaled by
         * rootWidth/1920 x zoom - so the chip lands where the panel's top edge is.
         */
        let rootSize = root.GetSize();
        let zoom = this.MpTrPlacement().Z;
        let scale = 1.0;
        if rootSize.X > 1.0 {
            scale = (rootSize.X / 1920.0) * zoom;
            this.m_trRoot.SetMargin(new inkMargin(rootSize.X * 1.04 - MpTrPanelX() * scale,
                                                  rootSize.Y * 0.12 - MpTrPanelY() * scale, 0.0, 0.0));
        }
        this.m_trRoot.SetScale(new Vector2(scale, scale));

        let v = this.m_trView;
        // The tab always says TRADE; the line says the state, and the gold ENTER plate shows with
        // its verb whenever Enter does something here.
        let line = s"with \(v.partner)";
        let hint = "open";
        let session = v.mock ? "PREVIEW" : MpTrSession(v.session);

        if !v.live {
            line = StrLen(v.reason) > 0 ? v.reason : "The trade is over.";
            hint = "";
        } else {
            if v.state == 0u {
                if v.incoming {
                    line = s"\(v.partner) wants to trade";
                    hint = "look";
                } else {
                    line = s"Waiting for \(v.partner) to accept";
                }
            } else {
                if v.theirConfirmed && !v.myConfirmed {
                    line = s"\(v.partner) has locked in";
                }
            }
        }

        MpTrHudChip(this.m_trRoot, "TRADE", line, hint, session);
        this.m_trRoot.SetVisible(true);
    }

    // The one key: Enter brings the panel back. Registered only while there is a trade.
    private final func MpTrKeys(on: Bool) -> Void {
        if on && !this.m_trKeys {
            this.m_player.RegisterInputListener(this, n"TradeAccept");
            this.m_trKeys = true;
        }
        if !on && this.m_trKeys {
            this.m_player.UnregisterInputListener(this, n"TradeAccept");
            this.m_trKeys = false;
        }
    }

    private final func MpTrSend(command: String) -> Void {
        this.MpTrLog(s"sent \(command)");
        GameInstance.GetNetworkWorldSystem().GetChatSystem().Send(command);
    }

    /**
     * A click on the panel, by region id (TradeScreen.reds). Each one is a /trade command, or a
     * purely local change (scroll, the preview). Nothing here moves anything: the server decides
     * and the next NotifyTrade shows what it decided.
     */
    public final func MpTrAct(id: Int32, how: String) -> Void {
        let v = this.m_trView;
        if !IsDefined(v) {
            return;
        }
        let ui = this.MpTrUiNow();
        this.MpTrLog(s"click \(id) (\(how))");

        if id == MpTrIdHide() {
            if v.live {
                this.MpTrHidePanel();
            } else {
                this.MpTrFinish();
            }
            return;
        }

        // The BLUR trial switch: purely local, on every screen of the panel, ended or not.
        if id == MpTrIdBlur() {
            ui.blur = !ui.blur;
            if IsDefined(this.m_trPopup) {
                this.m_trPopup.ApplyBlur();
            }
            this.MpTrLog(s"blur \(ui.blur)");
            this.MpTrRefresh();
            return;
        }

        if !v.live {
            return;
        }

        if id == MpTrIdCancel() {
            if v.mock {
                this.MpTrFinish();
            } else {
                this.MpTrSend("/trade cancel");
            }
            return;
        }

        if id == MpTrIdConfirm() {
            if v.state == 0u {
                if v.incoming && !v.mock {
                    this.MpTrSend("/trade accept");
                }
                return;
            }
            if v.state == 1u && !v.myConfirmed && ui.pending < 0 {
                if v.mock {
                    v.myConfirmed = true;
                    this.MpTrRefresh();
                } else {
                    // The version on screen goes with it; the server refuses a stale one.
                    this.MpTrSend(s"/trade confirm \(v.version)");
                }
            }
            return;
        }

        if v.state != 1u {
            return;
        }

        if id >= MpTrIdMoney() && id < MpTrIdMoney() + 6 {
            let base = ui.pending >= 0 ? ui.pending : v.myMoney;
            let next = base + MpTrMoneyStep(id);
            if next > v.available { next = v.available; }
            if next < 0 { next = 0; }
            if next == base {
                return;
            }
            if v.mock {
                v.myMoney = next;
                v.myConfirmed = false;
            } else {
                // Shown straight away, sent straight away; the server's echo replaces it. Counted,
                // so a fast run of clicks keeps showing the LAST amount rather than each echo.
                ui.pending = next;
                ui.inFlight += 1;
                this.MpTrSend(s"/trade money \(next)");
            }
            this.MpTrRefresh();
            return;
        }

        // Rows and their quantity buttons.
        let index = -1;
        let change = 0;   // 0 toggle, -1 one fewer, +1 one more
        if id >= MpTrIdMore() {
            index = id - MpTrIdMore();
            change = 1;
        } else {
            if id >= MpTrIdLess() {
                index = id - MpTrIdLess();
                change = -1;
            } else {
                if id >= MpTrIdRow() {
                    index = id - MpTrIdRow();
                }
            }
        }

        if index < 0 || index >= ArraySize(v.holdings) {
            return;
        }

        let item = v.holdings[index];
        let want = 0;
        if change == 0 {
            // A click puts the WHOLE stack in, or takes it back out: one click for a gun, and
            // the stepper for "twenty of the ammo".
            want = item.offered > 0 ? 0 : item.qty;
        } else {
            want = item.offered + change;
            if want > item.qty { want = item.qty; }
            if want < 0 { want = 0; }
        }

        if want == item.offered {
            return;
        }

        if v.mock {
            item.offered = want;
            v.myConfirmed = false;
            this.MpTrRefresh();
            return;
        }

        // 0 takes it back out (the server's /trade item <id> 0).
        this.MpTrSend(s"/trade item \(item.id) \(want)");
    }

    // The wheel, over the panel but off both lists: your list scrolls a row at a time.
    public final func MpTrScroll(rows: Int32) -> Void {
        let ui = this.MpTrUiNow();
        ui.listTop += rows;
        this.MpTrRefresh();
    }

    // A rail (drag, press, wheel) or the wheel over a list: list 0 is yours, 1 theirs. The render
    // clamps it, so a rail can ask for one past the end.
    public final func MpTrSetTop(list: Int32, top: Int32) -> Void {
        let ui = this.MpTrUiNow();
        if list == 1 {
            if ui.theirTop == top { return; }
            ui.theirTop = top;
        } else {
            if ui.listTop == top { return; }
            ui.listTop = top;
        }
        this.MpTrRefresh();
    }

    // /tradeui: the panel with made-up contents, for checking placement and the mouse alone.
    private final func MpTrPreview() -> Void {
        if IsDefined(this.m_trView) && !this.m_trView.mock && this.m_trView.live {
            let notice = new ChatMessageUIEvent();
            notice.author = "SERVER";
            notice.message = "You are in a real trade - the preview would hide it. Finish or cancel it first.";
            this.QueueEvent(notice);
            return;
        }
        this.m_trView = MpTrMockView();
        let ui = this.MpTrUiNow();
        ui.listTop = 0;
        ui.theirTop = 0;
        ui.pending = -1;
        ui.inFlight = 0;
        ui.notice = "Preview - nothing here is sent.  ESC hides the panel, ENTER brings it back.";
        this.MpTrKeys(true);
        this.MpTrShowPanel();
    }

    // A placement nudge: the panel moves while it is up, so the change can be judged by eye.
    private final func MpTrNudge(dx: Float, dy: Float, dzoom: Float) -> Void {
        this.MpTrPlacement();
        this.m_trFracX += dx;
        this.m_trFracY += dy;
        this.m_trZoom += dzoom;
        if this.m_trZoom < 0.5 { this.m_trZoom = 0.5; }
        this.MpTrLog(s"placement \(this.m_trFracX),\(this.m_trFracY) zoom \(this.m_trZoom)");
        if IsDefined(this.m_trView) {
            this.MpTrShowPanel();
        }
    }

    private final func SendChat() -> Void {
        let textEntered: String = this.m_input.GetText();

        // Client-side trade commands, handled here and NOT sent on. The real trade needs none of
        // them - it is /trade <player> to the server and the panel from there - these are the
        // preview and the placement tools.
        //
        // /tradeui opens the panel with made-up contents (MpTrMockView): the way to check the
        // mouse and the placement alone. /tradeoff takes the preview down.
        if Equals(textEntered, "/tradeui") {
            this.m_input.SetText("");
            this.MpTrPreview();
            return;
        }
        if Equals(textEntered, "/tradeoff") {
            this.m_input.SetText("");
            if IsDefined(this.m_trView) && this.m_trView.mock {
                this.MpTrFinish();
            } else {
                this.MpTrHidePanel();
            }
            return;
        }
        // Placement nudges, as fractions of the screen. The panel moves while it is up, and the
        // values are logged ("[TradeScreen] placement ..."); /trsave keeps them.
        if Equals(textEntered, "/trright") { this.m_input.SetText(""); this.MpTrNudge(0.02, 0.0, 0.0); return; }
        if Equals(textEntered, "/trleft")  { this.m_input.SetText(""); this.MpTrNudge(-0.02, 0.0, 0.0); return; }
        if Equals(textEntered, "/trup")    { this.m_input.SetText(""); this.MpTrNudge(0.0, -0.02, 0.0); return; }
        if Equals(textEntered, "/trdown")  { this.m_input.SetText(""); this.MpTrNudge(0.0, 0.02, 0.0); return; }
        if Equals(textEntered, "/trbig")   { this.m_input.SetText(""); this.MpTrNudge(0.0, 0.0, 0.1); return; }
        if Equals(textEntered, "/trsmall") { this.m_input.SetText(""); this.MpTrNudge(0.0, 0.0, -0.1); return; }
        if Equals(textEntered, "/trreset") { this.m_input.SetText(""); this.m_trTuned = false; this.MpTrNudge(0.0, 0.0, 0.0); return; }
        // /trsave keeps the placement in the mod's own folder, so it survives a relaunch and goes
        // with an uninstall (the footprint rule).
        if Equals(textEntered, "/trsave") {
            this.m_input.SetText("");
            let p = this.MpTrPlacement();
            GameInstance.GetNetworkWorldSystem().GetChatSystem().SaveTradePlacement(p.X, p.Y, p.Z, 0.0);
            this.MpTrLog(s"placement saved \(p.X),\(p.Y) zoom \(p.Z)");
            return;
        }
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
                    // A name is permanent, so the first Enter only reads it back. Two
                    // reports (2026-09-10) typed Noremac and got Normac and NOremac - a
                    // dropped letter and a held Shift - and nothing between the keyboard
                    // and the server showed the player what they had actually typed.
                    // Keeping the box open until the SAME text is confirmed catches a
                    // slip whichever of the two it was, before it becomes an admin ticket.
                    if !this.ConfirmName() {
                        return true;
                    }
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

        // Enter brings the trade panel (and the mouse) back while a trade is up. Our own UIBase
        // action, the push-to-talk mechanism - proven live on 2.5 (2026-11-09, eight presses
        // logged). Registered only while there is a trade, and never while chat has focus, so it
        // cannot eat typing. Everything else on the panel is the mouse now.
        if this.m_trKeys && !this.m_chatInputOpen && Equals(actionName, n"TradeAccept")
           && Equals(actionType, gameinputActionType.BUTTON_PRESSED) {
            if this.MpTrChipShowing() {
                if this.m_trView.live {
                    this.MpTrShowPanel();
                } else {
                    this.MpTrFinish();
                }
            }
            return true;
        }

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

// Takes a trade's ending off the screen a few seconds after it arrived - unless the epoch moved,
// which means a newer trade (or a newer ending) owns the panel now.
public class MpTrHideChipCallback extends DelayCallback {
    public let owner: wref<ChatController>;
    public let epoch: Int32;

    public func Call() -> Void {
        if IsDefined(this.owner) {
            this.owner.MpTrEndingExpired(this.epoch);
        }
    }
}
