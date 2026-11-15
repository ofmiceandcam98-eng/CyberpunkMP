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

    // Widget clicks checked against the conversion this panel (MapCheck) - the first few are
    // logged, and every miss is.
    private let m_checks: Int32;

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
        // Top-left anchor and anchor point place the canvas by its corner. They do NOT say what it
        // scales about - that is the render-transform pivot, which defaults to the CENTRE.
        canvas.SetAnchor(inkEAnchor.TopLeft);
        canvas.SetAnchorPoint(new Vector2(0.0, 0.0));
        // So the pivot is set, and DO NOT REMOVE IT. test.110 and test.111 left it alone; on this
        // canvas, which has a real size, the centre is (960, 540), and scaling 1.35x about it drew
        // the panel 340 x 191 root units up and left of where the mouse conversion assumed it. Every
        // button still worked - they get their own clicks - but a press on your rail converted onto
        // your own rows and ticked an item in, and the wheel over their list scrolled yours
        // (zeldfep's test.110 log, 2026-11-14: "click 101 (position)" for presses on the ladder).
        // The selector never hit this because its canvas has no size, so its centre IS its corner.
        canvas.SetRenderTransformPivot(new Vector2(0.0, 0.0));
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
            this.Note(s"rail \(this.m_dragList): let go");
            this.m_clicked = 0;
            evt.Handle();
            return true;
        }

        if evt.IsAction(n"click") {
            let id = this.m_clicked;
            this.m_clicked = 0;
            let how = "widget";
            let screen = evt.GetScreenSpacePosition();

            if id > 0 {
                this.MapCheck(id, screen);
            } else {
                // No widget took it. A rail comes first: a release there with no drag in progress
                // means the press never started one, so it is a click on the ladder and the arrow
                // jumps - the ladder works even if OnPostOnPress never reaches this popup. Never
                // falls through to the rows: in test.110 a ladder click ticked an item in.
                if this.CanMap() {
                    let at = this.ToPanel(screen);
                    let r = this.m_hits.RailAt(at.X, at.Y);
                    if r >= 0 {
                        evt.Handle();
                        this.LadderClick(r, at);
                        return true;
                    }
                }
                id = this.FallbackHit(screen);
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

    // Whether a screen position can be turned into panel units yet: the video settings give a
    // screen size and the root has been laid out.
    private func CanMap() -> Bool {
        if !IsDefined(this.m_canvas) || this.m_scale <= 0.0 {
            return false;
        }
        let display = ScreenHelper.GetScreenSize(this.GetGame());
        let size = this.GetRootWidget().GetSize();
        return display.X >= 1.0 && display.Y >= 1.0 && size.X >= 1.0 && size.Y >= 1.0;
    }

    /**
     * Screen position to panel units - once, here, and nowhere else.
     *
     * GetScreenSpacePosition is in real display pixels (docs/DESIGN-LANGUAGE.md, measured on the
     * selector at 1440p), the root is that display at its own size, and the canvas is drawn at the
     * origin scaled about its pivot: drawn = origin + p + (local - p) * scale. This solves that
     * for local. The pivot is READ BACK, not assumed, so the conversion stays the exact inverse of
     * the transform the canvas is given - assuming it is what broke test.110 (OnCreate).
     */
    private func ToPanel(screen: Vector2) -> Vector2 {
        let display = ScreenHelper.GetScreenSize(this.GetGame());
        let size = this.GetRootWidget().GetSize();
        let rx = screen.X * size.X / display.X;
        let ry = screen.Y * size.Y / display.Y;
        let pivot = this.m_canvas.GetRenderTransformPivot();
        let box = this.m_canvas.GetSize();
        let px = pivot.X * box.X;
        let py = pivot.Y * box.Y;
        return new Vector2(px + (rx - this.m_originX - px) / this.m_scale,
                           py + (ry - this.m_originY - py) / this.m_scale);
    }

    // What the conversion rests on, for the "panel open" line.
    public func MapFacts() -> String {
        let display = ScreenHelper.GetScreenSize(this.GetGame());
        let pivot = new Vector2(-1.0, -1.0);
        if IsDefined(this.m_canvas) {
            pivot = this.m_canvas.GetRenderTransformPivot();
        }
        return s"pivot \(pivot.X),\(pivot.Y), the settings say \(display.X)x\(display.Y)";
    }

    /**
     * Ground truth for the conversion. A widget has just said the cursor is on box id, so the
     * conversion of the same click has to land inside that box. The first three of a panel are
     * logged and so is every miss, with how far off it was - so the log answers "is the mouse
     * conversion right" by itself, from clicks the player makes anyway.
     */
    private func MapCheck(id: Int32, screen: Vector2) -> Void {
        let hit = this.m_hits.Find(id);
        if !IsDefined(hit) {
            return;
        }
        this.m_checks += 1;
        if !this.CanMap() {
            if this.m_checks <= 3 {
                this.Note(s"map check: click on \(id) - not converted, the video settings give no screen size");
            }
            return;
        }
        let at = this.ToPanel(screen);
        let inside = at.X >= hit.x - 2.0 && at.X <= hit.x + hit.w + 2.0 && at.Y >= hit.y - 2.0 && at.Y <= hit.y + hit.h + 2.0;
        if this.m_checks > 3 && inside {
            return;
        }
        let said = s"click on \(id) at screen \(screen.X),\(screen.Y) -> panel \(at.X),\(at.Y)";
        let rect = s"its box \(hit.x),\(hit.y) \(hit.w)x\(hit.h)";
        if inside {
            this.Note(s"map check: \(said), inside \(rect)");
        } else {
            let dx = hit.x + hit.w * 0.5 - at.X;
            let dy = hit.y + hit.h * 0.5 - at.Y;
            this.Note(s"map check: \(said), OUTSIDE \(rect) - the conversion is off by about \(dx),\(dy) panel units");
        }
    }

    // A release on a rail that no drag claimed: the arrow jumps to the row under it.
    private func LadderClick(i: Int32, at: Vector2) -> Void {
        let rail = this.m_hits.rails[i];
        let top = rail.Nearest(at.Y);
        this.Note(s"rail \(rail.list): released on the ladder at \(at.X),\(at.Y) with no drag - row \(rail.top + 1) -> \(top + 1)");
        if top != rail.top && IsDefined(this.m_owner) {
            this.m_owner.MpTrSetTop(rail.list, top);
        }
    }

    private func Note(line: String) -> Void {
        if IsDefined(this.m_owner) {
            this.m_owner.MpTrLog(line);
        }
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
        if !evt.IsAction(n"click") || !IsDefined(this.m_owner) {
            return false;
        }
        if !this.CanMap() {
            this.Note("press not placed - the video settings give no screen size");
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
        let how = onArrow ? "picked up the arrow" : "jump";
        this.Note(s"rail \(rail.list): pressed at \(at.X),\(at.Y) - \(how)");
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
            this.Note(s"rail \(this.m_dragList): row \(rail.top + 1) -> \(top + 1)");
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
                this.Note(s"wheel \(dir) over rail \(rail.list) at \(at.X),\(at.Y)");
                this.m_owner.MpTrSetTop(rail.list, rail.Wheel(dir));
                return false;
            }
            i = this.m_hits.ListAt(at.X, at.Y);
            if i >= 0 {
                let list = this.m_hits.rails[i];
                this.Note(s"wheel \(dir) over list \(list.list) at \(at.X),\(at.Y)");
                this.m_owner.MpTrSetTop(list.list, list.top + dir);
                return false;
            }
            this.Note(s"wheel \(dir) at \(at.X),\(at.Y) - over no list, so your list");
        } else {
            this.Note(s"wheel \(dir) not placed - the video settings give no screen size, so your list");
        }
        this.m_owner.MpTrScroll(dir);
        return false;
    }
}
