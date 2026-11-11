// THE TRADE PANEL'S MOUSE.
//
// WHY A POPUP, AND WHY THIS ONE. The HUD layer the chat lives on has no cursor at all - that is
// the wall every earlier attempt hit (166d5a42 pushed a modal context, 889d4831 asked for the
// cursor; 6e1c52a6 reverted both because chat stopped taking keys and no cursor ever appeared).
// The game's own answer for "a cursor over the running world, without a menu screen" is an
// in-game notification with useCursor: the car colour picker and the frame switcher are both
// built that way (popupManager.script). Codeware - which this mod already depends on - wraps it
// as CustomPopup, and that is what this is.
//
// DELIBERATELY NOT Codeware's InGamePopup. That subclass adds three things on show: radialMenu
// time dilation, background blur and a ModalPopup context push. The first and last are wrong here -
// the first slows one player's world in a shared one, and the last is exactly the push that broke
// chat typing. The plain CustomPopup gives the cursor and input capture and nothing else, so the
// city keeps moving behind the panel and nobody's game slows down because two people are haggling.
// The blur alone is borrowed back, behind the BLUR trial switch and off by default (ApplyBlur).
//
// INPUT, AND THE ONE RULE THAT MATTERS: a click is ACTED ON in the post-release handler, never
// inside a widget's own callback. Acting redraws the panel, and redrawing from inside the callback
// of a widget being destroyed is how a click takes a screen down. So the widget callback only
// notes WHICH region was clicked, and OnGlobalReleaseInput - which runs after the event has been
// through the widgets - acts on it. If no widget reported the click at all (the selector's
// experience: hand-built hit rectangles that never received one), the same handler converts the
// click's screen position to authored units and finds the region itself. Either way the click
// lands in one place.

module CyberpunkMP.Ink

import Codeware.UI.*
import CyberpunkMP.*
import CyberpunkMP.World.*

public class MpTradePopup extends CustomPopup {
    private let m_owner: wref<ChatController>;
    private let m_canvas: wref<inkCanvas>;
    private let m_hits: ref<MpTrHits>;

    // The region a widget reported for the click now being released; 0 when none did (region
    // ids start at 1).
    private let m_clicked: Int32;

    // How the canvas sits in the root, for converting a fallback click back to authored units.
    private let m_scale: Float = 1.0;
    private let m_originX: Float;
    private let m_originY: Float;

    // A rail's arrow being dragged: which list (MpTrRailMap.list), and how far the arrow sat from
    // the cursor when it was picked up, so it stays under the cursor instead of jumping to it.
    private let m_dragging: Bool;
    private let m_dragList: Int32;
    private let m_dragOffset: Float;

    // Whether THIS popup has the game's background blur switched on, so closing undoes only what
    // it did itself and never clears a blur some other screen owns.
    private let m_blurOn: Bool;

    // False when Codeware's popup manager never attached (it hooks PopupsManager.OnPlayerAttach).
    // Opening then does nothing at all, so the caller has to know to say so instead.
    public static func CanOpen() -> Bool {
        let manager = CustomPopupManager.GetInstance();
        return IsDefined(manager) && manager.IsInitialized();
    }

    public static func Create(owner: ref<ChatController>) -> ref<MpTradePopup> {
        let popup = new MpTradePopup();
        popup.m_owner = owner;
        popup.m_hits = new MpTrHits();
        return popup;
    }

    // Its own queue, so a game message waiting in modal_popup can never hold the panel back
    // (or be held back by it).
    public func GetQueueName() -> CName {
        return n"mp_trade";
    }

    public func IsBlocking() -> Bool {
        return true;
    }

    public func UseCursor() -> Bool {
        return true;
    }

    protected cb func OnCreate() {
        super.OnCreate();

        let canvas = new inkCanvas();
        canvas.SetName(n"mp_trade_panel");
        // Top-left anchor AND pivot, so scaling grows down-and-right from the corner instead of
        // throwing the composition off-screen (the selector bug, CharacterSelect.MpCsOpen).
        canvas.SetAnchor(inkEAnchor.TopLeft);
        canvas.SetAnchorPoint(new Vector2(0.0, 0.0));
        // A real size, covering the whole authored composition. A zero-size parent can keep its
        // children out of hit-testing - one candidate for why the selector's hand-built hit
        // rectangles never once received a click - and Codeware's own popup container sets one.
        canvas.SetSize(new Vector2(1920.0, 1080.0));
        canvas.Reparent(this.GetRootCompoundWidget());
        this.m_canvas = canvas;
    }

