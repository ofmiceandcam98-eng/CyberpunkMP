// THE TRADE OVERLAY - the render.
//
// An in-game overlay, not a menu screen. It is drawn onto ChatController - the in-game chat HUD
// controller - so it sits on the same layer as chat, over the running game, with chat live to
// its left. That is what the approved mockup asks for in as many words: "It is an overlay, not a
// full-screen menu. Anchored to the same bottom line as chat. Chat stays live behind it."
// Mockup: /mnt/vol/projects/_internal-docs/trade-screen.html.
//
// Built the way the selector ended up working, because that path is proven where an authored
// .inkwidget was not (see the can-a-cli-authored-inkwidget-be-spawned Atlas branch): the ink
// tree is constructed at RUNTIME out of primitives and reparented onto the controller's root,
// and the canvas is SCALED by rootWidth/1920. The composition here is authored in the mockup's
// own 1920x1080.
//
// WHY THIS FILE IS ONLY THE RENDER. The open/close/trigger live in ChatController.reds, not
// here, because @addMethod/@addField/@wrapMethod only attach to pre-compiled game classes -
// ChatController is our own script class, compiled in the same pass, so it cannot be an
// annotation target. These free functions can be called from there (cross-file, same module).
//
// THIS IS THE SHELL: it renders the mockup from MOCK data. Wiring it to real trade state is
// flag-day A (a NotifyTrade protocol message + server emit + client RTTI). The wire carries item
// IDs and quantities; the overlay resolves names and categories from the TweakDBID client-side,
// because item names are a client-side convenience the server does not hold. Until that lands
// the buttons draw but do not act - input wiring rides with the data wiring.

module CyberpunkMP.Ink

import CyberpunkMP.*
import CyberpunkMP.World.*

// ============================================================================ palette
//
// From the mockup's :root, converted to HDRColor. Cyan is the trade accent (the selector's is
// gold); red is the shared warning colour; gold marks the confirming action. Kept at or just
// above 1.0, never the 2.0-channel bloom the selector's gold shipped with - that was illegible
// over a bright backdrop (the dossier fix, 6e4265f), and this panel is semi-transparent over
// live gameplay, which is busier still. A token is a starting point; readability wins.

public func MpTrCyan() -> HDRColor = new HDRColor(0.24, 0.91, 0.94, 1.0)      // #3ce8ef
public func MpTrCyanDim() -> HDRColor = new HDRColor(0.11, 0.43, 0.47, 1.0)   // #1d6e77
public func MpTrRed() -> HDRColor = new HDRColor(0.96, 0.22, 0.29, 1.0)       // #f4374a
public func MpTrRedDim() -> HDRColor = new HDRColor(0.48, 0.12, 0.17, 1.0)    // #7a1f2c
public func MpTrGold() -> HDRColor = new HDRColor(0.96, 0.82, 0.20, 1.0)      // #f5d033
public func MpTrInk() -> HDRColor = new HDRColor(0.91, 0.93, 0.95, 1.0)       // #e8eef2
public func MpTrInkDim() -> HDRColor = new HDRColor(0.62, 0.67, 0.72, 1.0)    // #9dabb7
public func MpTrInkFaint() -> HDRColor = new HDRColor(0.43, 0.47, 0.52, 1.0)  // #6d7986
public func MpTrPanel() -> HDRColor = new HDRColor(0.063, 0.047, 0.086, 1.0)  // panel fill
public func MpTrVoid() -> HDRColor = new HDRColor(0.031, 0.031, 0.047, 1.0)   // #08080c, notch

// The panel is more opaque than the mockup's .62 alpha on purpose: the mockup composites over a
// static screenshot, and a live game frame behind text at read size needs a firmer backing.

// ============================================================================ geometry
//
// Authored 1920x1080. The mockup places the trade panel at left:730 bottom:200 width:900
// height:470; bottom:200 with a 470-tall panel on a 1080 stage is a top edge at 410. Chat sits
// at left:60 with the same bottom line - it is the game's own chat, already on screen, and this
// overlay deliberately does not redraw it.

// v2 (zeldfep, 2026-09-14): four FLOATING boxes, not one plate - so this rect is the overall
// BOUNDING box of the composition (header + two offer columns + actions), used by MpTrOpen to
// re-centre the whole thing on screen. Authored large: item text sits at the 28px HUD anchor.
public func MpTrPanelX() -> Float = 690.0
public func MpTrPanelY() -> Float = 200.0
public func MpTrPanelW() -> Float = 1180.0
public func MpTrPanelH() -> Float = 744.0

