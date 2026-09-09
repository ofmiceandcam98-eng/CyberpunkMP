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

public func MpTrPanelX() -> Float = 730.0
public func MpTrPanelY() -> Float = 410.0
public func MpTrPanelW() -> Float = 900.0
public func MpTrPanelH() -> Float = 470.0

// ============================================================================ one item row
//
// A traded item: name, a category chip, and a quantity when more than one. Name and category are
// placeholders in the shell; with the wire they come from resolving the item's TweakDBID.

public func MpTrItemRow(parent: ref<inkCompoundWidget>, x: Float, y: Float, name: String,
                        category: String, qty: Int32) -> Void {
    MpCsText(parent, x, y, name, 18, n"Medium", MpTrInk());

    let chipW = 12.0 + Cast<Float>(StrLen(category)) * 8.0;
    MpCsBorder(parent, x, y + 26.0, chipW, 20.0, MpTrCyanDim(), 0.8);
    MpCsText(parent, x + 6.0, y + 28.0, category, 12, n"Regular", MpTrInkDim());

    if qty > 1 {
        MpCsText(parent, x + 360.0, y, s"x\(qty)", 18, n"Medium", MpTrGold());
    }
}

// ============================================================================ the render

public func MpTrBuild(c: ref<inkCanvas>) -> Void {
    let px = MpTrPanelX();
    let py = MpTrPanelY();
    let pw = MpTrPanelW();
    let ph = MpTrPanelH();

    // ------------------------------------------------------------------ plate
    MpCsRect(c, px, py, pw, ph, MpTrPanel(), 0.82);
    MpCsBorder(c, px, py, pw, ph, MpTrCyanDim(), 0.9);
    // The clipped corners: top-right and bottom-left, the design-language signature.
    MpCsNotch(c, px + pw, py, 30.0, MpTrVoid());
    MpCsNotch(c, px, py + ph, 30.0, MpTrVoid());

    // ------------------------------------------------------------------ header
    // Title reads as lit (chromatic aberration), partner named beside it, the overall eddies
    // readout parked on the right - "weight and eddies stay in the header".
    MpCsGlowText(c, px + 24.0, py + 14.0, MpCsSpaced("TRADE"), 26, n"Bold", MpTrCyan());
    MpCsText(c, px + 170.0, py + 21.0, "with Noremac", 18, n"Regular", MpTrInkDim());
    MpCsText(c, px + pw - 250.0, py + 14.0, MpCsSpaced("EDDIES"), 12, n"Regular", MpTrInkFaint());
    MpCsText(c, px + pw - 250.0, py + 30.0, "5,000 / 8,500", 18, n"Medium", MpTrGold());
    MpCsRect(c, px + 20.0, py + 56.0, pw - 40.0, 1.0, MpTrCyanDim(), 0.7);

    // ------------------------------------------------------------------ two columns
    let colTop = py + 78.0;
    let leftX = px + 30.0;
    let rightX = px + 470.0;

    MpCsRect(c, px + 450.0, py + 70.0, 1.0, 320.0, MpTrCyanDim(), 0.5); // divider

    MpCsText(c, leftX, colTop, MpCsSpaced("YOU OFFER"), 15, n"Medium", MpTrCyan());
    MpCsText(c, rightX, colTop, MpCsSpaced("NOREMAC OFFERS"), 15, n"Medium", MpTrRed());

    // Eddies line at the top of each column, then items. Mock content mirrors the mockup.
    let rowTop = colTop + 34.0;
    MpCsText(c, leftX, rowTop, "Eddies", 18, n"Medium", MpTrInkDim());
    MpCsText(c, leftX + 360.0, rowTop, "5,000", 18, n"Medium", MpTrGold());

    MpCsText(c, rightX, rowTop, "Eddies", 18, n"Medium", MpTrInkDim());
    MpCsText(c, rightX + 360.0, rowTop, "8,500", 18, n"Medium", MpTrGold());

    // Left offers eddies only in the mockup; right offers three items with categories.
    MpTrItemRow(c, rightX, rowTop + 54.0, "Militech M-10AF Lexington", "Weapon", 1);
    MpTrItemRow(c, rightX, rowTop + 118.0, "MaxDoc Mk.2", "Consumable", 3);
    MpTrItemRow(c, rightX, rowTop + 182.0, "Kiroshi Optics Mk.1", "Cyberware", 1);

    // ------------------------------------------------------------------ footer
    // Confirmations clear on any change - the server's version-stamped rule, surfaced.
    MpCsText(c, px + 30.0, py + ph - 88.0,
             "Any change to either offer clears both confirmations.", 13, n"Regular",
             MpTrInkFaint());

    let btnY = py + ph - 60.0;
    // Cancel (red) and Confirm (cyan). Drawn as plates with a label; not interactive in the
    // shell - clicks are wired with the data path.
    MpCsRect(c, px + 30.0, btnY, 200.0, 40.0, MpTrRedDim(), 0.6);
    MpCsBorder(c, px + 30.0, btnY, 200.0, 40.0, MpTrRed(), 0.9);
    MpCsText(c, px + 96.0, btnY + 9.0, MpCsSpaced("CANCEL"), 15, n"Medium", MpTrRed());

    MpCsRect(c, px + pw - 230.0, btnY, 200.0, 40.0, MpTrCyanDim(), 0.6);
    MpCsBorder(c, px + pw - 230.0, btnY, 200.0, 40.0, MpTrCyan(), 0.9);
    MpCsText(c, px + pw - 168.0, btnY + 9.0, MpCsSpaced("CONFIRM"), 15, n"Medium", MpTrCyan());
}