    protected cb func OnAttach() {
        super.OnAttach();
        // The wheel scrolls the list under it. The same global hook the game's gallery uses.
        this.RegisterToGlobalInputCallback(n"OnPostOnAxis", this, n"OnMpTrAxis");
        // The rails' drag: press, move, then the release CustomPopup already hooks. The game's own
        // colour wheel (glamdoc.script) drags exactly this way - OnPostOnPress, then OnPostOnRelative
        // for mouse_x/mouse_y read through GetScreenSpacePosition, then OnPostOnRelease.
        this.RegisterToGlobalInputCallback(n"OnPostOnPress", this, n"OnMpTrPress");
        this.RegisterToGlobalInputCallback(n"OnPostOnRelative", this, n"OnMpTrMove");
        this.ApplyBlur();
        this.Redraw();

        if IsDefined(this.m_owner) {
            this.m_owner.MpTrPopupShown(this);
        }
    }

    protected cb func OnDetach() {
        this.UnregisterFromGlobalInputCallback(n"OnPostOnAxis", this, n"OnMpTrAxis");
        this.UnregisterFromGlobalInputCallback(n"OnPostOnPress", this, n"OnMpTrPress");
        this.UnregisterFromGlobalInputCallback(n"OnPostOnRelative", this, n"OnMpTrMove");
        this.m_dragging = false;
        if this.m_blurOn {
            let gc = this.GetGameController();
            if IsDefined(gc) {
                PopupStateUtils.SetBackgroundBlur(gc, false);
            }
            this.m_blurOn = false;
        }
        super.OnDetach();

        if IsDefined(this.m_owner) {
            this.m_owner.MpTrPopupClosed(this);
        }
    }

    // The panel is already where the HUD chip was, so it appears and goes rather than fading
    // over half a second (CustomPopup's default). Still an animation, because CustomPopup's
    // close path - OnHideFinish, then OnHidden, which releases the notification - hangs off it.
    protected cb func OnShow() {
        let alpha = new inkAnimTransparency();
        alpha.SetStartTransparency(0.0);
        alpha.SetEndTransparency(1.0);
        alpha.SetType(inkanimInterpolationType.Linear);
        alpha.SetMode(inkanimInterpolationMode.EasyOut);
        alpha.SetDuration(0.12);
        let def = new inkAnimDef();
        def.AddInterpolator(alpha);
        this.m_transitionAnimProxy = this.GetRootWidget().PlayAnimation(def);
        this.m_transitionAnimProxy.RegisterToCallback(inkanimEventType.OnFinish, this, n"OnShowFinish");
    }

    protected cb func OnHide() {
        let alpha = new inkAnimTransparency();
        alpha.SetStartTransparency(1.0);
        alpha.SetEndTransparency(0.0);
        alpha.SetType(inkanimInterpolationType.Linear);
        alpha.SetMode(inkanimInterpolationMode.EasyOut);
        alpha.SetDuration(0.08);
        let def = new inkAnimDef();
        def.AddInterpolator(alpha);
        this.m_transitionAnimProxy = this.GetRootWidget().PlayAnimation(def);
        this.m_transitionAnimProxy.RegisterToCallback(inkanimEventType.OnFinish, this, n"OnHideFinish");
    }

