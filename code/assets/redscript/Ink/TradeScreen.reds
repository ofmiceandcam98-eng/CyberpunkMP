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
public func MpTrPanelW() -> Float = 1120.0
public func MpTrPanelH() -> Float = 560.0

// Overlay zoom over the authored 1:1. v2 authors the composition large (four floating boxes,
// 28px item text), so the base fit is already HUD-sized and this stays 1.0. Bump this one
// number to grow the whole overlay.
public func MpTrZoom() -> Float = 2.6

// Overlay position, as a FRACTION of the chat root's MEASURED size (root.GetSize()) - so it is
// resolution-independent (zeldfep runs 2K; must adapt to any display). The chat box owns the
// bottom-left of this root, so the composition's top-left lands at (fracX, fracY) of the root,
// putting the overlay to the RIGHT of the chat box (zeldfep, 2026-09-14: "move them right past
// the chat box; dynamic to monitor size"). Nudge these two: +X right, +Y down, range 0..1.
public func MpTrShiftFracX() -> Float = 1.04
public func MpTrShiftFracY() -> Float = 0.12

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
    // See-through fill (zeldfep, 2026-09-14: "blur see through") - low alpha so the game reads
    // behind it; the frosted look comes from SetBackgroundBlur in MpTrOpen.
    MpCsRect(c, x, y, w, h, MpTrPanel(), 0.5);
    // Bright cyan border so each box reads as a SEPARATE floating box, not one merged panel.
    MpCsBorder(c, x, y, w, h, MpTrCyan(), 0.8);
}

// ============================================================================ one item row
//
// A traded item at the v2 anchor (28px name = the HUD character-name / Y key-hint size). Square
// bullet, name, a category chip under it, and a gold quantity on the right when more than one.
// Name and category are placeholders in the shell; with the wire they resolve from the TweakDBID.
public func MpTrRow(parent: ref<inkCompoundWidget>, x: Float, y: Float, w: Float, name: String,
                    category: String, qty: Int32) -> Void {
    // Square bullet (mockup's .ic), name, a category chip under it, gold quantity on the right.
    MpCsRect(parent, x, y + 3.0, 14.0, 14.0, MpTrCyan(), 0.85);
    MpCsText(parent, x + 24.0, y, name, 21, n"Medium", MpTrInk());

    let chipW = 16.0 + Cast<Float>(StrLen(category)) * 8.0;
    MpCsBorder(parent, x + 24.0, y + 28.0, chipW, 22.0, MpTrCyanDim(), 0.8);
    MpCsText(parent, x + 32.0, y + 30.0, category, 13, n"Regular", MpTrInkDim());

    if qty > 1 {
        MpCsText(parent, x + w - 66.0, y, s"x\(qty)", 21, n"Medium", MpTrGold());
    }
}

// ============================================================================ the render

// The /tradehelp overlay - a command reference drawn on top of the panel. Typed commands are the
// working interface (the nav keys do not reach a HUD controller without a UI context).
public func MpTrHelp(c: ref<inkCanvas>) -> Void {
    let x = MpTrPanelX() + 70.0;
    let y = MpTrPanelY() + 78.0;
    let w = MpTrPanelW() - 140.0;
    let h = 384.0;
    MpCsRect(c, x, y, w, h, MpTrVoid(), 0.95);
    MpCsBorder(c, x, y, w, h, MpTrCyan(), 1.0);
    MpCsGlowText(c, x + 26.0, y + 18.0, MpCsSpaced("TRADE COMMANDS"), 26, n"Bold", MpTrCyan());

    let lx = x + 26.0;
    let dx = x + 250.0;
    let ly = y + 64.0;
    let step = 29.0;
    MpCsText(c, lx, ly,             "/tradeui",  20, n"Medium", MpTrGold());  MpCsText(c, dx, ly,             "open the trade menu",         20, n"Regular", MpTrInk());
    MpCsText(c, lx, ly + step,      "/tradeoff", 20, n"Medium", MpTrGold());  MpCsText(c, dx, ly + step,      "close it",                    20, n"Regular", MpTrInk());
    MpCsText(c, lx, ly + step*2.0,  "/tr+  /tr-",20, n"Medium", MpTrGold());  MpCsText(c, dx, ly + step*2.0,  "offer more / less eddies",    20, n"Regular", MpTrInk());
    MpCsText(c, lx, ly + step*3.0,  "/trok",     20, n"Medium", MpTrGold());  MpCsText(c, dx, ly + step*3.0,  "confirm",                     20, n"Regular", MpTrInk());
    MpCsText(c, lx, ly + step*4.0,  "/trright /trleft", 20, n"Medium", MpTrGold()); MpCsText(c, dx, ly + step*4.0, "move the menu sideways",   20, n"Regular", MpTrInk());
    MpCsText(c, lx, ly + step*5.0,  "/trup /trdown",    20, n"Medium", MpTrGold()); MpCsText(c, dx, ly + step*5.0, "move the menu up / down",  20, n"Regular", MpTrInk());
    MpCsText(c, lx, ly + step*6.0,  "/trbig /trsmall",  20, n"Medium", MpTrGold()); MpCsText(c, dx, ly + step*6.0, "resize the menu",          20, n"Regular", MpTrInk());
    MpCsText(c, lx, ly + step*7.0,  "/trgap",    20, n"Medium", MpTrGold());  MpCsText(c, dx, ly + step*7.0,  "spacing",                     20, n"Regular", MpTrInk());
    MpCsText(c, lx, ly + step*8.0,  "/trsave",   20, n"Medium", MpTrGold());  MpCsText(c, dx, ly + step*8.0,  "save this position",          20, n"Regular", MpTrInk());
    MpCsText(c, lx, ly + step*9.0,  "/trreset",  20, n"Medium", MpTrGold());  MpCsText(c, dx, ly + step*9.0,  "reset to default position",   20, n"Regular", MpTrInk());
    MpCsText(c, lx, ly + step*10.0, "/tradehelp",18, n"Regular", MpTrInkFaint()); MpCsText(c, dx, ly + step*10.0, "show / hide this list",    18, n"Regular", MpTrInkFaint());
}

