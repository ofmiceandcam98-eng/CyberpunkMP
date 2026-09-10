// THE TALK BUTTON - a persistent megaphone on the multiplayer HUD hotkey bar.
//
// The bar (roster / phone / vehicle / stats) is an authored .inkwidget we do not edit, so
// this button is built at RUNTIME and reparented onto the HUD root, the same way the voice
// "[ TALKING ]" indicator already places itself (GetRootCompoundWidget, BottomLeft anchor).
// It shows the megaphone plus the Y key hint - Y is the rebindable DEFAULT push-to-talk key,
// shown so a new player knows the key - and it BRIGHTENS while the mic is open.
//
// Free-function render, cleared and redrawn on state change (see MpTalkButtonRender). The
// owning field, build and lit-toggle live in MultiplayerGameController.reds, because that is
// our own script class and @addField/@addMethod only attach to pre-compiled game classes.
//
// v1 note: positioned to sit under the stats button by eye - the authored bar's exact button
// coordinates are not in script, so the offset here is a first guess and may want a nudge
// once seen in game (the selector pattern: build, look, refine).

module CyberpunkMP.Ink

import CyberpunkMP.*

// A cyan horn drawn from primitives - no atlas dependency, so it cannot render blank if a
// part name is wrong. A mouthpiece rectangle, a flared cone (rotated), and two short sound
// lines. Lit = full cyan; idle = dimmed ink.
public func MpTalkIcon(parent: ref<inkCompoundWidget>, x: Float, y: Float, lit: Bool) -> Void {
    let col = lit ? MpTrCyan() : MpTrInkDim();
    let op = lit ? 1.0 : 0.75;

    // Mouthpiece - the narrow end.
    MpCsRect(parent, x + 6.0, y + 12.0, 7.0, 12.0, col, op);

    // Cone - a rectangle rotated to read as the flare of the horn.
    let cone = new inkRectangle();
    cone.SetAnchor(inkEAnchor.TopLeft);
    cone.SetAnchorPoint(new Vector2(0.5, 0.5));
    cone.SetMargin(new inkMargin(x + 20.0, y + 18.0, 0.0, 0.0));
    cone.SetSize(new Vector2(20.0, 22.0));
    cone.SetTintColor(col);
    cone.SetOpacity(op);
    cone.SetRotation(-22.0);
    cone.Reparent(parent);

    // Two short sound lines off the wide end.
    let wave1 = new inkRectangle();
    wave1.SetAnchor(inkEAnchor.TopLeft);
    wave1.SetAnchorPoint(new Vector2(0.5, 0.5));
    wave1.SetMargin(new inkMargin(x + 34.0, y + 12.0, 0.0, 0.0));
    wave1.SetSize(new Vector2(9.0, 2.0));
    wave1.SetTintColor(col);
    wave1.SetOpacity(op);
    wave1.SetRotation(-35.0);
    wave1.Reparent(parent);

    let wave2 = new inkRectangle();
    wave2.SetAnchor(inkEAnchor.TopLeft);
    wave2.SetAnchorPoint(new Vector2(0.5, 0.5));
    wave2.SetMargin(new inkMargin(x + 35.0, y + 26.0, 0.0, 0.0));
    wave2.SetSize(new Vector2(9.0, 2.0));
    wave2.SetTintColor(col);
    wave2.SetOpacity(op);
    wave2.SetRotation(35.0);
    wave2.Reparent(parent);
}

// The whole button: a Y key-hint box on the left, an icon box on the right, matched to the
// bar's cyan bordered boxes. Redrawn wholesale on each state change.
public func MpTalkButtonRender(canvas: ref<inkCanvas>, lit: Bool) -> Void {
    canvas.RemoveAllChildren();

    let edge = lit ? MpTrCyan() : MpTrCyanDim();

    // Key-hint box with "Y".
    MpCsRect(canvas, 0.0, 0.0, 46.0, 60.0, MpTrVoid(), lit ? 0.55 : 0.35);
    MpCsBorder(canvas, 0.0, 0.0, 46.0, 60.0, edge, 0.9);
    MpCsText(canvas, 15.0, 15.0, "Y", 28, n"Medium", lit ? MpTrCyan() : MpTrInkDim());

    // Icon box.
    let ix = 54.0;
    MpCsRect(canvas, ix, 0.0, 60.0, 60.0, MpTrVoid(), lit ? 0.55 : 0.35);
    MpCsBorder(canvas, ix, 0.0, 60.0, 60.0, edge, 0.9);
    MpTalkIcon(canvas, ix + 8.0, 6.0, lit);
}