    /**
     * Rebuild the panel from the owner's current view.
     *
     * Placed by the root's own size: a fraction of it for the corner, and MpTrScaleFor for the
     * scale, so it is the same size and place on any resolution. The rect is logged, because the
     * 3840-wide HUD the placement was derived from is inferred - this line is how a session says
     * whether the panel landed where the v2 overlay was.
     */
    public func Redraw() -> Void {
        if !IsDefined(this.m_canvas) || !IsDefined(this.m_owner) {
            return;
        }

        let view = this.m_owner.MpTrViewNow();
        let ui = this.m_owner.MpTrUiNow();
        if !IsDefined(view) || !IsDefined(ui) {
            return;
        }

        let size = this.GetRootWidget().GetSize();
        let rootW = size.X > 1.0 ? size.X : 3840.0;
        let rootH = size.Y > 1.0 ? size.Y : 2160.0;
        let placement = this.m_owner.MpTrPlacement();

        this.m_scale = MpTrScaleFor(rootW, placement.Z);
        this.m_originX = rootW * placement.X - MpTrPanelX() * this.m_scale;
        this.m_originY = rootH * placement.Y - MpTrPanelY() * this.m_scale;

        this.m_canvas.RemoveAllChildren();
        this.m_canvas.SetMargin(new inkMargin(this.m_originX, this.m_originY, 0.0, 0.0));
        this.m_canvas.SetScale(new Vector2(this.m_scale, this.m_scale));

        this.m_hits.Clear();
        MpTrBuild(this.m_canvas, view, ui, this.m_hits);
        MpTrDrawHits(this.m_canvas, this.m_hits, this);
    }

    /**
     * The BLUR trial switch (MpTrUi.blur): the game's own popup blur, the one flag its phone, its
     * weapon wheel and Codeware's InGamePopup all set (PopupStateUtils.SetBackgroundBlur, which
     * writes UI_System.CircularBlurEnabled). Only the blur is borrowed from InGamePopup - not its
     * slow-motion or modal push (file header). It softens the WORLD, not the patch behind the panel;
     * how strong it is, what shape, and whether chat stays sharp are what the trial is for.
     */
    public func ApplyBlur() -> Void {
        let want = false;
        if IsDefined(this.m_owner) {
            want = this.m_owner.MpTrUiNow().blur;
        }
        if Equals(want, this.m_blurOn) {
            return;
        }
        let gc = this.GetGameController();
        if !IsDefined(gc) {
            return;
        }
        PopupStateUtils.SetBackgroundBlur(gc, want);
        this.m_blurOn = want;
    }

    public func RootSize() -> Vector2 {
        return this.GetRootWidget().GetSize();
    }

    public func Scale() -> Float {
        return this.m_scale;
    }

    // ---------------------------------------------------------------- input

    protected cb func OnMpTrHitRelease(evt: ref<inkPointerEvent>) -> Bool {
        if evt.IsAction(n"click") {
            // Noted, not acted on - see the file header.
            this.m_clicked = MpTrHitId(evt.GetCurrentTarget());
        }
        return false;
    }

    protected cb func OnMpTrHitOver(evt: ref<inkPointerEvent>) -> Bool {
        let w = evt.GetCurrentTarget();
        if IsDefined(w) {
            w.SetOpacity(0.16);
        }
        return false;
    }

    protected cb func OnMpTrHitOut(evt: ref<inkPointerEvent>) -> Bool {
        let w = evt.GetCurrentTarget();
        if IsDefined(w) {
            w.SetOpacity(MpTrHitRest());
        }
        return false;
    }

    protected cb func OnGlobalReleaseInput(evt: ref<inkPointerEvent>) -> Bool {
        // Escape gives the mouse back. The trade itself carries on - only CANCEL walks away.
        if evt.IsAction(n"cancel") && !evt.IsHandled() {
            this.Close();
            evt.Handle();
            return true;
        }

        if evt.IsAction(n"click") && this.m_dragging {
            // The end of a drag is not a click on whatever the cursor came to rest over.
            this.m_dragging = false;
            this.m_clicked = 0;
            evt.Handle();
            return true;
        }

        if evt.IsAction(n"click") {
            let id = this.m_clicked;
            this.m_clicked = 0;
            let how = "widget";

            if id <= 0 {
                id = this.FallbackHit(evt.GetScreenSpacePosition());
                how = "position";
            }

            if id >= 0 && IsDefined(this.m_owner) {
                evt.Handle();
                this.m_owner.MpTrAct(id, how);
                return true;
            }
        }

        return false;
    }

    // Whether a screen position can be turned into authored units yet.
    private func CanMap() -> Bool {
        let display = ScreenHelper.GetScreenSize(this.GetGame());
        let size = this.GetRootWidget().GetSize();
        return display.X >= 1.0 && display.Y >= 1.0 && size.X >= 1.0 && this.m_scale > 0.0;
    }

