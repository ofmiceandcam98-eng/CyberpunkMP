module CyberpunkMP.Ink

import CyberpunkMP.*

public class ChatMessageController extends ListItemController {
    public let m_authorWidget: inkTextRef;
    public let m_messageWidget: inkTextRef;

    public let m_data: ref<ChatMessageData>;
    public let m_authorLabel: wref<inkText>;
    public let m_messageLabel: wref<inkText>;

    protected cb func OnInitialize() -> Bool {
        FTLog(s"[ChatMessageController] OnInitialize");
        // super.OnInitialize();
        this.m_authorLabel = inkTextRef.Get(this.m_authorWidget) as inkText;
        this.m_messageLabel = inkTextRef.Get(this.m_messageWidget) as inkText;
        // this.RegisterToCallback(n"OnAddedToList", this, n"OnAddedToList");
    }

    protected cb func OnUninitialize() -> Bool {
        // this.UnregisterFromCallback(n"OnAddedToList", this, n"OnAddedToList");
    }

    protected cb func OnDataChanged(value: ref<IScriptable>) -> Bool {
        FTLog(s"[ChatMessageController] OnDataChanged");
        // super.OnDataChanged(value);
        this.m_data = value as ChatMessageData;
        this.Apply();
    }

    public final func Refresh(value: ref<IScriptable>) -> Void {
        this.m_data = value as ChatMessageData;
        this.Apply();
    }

    // One place that fills the row in, instead of the same block copied into
    // OnDataChanged and Refresh. They had already drifted apart once.
    private final func Apply() -> Void {
        // The name goes INTO the message line, and the separate name widget is hidden.
        //
        // The two are separate widgets stacked vertically by the .inkwidget asset, so
        // every message cost two lines and the chat box grew twice as fast as it needed
        // to. Putting them side by side means editing that asset in WolvenKit; composing
        // the text achieves the same reading - "name: what they said" on one line - with
        // no asset work.
        //
        // The name is repeated on every line rather than grouped. In a log you scroll
        // back through, a run of unattributed lines is ambiguous the moment it scrolls
        // past the name that headed it.
        this.m_authorLabel.SetVisible(false);
        this.m_messageLabel.SetText(this.m_data.m_author + ": " + this.m_data.m_message);
        this.ApplyChannelColor();
    }

    // Tints the line by channel.
    //
    // List items are RECYCLED as you scroll - the same controller is handed new data over
    // and over. So this must set the colour back to normal for untinted channels, not
    // just apply one for tinted ones. Without the else branch a single yell eventually
    // turns half the chat log red as its widget gets reused.
    private final func ApplyChannelColor() -> Void {
        if !IsDefined(this.m_messageLabel) {
            return;
        }

        if ChatChannelIsTinted(this.m_data.m_channel) {
            this.m_messageLabel.SetTintColor(GetChatChannelColor(this.m_data.m_channel));
        } else {
            this.m_messageLabel.SetTintColor(new HDRColor(1.0, 1.0, 1.0, 1.0));
        }
    }

    protected cb func OnAddedToList(target: wref<ListItemController>) -> Bool {
        // FTLog(s"[ChatMessageController] OnAddedToList");
        let m_animListText: ref<inkAnimDef>;
        let m_animTextInterp: ref<inkAnimTextOffset>;
        let stageOneTime: Float;
        let stageTwoTime: Float;
        if this.GetIndex() == 0 {
            m_animListText = new inkAnimDef();
            m_animTextInterp = new inkAnimTextOffset();
            m_animTextInterp.SetDuration(0.08);
            m_animTextInterp.SetStartProgress(0.25);
            m_animTextInterp.SetEndProgress(0.00);
            m_animListText.AddInterpolator(m_animTextInterp);
            this.GetRootWidget().PlayAnimation(m_animListText);
        } else {
            stageOneTime = 0.10;
            stageTwoTime = 0.10 + MinF(5.00, Cast<Float>(this.GetIndex())) * 0.15;
            m_animListText = new inkAnimDef();
            m_animTextInterp = new inkAnimTextOffset();
            m_animTextInterp.SetStartDelay(0.00);
            m_animTextInterp.SetDuration(stageOneTime);
            m_animTextInterp.SetStartProgress(0.75);
            m_animTextInterp.SetEndProgress(0.01);
            m_animTextInterp.SetType(inkanimInterpolationType.Quadratic);
            m_animTextInterp.SetMode(inkanimInterpolationMode.EasyOut);
            m_animListText.AddInterpolator(m_animTextInterp);
            m_animTextInterp = new inkAnimTextOffset();
            m_animTextInterp.SetStartDelay(stageOneTime);
            m_animTextInterp.SetDuration(stageTwoTime);
            m_animTextInterp.SetStartProgress(0.01);
            m_animTextInterp.SetEndProgress(0.00);
            m_animTextInterp.SetType(inkanimInterpolationType.Quadratic);
            m_animTextInterp.SetMode(inkanimInterpolationMode.EasyOut);
            m_animListText.AddInterpolator(m_animTextInterp);
            this.GetRootWidget().PlayAnimation(m_animListText);
        };
    }
}