public func MpTrBuild(c: ref<inkCanvas>, gap: Float, sel: Int32, eddies: Int32) -> Void {
    let bx = MpTrPanelX();
    let by = MpTrPanelY();
    let bw = MpTrPanelW();
    let bh = MpTrPanelH();

    // ---- ONE cohesive panel (the mockup: "read as one strip, not two floating windows") ----
    // See-through fill; the frosted look comes from SetBackgroundBlur in MpTrOpen.
    MpCsRect(c, bx, by, bw, bh, MpTrPanel(), 0.5);
    MpCsBorder(c, bx, by, bw, bh, MpTrCyan(), 0.8);

    // ---- header: title, partner, weight + eddies, close X, divider ----
    MpCsGlowText(c, bx + 24.0, by + 16.0, MpCsSpaced("TRADE"), 32, n"Bold", MpTrCyan());
    MpCsText(c, bx + 200.0, by + 24.0, "with Noremac", 22, n"Regular", MpTrInkDim());
    MpCsText(c, bx + bw - 420.0, by + 16.0, MpCsSpaced("WEIGHT"), 15, n"Regular", MpTrInkFaint());
    MpCsText(c, bx + bw - 420.0, by + 36.0, "6 / 200", 21, n"Medium", MpTrInk());
    MpCsText(c, bx + bw - 250.0, by + 16.0, MpCsSpaced("EDDIES"), 15, n"Regular", MpTrInkFaint());
    MpCsText(c, bx + bw - 250.0, by + 36.0, "20,100", 21, n"Medium", MpTrGold());
    MpCsBorder(c, bx + bw - 56.0, by + 16.0, 34.0, 34.0, MpTrRed(), 0.85);
    MpCsText(c, bx + bw - 46.0, by + 19.0, "X", 24, n"Bold", MpTrRed());
    if sel == 2 { MpCsBorder(c, bx + bw - 60.0, by + 12.0, 42.0, 42.0, MpTrGold(), 1.0); }
    MpCsRect(c, bx + 18.0, by + 62.0, bw - 36.0, 1.0, MpTrCyanDim(), 0.7);

    // ---- two columns with a vertical divider ----
    let colTop = by + 82.0;
    let colBot = by + bh - 96.0;
    let midX = bx + bw / 2.0;
    let leftX = bx + 26.0;
    let rightX = midX + 26.0;
    let leftW = midX - leftX - 20.0;
    let rightW = bx + bw - rightX - 26.0;
    MpCsRect(c, midX, colTop - 4.0, 1.0, colBot - colTop + 8.0, MpTrCyanDim(), 0.5);

    // left: YOU OFFER - eddies stepper, then your inventory rows (mockup content)
    MpCsText(c, leftX, colTop, MpCsSpaced("YOU OFFER"), 21, n"Medium", MpTrCyan());
    MpCsText(c, leftX, colTop + 44.0, "Eddies", 20, n"Medium", MpTrGold());
    MpCsBorder(c, leftX + 106.0, colTop + 38.0, 250.0, 40.0, MpTrCyanDim(), 0.8);
    MpCsText(c, leftX + 120.0, colTop + 44.0, "-", 24, n"Bold", MpTrInkDim());
    MpCsText(c, leftX + 168.0, colTop + 46.0, s"\(eddies)", 21, n"Medium", MpTrGold());
    MpCsText(c, leftX + 214.0, colTop + 44.0, "+", 24, n"Bold", MpTrCyan());
    MpCsText(c, leftX + 262.0, colTop + 48.0, "/ 20,100", 15, n"Regular", MpTrInkFaint());
    MpTrRow(c, leftX, colTop + 104.0, leftW, "Nekomata", "Sniper", 1);
    MpTrRow(c, leftX, colTop + 168.0, leftW, "MaxDoc Mk.1", "Consumable", 12);
    MpTrRow(c, leftX, colTop + 232.0, leftW, "Scrap Electronics", "Component", 37);

    // right: NOREMAC OFFERS - their three items, then their eddies
    MpCsText(c, rightX, colTop, MpCsSpaced("NOREMAC OFFERS"), 21, n"Medium", MpTrRed());
    MpCsText(c, bx + bw - 46.0, colTop, "3", 21, n"Medium", MpTrRed());
    MpTrRow(c, rightX, colTop + 44.0, rightW, "Militech M-10AF Lexington", "Pistol", 1);
    MpTrRow(c, rightX, colTop + 108.0, rightW, "MaxDoc Mk.2", "Consumable", 6);
    MpTrRow(c, rightX, colTop + 172.0, rightW, "Kiroshi Optics Mk.1", "Cyberware", 1);
    MpCsText(c, rightX, colTop + 240.0, "Eddies", 20, n"Medium", MpTrInkDim());
    MpCsText(c, bx + bw - 118.0, colTop + 240.0, "2,500", 21, n"Medium", MpTrGold());

    // ---- footer: divider, confirm LEDs, note, Cancel / Confirm ----
    let fY = by + bh - 76.0;
    MpCsRect(c, bx + 18.0, fY - 10.0, bw - 36.0, 1.0, MpTrCyanDim(), 0.6);
    MpCsRect(c, bx + 24.0, fY + 3.0, 12.0, 12.0, MpTrRed(), 0.9);
    MpCsText(c, bx + 44.0, fY - 2.0, "You: not confirmed", 19, n"Medium", MpTrInk());
    MpCsRect(c, bx + 24.0, fY + 31.0, 12.0, 12.0, MpTrCyan(), 0.9);
    MpCsText(c, bx + 44.0, fY + 26.0, "Noremac: confirmed", 19, n"Medium", MpTrInk());
    MpCsText(c, bx + 300.0, fY + 40.0,
             "Type  /tradehelp  for all commands      /tr+  /tr- eddies    /trok confirm    /tradeoff exit",
             14, n"Regular", MpTrInkFaint());
    let btnW = 190.0;
    let btnH = 44.0;
    let btnY = fY + 4.0;
    let cancelX = bx + bw - btnW * 2.0 - 40.0;
    MpCsRect(c, cancelX, btnY, btnW, btnH, MpTrRedDim(), 0.6);
    MpCsBorder(c, cancelX, btnY, btnW, btnH, MpTrRed(), 0.9);
    MpCsText(c, cancelX + 56.0, btnY + 11.0, MpCsSpaced("CANCEL"), 20, n"Medium", MpTrRed());
    let confirmX = bx + bw - btnW - 20.0;
    MpCsRect(c, confirmX, btnY, btnW, btnH, MpTrCyanDim(), 0.6);
    MpCsBorder(c, confirmX, btnY, btnW, btnH, MpTrCyan(), 0.9);
    MpCsText(c, confirmX + 50.0, btnY + 11.0, MpCsSpaced("CONFIRM"), 20, n"Medium", MpTrCyan());

    // Selection highlight (Tab cycles it): gold ring around the chosen footer control.
    if sel == 0 { MpCsBorder(c, cancelX - 4.0, btnY - 4.0, btnW + 8.0, btnH + 8.0, MpTrGold(), 1.0); }
    if sel == 1 { MpCsBorder(c, confirmX - 4.0, btnY - 4.0, btnW + 8.0, btnH + 8.0, MpTrGold(), 1.0); }
}