    // Screen pixels to the root's units, then to authored - once, here, and nowhere else.
    private func ToPanel(screen: Vector2) -> Vector2 {
        let display = ScreenHelper.GetScreenSize(this.GetGame());
        let size = this.GetRootWidget().GetSize();
        let rx = screen.X * size.X / display.X;
        let ry = screen.Y * size.Y / display.Y;
        return new Vector2((rx - this.m_originX) / this.m_scale, (ry - this.m_originY) / this.m_scale);
    }

    private func FallbackHit(screen: Vector2) -> Int32 {
        if !this.CanMap() {
            return -1;
        }
        let at = this.ToPanel(screen);
        return this.m_hits.At(at.X, at.Y);
    }

    // A press on a rail: on the arrow it is picked up where it is held; on the bare ladder (or a
    // number) the arrow jumps there, and either way it can then be dragged. The launcher's rail.
    protected cb func OnMpTrPress(evt: ref<inkPointerEvent>) -> Bool {
        if !evt.IsAction(n"click") || !IsDefined(this.m_owner) || !this.CanMap() {
            return false;
        }
        let at = this.ToPanel(evt.GetScreenSpacePosition());
        let i = this.m_hits.RailAt(at.X, at.Y);
        if i < 0 {
            return false;
        }
        let rail = this.m_hits.rails[i];
        let arrowY = rail.ys[rail.top];
        let onArrow = at.X <= rail.x + 16.0 && AbsF(at.Y - arrowY) <= 9.0;
        this.m_dragOffset = onArrow ? arrowY - at.Y : 0.0;
        this.m_dragList = rail.list;
        this.m_dragging = true;
        this.m_clicked = 0;
        this.DragTo(at.Y);
        evt.Handle();
        return true;
    }

    protected cb func OnMpTrMove(evt: ref<inkPointerEvent>) -> Bool {
        if !this.m_dragging || !(evt.IsAction(n"mouse_y") || evt.IsAction(n"mouse_x")) || !this.CanMap() {
            return false;
        }
        let at = this.ToPanel(evt.GetScreenSpacePosition());
        this.DragTo(at.Y);
        return false;
    }

    // Only a change of row redraws: the move events come far faster than rows go by.
    private func DragTo(y: Float) -> Void {
        let rail = this.m_hits.Rail(this.m_dragList);
        if !IsDefined(rail) || !IsDefined(this.m_owner) {
            this.m_dragging = false;
            return;
        }
        let top = rail.Nearest(y + this.m_dragOffset);
        if top != rail.top {
            this.m_owner.MpTrSetTop(this.m_dragList, top);
        }
    }

    // The wheel: over a rail, two rungs a notch (the launcher's rail); over a list, a row a notch;
    // anywhere else on the panel, your list. Only the direction of a notch counts, so a mouse that
    // reports big notches moves no further than one that reports small ones (the launcher's
    // 600-a-notch lesson, the-settings-rail-s-wheel-does-not-work-on-zeldfep-s-v0-3-12).
    protected cb func OnMpTrAxis(evt: ref<inkPointerEvent>) -> Bool {
        if evt.IsHandled() || !evt.IsAction(n"mouse_wheel") || !IsDefined(this.m_owner) || this.m_dragging {
            return false;
        }
        let axis = evt.GetAxisData();
        if axis == 0.0 {
            return false;
        }
        let dir = axis > 0.0 ? -1 : 1;
        evt.Handle();

        if this.CanMap() {
            let at = this.ToPanel(evt.GetScreenSpacePosition());
            let i = this.m_hits.RailAt(at.X, at.Y);
            if i >= 0 {
                let rail = this.m_hits.rails[i];
                this.m_owner.MpTrSetTop(rail.list, rail.Wheel(dir));
                return false;
            }
            i = this.m_hits.ListAt(at.X, at.Y);
            if i >= 0 {
                let list = this.m_hits.rails[i];
                this.m_owner.MpTrSetTop(list.list, list.top + dir);
                return false;
            }
        }
        this.m_owner.MpTrScroll(dir);
        return false;
    }
}