// Overlay zoom over the authored 1:1. v2 authors the composition large (four floating boxes,
// 28px item text), so the base fit is already HUD-sized and this stays 1.0. Bump this one
// number to grow the whole overlay.
public func MpTrZoom() -> Float = 1.0

// Overlay position, as a FRACTION of the chat root's MEASURED size (root.GetSize()) - so it is
// resolution-independent (zeldfep runs 2K; must adapt to any display). The chat box owns the
// bottom-left of this root, so the composition's top-left lands at (fracX, fracY) of the root,
// putting the overlay to the RIGHT of the chat box (zeldfep, 2026-09-14: "move them right past
// the chat box; dynamic to monitor size"). Nudge these two: +X right, +Y down, range 0..1.
public func MpTrShiftFracX() -> Float = 0.72
public func MpTrShiftFracY() -> Float = 0.06

// Default gap between the four floating boxes, authored px. Bigger than the first pass (16 read
// as one merged block at scale). Live-tunable via /trgap.
public func MpTrGapDefault() -> Float = 40.0

// ============================================================================ a floating box
//
// v2 is FOUR separate boxes floating on the HUD, not one plate. Each is a fill + border, drawn
// clean (no clipped-corner notch: zeldfep read the notch diamonds as clutter on the selector and
// the talk button, 2026-09-14). Bump the alpha a touch over the mockup - a live game frame behind
// read-size text needs a firmer backing.
public func MpTrBox(c: ref<inkCanvas>, x: Float, y: Float, w: Float, h: Float) -> Void {
    MpCsRect(c, x, y, w, h, MpTrPanel(), 0.92);
    // Bright cyan border so each box reads as a SEPARATE floating box, not one merged panel.
    MpCsBorder(c, x, y, w, h, MpTrCyan(), 0.75);
}

// ============================================================================ one item row
//
// A traded item at the v2 anchor (28px name = the HUD character-name / Y key-hint size). Square
// bullet, name, a category chip under it, and a gold quantity on the right when more than one.
// Name and category are placeholders in the shell; with the wire they resolve from the TweakDBID.
public func MpTrRow(parent: ref<inkCompoundWidget>, x: Float, y: Float, w: Float, name: String,
                    category: String, qty: Int32) -> Void {
    MpCsRect(parent, x, y + 6.0, 16.0, 16.0, MpTrCyan(), 0.85);
    MpCsText(parent, x + 30.0, y, name, 28, n"Medium", MpTrInk());

    let chipW = 20.0 + Cast<Float>(StrLen(category)) * 11.0;
    MpCsBorder(parent, x + 30.0, y + 40.0, chipW, 26.0, MpTrCyanDim(), 0.8);
    MpCsText(parent, x + 40.0, y + 43.0, category, 18, n"Regular", MpTrInkDim());

    if qty > 1 {
        MpCsText(parent, x + w - 90.0, y, s"x\(qty)", 28, n"Medium", MpTrGold());
    }
}

// ============================================================================ the render

public func MpTrBuild(c: ref<inkCanvas>, gap: Float) -> Void {
    let bx = MpTrPanelX();
    let by = MpTrPanelY();
    let bw = MpTrPanelW();

    let headH = 96.0;
    let colY = by + headH + gap;
    let colH = 470.0;
    let colW = (bw - gap) / 2.0;
    let leftX = bx;
    let rightX = bx + colW + gap;
    let actY = colY + colH + gap;
    let actH = 146.0;

    // ------------------------------------------------------------------ header box
    MpTrBox(c, bx, by, bw, headH);
    MpCsGlowText(c, bx + 28.0, by + 22.0, MpCsSpaced("TRADE"), 40, n"Bold", MpTrCyan());
    MpCsText(c, bx + 230.0, by + 40.0, "with Noremac", 28, n"Regular", MpTrInkDim());
    // Weight and eddies readout, parked left of the close button.
    MpCsText(c, bx + bw - 430.0, by + 20.0, MpCsSpaced("WEIGHT"), 18, n"Regular", MpTrInkFaint());
    MpCsText(c, bx + bw - 430.0, by + 44.0, "6 / 200", 26, n"Medium", MpTrInk());
    MpCsText(c, bx + bw - 250.0, by + 20.0, MpCsSpaced("EDDIES"), 18, n"Regular", MpTrInkFaint());
    MpCsText(c, bx + bw - 250.0, by + 44.0, "20,100", 26, n"Medium", MpTrGold());
    // Close X - DRAWN; the overlay closes on /tradeoff until input is wired (flag-day A).
    MpCsBorder(c, bx + bw - 62.0, by + 22.0, 40.0, 40.0, MpTrRed(), 0.85);
    MpCsText(c, bx + bw - 50.0, by + 25.0, "X", 30, n"Bold", MpTrRed());

    // ------------------------------------------------------------------ you offer box
    MpTrBox(c, leftX, colY, colW, colH);
    MpCsText(c, leftX + 24.0, colY + 22.0, MpCsSpaced("YOU OFFER"), 28, n"Medium", MpTrCyan());
    // Eddies stepper box.
    MpCsText(c, leftX + 24.0, colY + 70.0, "Eddies", 28, n"Medium", MpTrInkDim());
    MpCsBorder(c, leftX + 24.0, colY + 108.0, 264.0, 46.0, MpTrCyanDim(), 0.8);
    MpCsText(c, leftX + 38.0, colY + 112.0, "+", 30, n"Bold", MpTrCyan());
    MpCsText(c, leftX + 82.0, colY + 114.0, "0 / 20,100", 26, n"Medium", MpTrGold());
    // Your item rows (restored in v2 - the shell had dropped them).
    MpTrRow(c, leftX + 24.0, colY + 186.0, colW - 48.0, "Arasaka Cyberdeck", "Cyberware", 1);
    MpTrRow(c, leftX + 24.0, colY + 264.0, colW - 48.0, "Pistol Ammo", "Ammo", 120);

    // ------------------------------------------------------------------ noremac offers box
    MpTrBox(c, rightX, colY, colW, colH);
    MpCsText(c, rightX + 24.0, colY + 22.0, MpCsSpaced("NOREMAC OFFERS"), 28, n"Medium", MpTrRed());
    MpCsText(c, rightX + colW - 54.0, colY + 22.0, "3", 28, n"Medium", MpTrRed());
    MpTrRow(c, rightX + 24.0, colY + 70.0, colW - 48.0, "Militech M-10AF Lexington", "Pistol", 1);
    MpTrRow(c, rightX + 24.0, colY + 148.0, colW - 48.0, "MaxDoc Mk.2", "Consumable", 6);
    MpTrRow(c, rightX + 24.0, colY + 226.0, colW - 48.0, "Kiroshi Optics Mk.1", "Cyberware", 1);
    MpCsText(c, rightX + 24.0, colY + 320.0, "Eddies", 28, n"Medium", MpTrInkDim());
    MpCsText(c, rightX + colW - 140.0, colY + 320.0, "2,500", 28, n"Medium", MpTrGold());

    // ------------------------------------------------------------------ actions box
    MpTrBox(c, bx, actY, bw, actH);
    // Confirm LEDs - the two pieces the shell dropped. Red = not confirmed, cyan = confirmed.
    MpCsRect(c, bx + 24.0, actY + 26.0, 14.0, 14.0, MpTrRed(), 0.9);
    MpCsText(c, bx + 48.0, actY + 18.0, "You: not confirmed", 24, n"Medium", MpTrInk());
    MpCsRect(c, bx + 24.0, actY + 62.0, 14.0, 14.0, MpTrCyan(), 0.9);
    MpCsText(c, bx + 48.0, actY + 54.0, "Noremac: confirmed", 24, n"Medium", MpTrInk());
    MpCsText(c, bx + 24.0, actY + actH - 30.0,
             "Any change to either offer clears both confirmations.", 18, n"Regular",
             MpTrInkFaint());

    // Cancel (red) and Confirm (cyan). Drawn as plates; NOT interactive in the shell - clicks
    // ride with the data path (flag-day A).
    let btnW = 220.0;
    let btnH = 52.0;
    let btnY = actY + 30.0;
    let cancelX = bx + bw - btnW * 2.0 - 44.0;
    MpCsRect(c, cancelX, btnY, btnW, btnH, MpTrRedDim(), 0.6);
    MpCsBorder(c, cancelX, btnY, btnW, btnH, MpTrRed(), 0.9);
    MpCsText(c, cancelX + 66.0, btnY + 13.0, MpCsSpaced("CANCEL"), 22, n"Medium", MpTrRed());
    let confirmX = bx + bw - btnW - 24.0;
    MpCsRect(c, confirmX, btnY, btnW, btnH, MpTrCyanDim(), 0.6);
    MpCsBorder(c, confirmX, btnY, btnW, btnH, MpTrCyan(), 0.9);
    MpCsText(c, confirmX + 60.0, btnY + 13.0, MpCsSpaced("CONFIRM"), 22, n"Medium", MpTrCyan());
}
