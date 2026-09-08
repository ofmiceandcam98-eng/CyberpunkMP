// THE CHARACTER SELECTION SCREEN.
//
// Built at runtime out of ink primitives, hung on the main menu controller's root. There
// is no .inkwidget behind it and no archive to load: everything here is a rectangle, a
// piece of text, or a margin. That is a deliberate trade rather than a shortcut -
// authoring a widget resource means building WolvenKit, packing an archive and shipping a
// flag-day-sized asset change, and this screen can exist tonight without any of it.
//
// WHAT IS MISSING BECAUSE OF THAT TRADE, so nobody has to rediscover it: the photographic
// backdrop. An image needs a texture inside an .archive. Until that lands, the backplate
// below stands in for it - a dark scrim with the mockup's own sky colours bled across it.
// Everything else - the plates, the clipped corners, the gold active state, the type
// scale, the detail panel - is the design as approved.
//
// Coordinates are the mockup's, at the 1920x1080 the game lays ink out in. They are
// absolute on purpose: this screen is a composition, and a flow layout would drift the
// moment a name ran long.

import CyberpunkMP.World.*

/*
 * The palette, straight from the mockup, as HDRColor.
 *
 * Named rather than inlined because the same six colours appear thirty times between here
 * and the bottom of the file, and the design language's whole point is that a value is
 * picked from the palette rather than invented at the call site. Anything above 1.0 is
 * deliberate - ink treats it as emission, which is what makes gold on a dark plate read as
 * lit rather than painted.
 */
public func MpCsGold() -> HDRColor = new HDRColor(2.0, 1.68, 0.24, 1.0)
public func MpCsGoldDim() -> HDRColor = new HDRColor(0.49, 0.42, 0.11, 1.0)
public func MpCsRed() -> HDRColor = new HDRColor(1.0, 0.18, 0.27, 1.0)
public func MpCsRedDim() -> HDRColor = new HDRColor(0.49, 0.11, 0.16, 1.0)
public func MpCsInk() -> HDRColor = new HDRColor(0.95, 0.96, 0.97, 1.0)
public func MpCsInkDim() -> HDRColor = new HDRColor(0.59, 0.63, 0.67, 1.0)
public func MpCsInkFaint() -> HDRColor = new HDRColor(0.37, 0.42, 0.46, 1.0)
public func MpCsPlate() -> HDRColor = new HDRColor(0.04, 0.03, 0.055, 1.0)
public func MpCsVoid() -> HDRColor = new HDRColor(0.016, 0.016, 0.031, 1.0)

// The font the game itself uses. Borrowing it rather than shipping one is why this screen
// reads as part of Cyberpunk instead of as an overlay sitting on top of it.
public func MpCsFont() -> String = "base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily"

/**
 * THE ONLY LOGGING ON THIS SCREEN THAT ANYBODY WILL EVER READ.
 *
 * FTLog reaches NO COLLECTED FILE. It goes to the game's own log; the redscript log beside
 * it is scc.exe's COMPILE output and scc exits before the game runs, so nothing is left in
 * the process to capture runtime script output. LogChannel needs CET, which is not one of
 * our prerequisites. Measured 2026-09-07 on a live box: five redscript logs, zero
 * [Selector] lines in any of them.
 *
 * ScriptLog is native and lands in CyberpunkMP.log, which the launcher has uploaded for
 * weeks. That is the difference between "which menu branch ran" being answerable from the
 * server and being guessed at - and it was guessed at for a whole evening, expensively.
 *
 * FTLog is called TOO, deliberately. It costs nothing, and it is still the fastest thing to
 * read when the game is on the same machine as the person debugging.
 */
public func MpCsLog(text: String) -> Void {
    FTLog(s"[Selector] \(text)");

    let network = GameInstance.GetNetworkWorldSystem();

    if IsDefined(network) {
        network.ScriptLog(s"[Selector] \(text)");
    }
}

/**
 * How many slots exist at all, locked or not.
 *
 * MUST MATCH PlayerStore::kMaxSlots. The two are separate constants in separate languages
 * because the ceiling is not something the protocol carries - the server sends how many
 * slots an account may USE (GetCharacterSlots), and the total is a fact about the product
 * rather than about the account. If the server ever entitles somebody to more than this,
 * the extra slots are simply not drawn, so a change to one is a change to both.
 */
public func MpCsMaxSlots() -> Int32 = 4

/**
 * A filled rectangle at an absolute position.
 *
 * Every plate, rule, border and bar on this screen is one of these. Anchored top-left with
 * an anchor POINT of (0,0) so the margin reads as x/y from the top-left corner of the
 * parent - without the anchor point the widget's own centre lands on the corner instead,
 * which is the bug that ate an evening on the old panel.
 */
public func MpCsRect(parent: ref<inkCompoundWidget>, x: Float, y: Float, w: Float, h: Float,
                     color: HDRColor, opacity: Float) -> ref<inkRectangle> {
    let r = new inkRectangle();
    r.SetAnchor(inkEAnchor.TopLeft);
    r.SetAnchorPoint(new Vector2(0.0, 0.0));
    r.SetMargin(new inkMargin(x, y, 0.0, 0.0));
    r.SetSize(new Vector2(w, h));
    r.SetTintColor(color);
    r.SetOpacity(opacity);
    r.Reparent(parent);

    return r;
}

/**
 * A line of text at an absolute position.
 */
public func MpCsText(parent: ref<inkCompoundWidget>, x: Float, y: Float, text: String,
                     size: Int32, style: CName, color: HDRColor) -> ref<inkText> {
    let t = new inkText();
    t.SetAnchor(inkEAnchor.TopLeft);
    t.SetAnchorPoint(new Vector2(0.0, 0.0));
    t.SetMargin(new inkMargin(x, y, 0.0, 0.0));
    t.SetFontFamily(MpCsFont());
    t.SetFontStyle(style);
    t.SetFontSize(size);
    t.SetText(text);
    t.SetTintColor(color);
    t.Reparent(parent);

    return t;
}

/**
 * THE CLIPPED CORNER, which is the signature of this whole design language.
 *
 * ink has no polygon clipping, so the notch is drawn rather than cut: a square the colour
 * of what is BEHIND the plate, rotated 45 degrees and parked over the corner so it eats
 * it. The rotation is why this works at all - an axis-aligned square would just be a
 * smaller square sitting in the corner.
 *
 * Only the top-right and bottom-left are notched, which is the asymmetry the mockup uses
 * and the game's own panels use: notching all four reads as a stop sign.
 */
public func MpCsNotch(parent: ref<inkCompoundWidget>, x: Float, y: Float, size: Float,
                      color: HDRColor) -> Void {
    let n = new inkRectangle();
    n.SetAnchor(inkEAnchor.TopLeft);
    n.SetAnchorPoint(new Vector2(0.5, 0.5));
    n.SetMargin(new inkMargin(x, y, 0.0, 0.0));
    n.SetSize(new Vector2(size, size));
    n.SetTintColor(color);
    n.SetRotation(45.0);
    n.Reparent(parent);
}

/**
 * A one-pixel outline, as four rules.
 *
 * Cheaper to read than to look at: ink rectangles have no stroke, so a border is the four
 * edges drawn individually. Kept in one function because getting three of the four right
 * and the fourth an pixel off is exactly the kind of thing nobody sees until it ships.
 */
public func MpCsBorder(parent: ref<inkCompoundWidget>, x: Float, y: Float, w: Float, h: Float,
                       color: HDRColor, opacity: Float) -> Void {
    MpCsRect(parent, x, y, w, 1.0, color, opacity);
    MpCsRect(parent, x, y + h - 1.0, w, 1.0, color, opacity);
    MpCsRect(parent, x, y, 1.0, h, color, opacity);
    MpCsRect(parent, x + w - 1.0, y, 1.0, h, color, opacity);
}

// ============================================================================ state

@addField(SingleplayerMenuGameController)
let m_csRoot: wref<inkCanvas>;

// Which slot the caret is on. This is the SCREEN's selection, not the server's - pressing
// a card moves this immediately so the screen answers the press, and the server's own
// answer arrives afterwards as a fresh roster and redraws everything.
@addField(SingleplayerMenuGameController)
let m_csCursor: Int32;

@addField(SingleplayerMenuGameController)
let m_csStatus: wref<inkText>;

@addField(SingleplayerMenuGameController)
let m_csOpen: Bool;

// ============================================================================ build

/**
 * Open the selector, or rebuild it in place if it is already open.
 *
 * Rebuilding wholesale rather than mutating the widgets that changed. It is a few hundred
 * rectangles on a menu that is not rendering a world, the cost is invisible, and the
 * alternative is holding an array of widget references per slot and keeping them in step
 * with a roster that can change shape underneath them - which is where this kind of screen
 * usually goes wrong.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsOpen() -> Void {
    let root = this.GetRootCompoundWidget();

    if !IsDefined(root) {
        MpCsLog(s"no root widget - cannot open the character screen");
        return;
    }

    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) {
        return;
    }

    if IsDefined(this.m_csRoot) {
        this.m_csRoot.RemoveAllChildren();
    } else {
        let canvas = new inkCanvas();
        canvas.SetName(n"mp_character_select");
        canvas.SetAnchor(inkEAnchor.Fill);
        canvas.SetInteractive(true);
        canvas.Reparent(root);

        this.m_csRoot = canvas;
    }

    /*
     * THE MENU'S COORDINATE SPACE IS NOT 1920x1080, AND ASSUMING IT WAS PUT THIS SCREEN AT
     * HALF SIZE IN THE CORNER.
     *
     * Measured live 2026-09-07: a rectangle built 1920 wide covered about 960 screen pixels,
     * so this controller's root lays out in roughly double the virtual resolution ink is
     * usually described in. Every absolute coordinate below landed at half its intended
     * place - the dossier panel drew on top of the roster and the backplate stopped in the
     * middle of the screen.
     *
     * Rather than doubling every number and hoping, the composition stays authored in the
     * mockup's own 1920x1080 and the CANVAS is scaled to whatever the root actually is. That
     * is self-correcting: it is right on this controller, right at any resolution, and right
     * if a patch changes the space underneath us.
     *
     * A root that has not been laid out yet reports zero, and scaling by zero would draw
     * nothing at all - so an unmeasurable root falls back to 1:1, which is wrong in exactly
     * the way the screenshot showed rather than invisible.
     */
    let rootSize = root.GetSize();
    let scale = 1.0;

    if rootSize.X > 1.0 {
        scale = rootSize.X / 1920.0;
    }

    this.m_csRoot.SetScale(new Vector2(scale, scale));

    MpCsLog(s"root is \(rootSize.X)x\(rootSize.Y) - composition scaled by \(scale)");

    this.m_csOpen = true;
    this.m_csRoot.SetVisible(true);

    let c = this.m_csRoot;

    // ---------------------------------------------------------------- backplate
    //
    // THE RENDER, as a real texture. It ships in zz_NightCityOnline_Selector.archive as an
    // xbm plus a single-texture inkatlas - an inkImage can only bind an ATLAS, never a raw
    // xbm, which is the whole reason that atlas exists.
    //
    // What was here before was three translucent rectangles standing in for a skyline, and
    // they read exactly as badly as that sounds: flat magenta and blue slabs across the top
    // half of the screen. They are gone rather than layered under the image.
    //
    // An opaque black bed goes down FIRST. The image is drawn at the 1920x1080 it was
    // authored at, and anything the menu is showing behind it - the badlands, the expansion
    // logo - must not read through the edges if a resolution ever leaves a seam.
    // Anchored Fill rather than sized, deliberately. Every other coordinate on this screen
    // depends on the scale measured above being right; this one does not. If the root ever
    // reports zero and the scale falls back to 1:1, the composition is wrong but the screen
    // is still BLACK behind it rather than half-covered over the game's own menu - which is
    // the difference between something that looks unfinished and something that looks broken.
    let bed = new inkRectangle();
    bed.SetName(n"mp_cs_bed");
    bed.SetAnchor(inkEAnchor.Fill);
    bed.SetMargin(new inkMargin(0.0, 0.0, 0.0, 0.0));
    bed.SetTintColor(MpCsVoid());
    bed.SetOpacity(1.0);
    bed.Reparent(c);

    let backdrop = new inkImage();
    backdrop.SetName(n"mp_cs_backdrop");
    backdrop.SetAnchor(inkEAnchor.TopLeft);
    backdrop.SetAnchorPoint(new Vector2(0.0, 0.0));
    backdrop.SetMargin(new inkMargin(0.0, 0.0, 0.0, 0.0));
    backdrop.SetSize(new Vector2(1920.0, 1080.0));
    backdrop.SetAtlasResource(r"nightcityonline\\character_select_bg.inkatlas");
    backdrop.SetTexturePart(n"whole");
    backdrop.Reparent(c);

    // Just enough gradient for panel text to sit on, as the mockup put it - "the picture is
    // the design; washing it out to make room for UI would waste it". Two soft plates down
    // the left and bottom where the roster and the footer live, and nothing anywhere else.
    MpCsRect(c, 0.0, 0.0, 720.0, 1080.0, MpCsVoid(), 0.55);
    MpCsRect(c, 0.0, 880.0, 1920.0, 200.0, MpCsVoid(), 0.45);

    // Registration marks. The mockup's corner brackets - the detail that says "this is an
    // instrument you are reading" rather than "this is a dialog box".
    this.MpCsRegistration(c);

    // ---------------------------------------------------------------- title
    MpCsText(c, 68.0, 96.0, "NIGHT CITY ONLINE", 14, n"Medium", MpCsGold());
    MpCsText(c, 68.0, 122.0, "SELECT IDENTITY", 76, n"Bold", MpCsInk());
    MpCsText(c, 68.0, 214.0, "WHO ARE YOU TONIGHT", 15, n"Regular", MpCsInkFaint());
    MpCsRect(c, 68.0, 248.0, 540.0, 1.0, MpCsRed(), 0.9);

    // ---------------------------------------------------------------- roster
    //
    // ALL FOUR SLOTS ARE ALWAYS DRAWN, whether or not this account may use them. zeldfep,
    // 2026-09-07: "4 for devs and 4 for public players 1 slot unlocked 3 more after
    // purchasing so just grey them out for now".
    //
    // Drawing only what somebody owns would mean a public account sees one card and no
    // indication that more exist. Four cards with three greyed says what is on offer, and
    // the day slots become purchasable the screen already has the shelf to sell from.
    let unlocked = network.GetCharacterSlots();

    if unlocked > MpCsMaxSlots() {
        unlocked = MpCsMaxSlots();
    }

    if unlocked < 1 {
        unlocked = 1;
    }

    // Keep the caret on something real, and never on a slot this account cannot use. It can
    // point at a retired slot after a delete, and a dossier describing a character that is
    // no longer there is worse than no dossier.
    if this.m_csCursor < 0 || this.m_csCursor >= unlocked {
        this.m_csCursor = this.MpCsActiveSlot();
    }

    let slot = 0;

    while slot < MpCsMaxSlots() {
        this.MpCsCard(c, slot, 68.0, 322.0 + Cast<Float>(slot) * 99.0, slot < unlocked);
        slot += 1;
    }

    // ---------------------------------------------------------------- detail
    this.MpCsDetail(c, 1232.0, 196.0);

    // ---------------------------------------------------------------- actions
    //
    // The three verbs, on the game's own menu items rather than here. These are LABELS of
    // what the menu underneath can do, drawn in the mockup's positions so the composition
    // is right; the presses themselves stay on menu items, which are focusable and
    // controller-navigable in a way a runtime widget is not.
    this.MpCsStatusLine(c, 68.0, 934.0);

    MpCsText(c, 68.0, 1004.0, "IDENTITY IS A TOOL. MAKE IT YOURS.", 13, n"Regular", MpCsInkFaint());

    MpCsLog(s"character screen open - \(unlocked) of \(MpCsMaxSlots()) slot(s) unlocked, caret on \(this.m_csCursor)");

    /*
     * PIPELINE PROBE - answers one question and then comes out.
     *
     * Can a widget library authored entirely from the command line be loaded by the game?
     * character_select.inkwidget was built by serialising an existing library to JSON,
     * keeping Root plus one item, renaming it, and deserialising - never opened in the
     * WolvenKit GUI. Everything the authored screen depends on rests on the answer, so it
     * is asked before the layout is generated rather than after.
     *
     * It is asked HERE, in a build somebody plays, because the offline checks cannot
     * answer it: the file round-trips and re-reads perfectly and could still be refused by
     * the game. The round trip is known to be lossy - multiplayer_ui.inkwidget came back
     * 53 bytes smaller with no edits - which is exactly why this library is a NEW file and
     * not an edit of the one holding chat, emotes and the job list.
     *
     * Nothing is attached to the screen either way. The spawned widget is logged and
     * dropped; the screen you are looking at is still the runtime-built one.
     */
    /*
     * TWO ASKS, ONE OF THEM A CONTROL.
     *
     * test.26 asked only for the authored library and got SILENCE - no spawned callback and
     * no failed callback, because an unresolvable resource never calls back at all. That is
     * a third outcome the probe did not have a branch for, so it proved nothing: it could
     * not tell "my library is bad" from "this controller cannot async-spawn anything".
     *
     * So the same call is made against a library that is KNOWN to work - multiplayer_ui,
     * which Death.reds and the whole HUD spawn from every session. The pair separates the
     * two:
     *
     *   control answers, mine silent  -> the authored library is the problem
     *   both silent                   -> the menu controller cannot spawn, library is fine
     *   both answer                   -> the authored path works, look elsewhere
     */
    this.m_csProbeControl = false;
    this.m_csProbeAuthored = false;

    this.AsyncSpawnFromExternal(this.m_csRoot,
                                r"mods\\cyberpunkmp\\multiplayer_ui.inkwidget",
                                n"server_list", this, n"OnMpCsProbeControl");

    this.AsyncSpawnFromExternal(this.m_csRoot,
                                r"nightcityonline\\character_select.inkwidget",
                                n"character_select", this, n"OnMpCsProbeAuthored");

    MpCsLog(s"probe: asked for BOTH libraries - a verdict line follows in 3s");

    // SILENCE HAS TO REPORT ITSELF. Waiting on a callback that never comes is exactly the
    // shape that made test.26 worthless, and it is the same lesson as the stale-workload
    // decree: a wait needs a deadline and a failure branch, or it cannot be told from
    // still-working.
    let verdict = new MpCsProbeVerdict();
    verdict.controller = this;

    GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(verdict, 3.0, false);
}

/**
 * Reads the probe out three seconds after both asks. See MpCsOpen for the experiment.
 *
 * A DelayCallback rather than trusting the callbacks to arrive, because the whole point is
 * that they might not - and a probe whose failure mode is "nothing is written anywhere" is
 * not a probe.
 */
public class MpCsProbeVerdict extends DelayCallback {
    public let controller: wref<SingleplayerMenuGameController>;

    public func Call() -> Void {
        if !IsDefined(this.controller) {
            return;
        }

        this.controller.MpCsProbeReport();
    }
}

@addMethod(SingleplayerMenuGameController)
public func MpCsProbeReport() -> Void {
    let control = this.m_csProbeControl;
    let authored = this.m_csProbeAuthored;

    if control && authored {
        MpCsLog(s"probe VERDICT: both spawned - the authored library WORKS, build the real screen on it");
        return;
    }

    if control && !authored {
        MpCsLog(s"probe VERDICT: control spawned, authored did NOT - the library I built is the problem, not the call");
        return;
    }

    if !control && !authored {
        MpCsLog(s"probe VERDICT: NEITHER spawned - this controller cannot async-spawn here; my library is not implicated");
        return;
    }

    MpCsLog(s"probe VERDICT: authored spawned but the control did not - unexpected, treat the control as suspect");
}

@addField(SingleplayerMenuGameController)
let m_csProbeControl: Bool;

@addField(SingleplayerMenuGameController)
let m_csProbeAuthored: Bool;

// The known-good library. If this one does not arrive, nothing about the authored file is
// proven either way - which is exactly the hole test.26 fell into.
@addMethod(SingleplayerMenuGameController)
protected cb func OnMpCsProbeControl(widget: ref<inkWidget>, userData: ref<IScriptable>) -> Bool {
    this.m_csProbeControl = IsDefined(widget);

    if IsDefined(widget) {
        widget.SetVisible(false);
    }

    MpCsLog(s"probe: control callback fired, widget=\(IsDefined(widget))");
    return true;
}

// The library authored entirely from the command line. This is the one under test.
@addMethod(SingleplayerMenuGameController)
protected cb func OnMpCsProbeAuthored(widget: ref<inkWidget>, userData: ref<IScriptable>) -> Bool {
    this.m_csProbeAuthored = IsDefined(widget);

    if IsDefined(widget) {
        widget.SetVisible(false);
    }

    MpCsLog(s"probe: authored callback fired, widget=\(IsDefined(widget))");
    return true;
}

/**
 * Close the screen without destroying it. Reopening is then a rebuild rather than a
 * reparent, and the menu underneath is never left with an invisible interactive canvas
 * over it eating clicks.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsClose() -> Void {
    if IsDefined(this.m_csRoot) {
        this.m_csRoot.SetVisible(false);
        this.m_csRoot.SetInteractive(false);
    }

    this.m_csOpen = false;
}

/**
 * The four corner brackets.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsRegistration(parent: ref<inkCanvas>) -> Void {
    let a = 0.55;

    // top-left
    MpCsRect(parent, 34.0, 34.0, 20.0, 1.0, MpCsRed(), a);
    MpCsRect(parent, 34.0, 34.0, 1.0, 20.0, MpCsRed(), a);
    // top-right
    MpCsRect(parent, 1866.0, 34.0, 20.0, 1.0, MpCsRed(), a);
    MpCsRect(parent, 1885.0, 34.0, 1.0, 20.0, MpCsRed(), a);
    // bottom-left
    MpCsRect(parent, 34.0, 1045.0, 20.0, 1.0, MpCsRed(), a);
    MpCsRect(parent, 34.0, 1026.0, 1.0, 20.0, MpCsRed(), a);
    // bottom-right
    MpCsRect(parent, 1866.0, 1045.0, 20.0, 1.0, MpCsRed(), a);
    MpCsRect(parent, 1885.0, 1026.0, 1.0, 20.0, MpCsRed(), a);
}

/**
 * One character card.
 *
 * 568 x 90 with an 18px notch, the mockup's dimensions. The active card is the one piece
 * of gold on the screen: gold border, a lit plate, a 5px bar hanging off its left edge,
 * and it sits 14px further right than the others. That offset is doing real work - it is
 * readable at a glance and from across a room, which a colour change alone is not.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsCard(parent: ref<inkCanvas>, slot: Int32, x: Float, y: Float,
                     unlocked: Bool) -> Void {
    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) {
        return;
    }

    let index = this.MpCsRosterIndex(slot);
    let occupied = index >= 0;
    let selected = unlocked && slot == this.m_csCursor;
    let w = 568.0;
    let h = 90.0;

    /*
     * A LOCKED SLOT IS DRAWN, DIMMED, AND DOES NOTHING.
     *
     * Deliberately not hidden: the point of showing it is that somebody can see there are
     * three more and that they are obtainable. Deliberately not pressable either - a card
     * that highlights and then refuses is a worse answer than a card that never pretended.
     */
    if !unlocked {
        MpCsRect(parent, x, y, w, h, MpCsPlate(), 0.45);
        MpCsBorder(parent, x, y, w, h, MpCsRedDim(), 0.35);
        MpCsNotch(parent, x + w, y, 26.0, MpCsVoid());
        MpCsNotch(parent, x, y + h, 26.0, MpCsVoid());

        MpCsText(parent, x + 15.0, y + 36.0, s"0\(slot + 1)", 13, n"Regular", MpCsInkFaint());
        MpCsText(parent, x + 128.0, y + 32.0, "LOCKED", 26, n"Bold", MpCsInkFaint());
        MpCsText(parent, x + 128.0, y + 62.0, "AN EXTRA CHARACTER SLOT", 13, n"Regular",
                 MpCsInkFaint());

        return;
    }

    // The selected card steps right, as in the mockup.
    let cx = selected ? x + 14.0 : x;

    let plate = selected ? MpCsGold() : MpCsPlate();
    let plateAlpha = selected ? 0.14 : 0.72;
    let edge = selected ? MpCsGold() : MpCsRedDim();
    let edgeAlpha = selected ? 1.0 : 0.85;

    MpCsRect(parent, cx, y, w, h, plate, plateAlpha);
    MpCsBorder(parent, cx, y, w, h, edge, edgeAlpha);

    // The notches, painted the colour of the backplate so they read as cut out of the card.
    MpCsNotch(parent, cx + w, y, 26.0, MpCsVoid());
    MpCsNotch(parent, cx, y + h, 26.0, MpCsVoid());

    if selected {
        MpCsRect(parent, cx - 14.0, y + 6.0, 5.0, h - 12.0, MpCsGold(), 1.0);
    }

    // Slot number.
    MpCsText(parent, cx + 15.0, y + 36.0, s"0\(slot + 1)", 13, n"Regular",
             selected ? MpCsGold() : MpCsInkFaint());

    // The chip - a 64px square with the character's initial in it. Empty slots get a
    // dash, which is the one thing that distinguishes "nobody here" from "somebody whose
    // name failed to load".
    let chipX = cx + 47.0;
    let chipY = y + 13.0;

    MpCsRect(parent, chipX, chipY, 64.0, 64.0, MpCsRed(), occupied ? 0.14 : 0.05);
    MpCsBorder(parent, chipX, chipY, 64.0, 64.0, edge, occupied ? 0.9 : 0.4);
    MpCsNotch(parent, chipX + 64.0, chipY, 16.0, MpCsVoid());

    if !occupied {
        MpCsText(parent, chipX + 26.0, chipY + 18.0, "+", 24, n"Bold",
                 selected ? MpCsGold() : MpCsInkFaint());
        MpCsText(parent, cx + 128.0, y + 32.0, "EMPTY SLOT", 26, n"Bold",
                 selected ? MpCsGold() : MpCsInkFaint());

        // An empty slot is a DESTINATION now, not a dead row. Selecting it points the
        // account at it, and the creator's save lands there - which is what makes a second
        // character an addition rather than a replacement.
        MpCsText(parent, cx + 128.0, y + 62.0,
                 selected ? "READY - CREATE NEW CHARACTER" : "SELECT TO CREATE HERE", 13,
                 n"Regular", selected ? MpCsGold() : MpCsInkFaint());

        this.MpCsArm(parent, slot, cx, y, w, h);
        return;
    }

    let name = network.GetRosterName(Cast<Uint32>(index));
    let shown = NotEquals(name, "") ? name : "unnamed";

    MpCsText(parent, chipX + 24.0, chipY + 16.0, StrLeft(shown, 1), 24, n"Bold",
             selected ? MpCsGold() : MpCsInkDim());

    MpCsText(parent, cx + 128.0, y + 24.0, shown, 26, n"Bold",
             selected ? MpCsGold() : MpCsInk());

    // Lifepath and state on one line. This is what the protocol flag day was FOR - with
    // four characters the name alone stops being enough to tell them apart.
    let lifepath = network.GetRosterLifepath(Cast<Uint32>(index));
    let meta = NotEquals(lifepath, "") ? lifepath : "no lifepath on record";

    if !network.HasRosterSpawnedBefore(Cast<Uint32>(index)) {
        meta += "  -  NEVER PLAYED";
    }

    MpCsText(parent, cx + 128.0, y + 58.0, meta, 14, n"Regular", MpCsInkFaint());

    // Level, right-aligned by measurement rather than by anchor: the card is a canvas and
    // the number is at most four glyphs, so a fixed inset lands it in the same place every
    // time without a second layout pass.
    let level = network.GetRosterLevel(Cast<Uint32>(index));

    MpCsText(parent, cx + w - 84.0, y + 32.0, s"LV \(level)", 26, n"Regular",
             selected ? MpCsGold() : MpCsInkDim());

    this.MpCsArm(parent, slot, cx, y, w, h);
}

/**
 * Make a card pressable.
 *
 * A transparent interactive rectangle laid over the whole card, named for its slot. The
 * name is how the callback knows which card was hit - ink hands the handler the widget it
 * landed on, and reading a name off it is far less fragile than keeping a parallel array
 * of references in step with a roster that changes shape.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsArm(parent: ref<inkCanvas>, slot: Int32, x: Float, y: Float, w: Float,
                    h: Float) -> Void {
    let hit = new inkRectangle();
    hit.SetName(this.MpCsHitName(slot));
    hit.SetAnchor(inkEAnchor.TopLeft);
    hit.SetAnchorPoint(new Vector2(0.0, 0.0));
    hit.SetMargin(new inkMargin(x, y, 0.0, 0.0));
    hit.SetSize(new Vector2(w, h));
    hit.SetOpacity(0.0);
    hit.SetInteractive(true);
    hit.Reparent(parent);

    hit.RegisterToCallback(n"OnRelease", this, n"OnMpCsCardRelease");
}

@addMethod(SingleplayerMenuGameController)
public func MpCsHitName(slot: Int32) -> CName {
    if slot == 0 {
        return n"mp_cs_hit_0";
    }

    if slot == 1 {
        return n"mp_cs_hit_1";
    }

    if slot == 2 {
        return n"mp_cs_hit_2";
    }

    return n"mp_cs_hit_3";
}

@addMethod(SingleplayerMenuGameController)
public func MpCsSlotFromHit(name: CName) -> Int32 {
    if Equals(name, n"mp_cs_hit_0") {
        return 0;
    }

    if Equals(name, n"mp_cs_hit_1") {
        return 1;
    }

    if Equals(name, n"mp_cs_hit_2") {
        return 2;
    }

    if Equals(name, n"mp_cs_hit_3") {
        return 3;
    }

    return -1;
}

/**
 * A card was clicked.
 *
 * Moves the caret and redraws immediately, then asks the server. Answering the press on
 * the frame it happens is the whole difference between a screen that feels built and one
 * that feels broken - the old panel's only feedback for a press was a load or nothing.
 *
 * Empty slots move the caret and say what fills them, and send NOTHING. The server's reply
 * to "select an empty slot" is a refusal nobody asked for.
 */
@addMethod(SingleplayerMenuGameController)
protected cb func OnMpCsCardRelease(e: ref<inkPointerEvent>) -> Bool {
    if !e.IsAction(n"click") {
        return false;
    }

    let target = e.GetTarget();

    if !IsDefined(target) {
        return false;
    }

    let slot = this.MpCsSlotFromHit(target.GetName());

    if slot < 0 {
        return false;
    }

    this.m_csCursor = slot;

    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) || !network.IsConnected() {
        MpCsLog(s"card pressed with no connection");
        this.MpCsOpen();
        return true;
    }

    let empty = this.MpCsRosterIndex(slot) < 0;

    // The same call either way. The server accepts an empty slot as a destination - that is
    // how a new character gets somewhere to go - so there is no special case here beyond
    // what the screen says while it waits.
    network.SelectCharacterSlot(slot);
    this.MpCsOpen();
    this.MpCsSay(empty ? "Slot armed - press CREATE NEW CHARACTER."
                       : "switching...");

    // The answer comes back as a fresh roster, so poll for it rather than assuming the
    // switch took. SelectCharacterSlot only says the request was sent.
    let poll = new MpSelectorPoll();
    poll.controller = this;
    poll.attempts = 0;
    poll.enterWhenKnown = false;

    GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(poll, 0.25, false);
    return true;
}

// ============================================================================ detail

/**
 * The dossier on whoever the caret is on.
 *
 * 620 wide with a striped gold header, which is the design language's "this is the thing
 * in play" marker - flat fills are states, stripes are attention.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsDetail(parent: ref<inkCanvas>, x: Float, y: Float) -> Void {
    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) {
        return;
    }

    let w = 620.0;
    let h = 460.0;

    MpCsRect(parent, x, y, w, h, MpCsPlate(), 0.82);
    MpCsBorder(parent, x, y, w, h, MpCsGoldDim(), 0.9);
    MpCsNotch(parent, x + w, y, 34.0, MpCsVoid());
    MpCsNotch(parent, x, y + h, 34.0, MpCsVoid());

    // The hazard strip: alternating gold and dark, drawn as blocks. Striped rather than
    // flat because this panel is what the ENTER press acts on - it is the screen's claim
    // about who you are about to become, and that is worth marking.
    let i = 0;

    while i < 34 {
        MpCsRect(parent, x + 1.0 + Cast<Float>(i) * 18.0, y + 1.0, 9.0, 7.0, MpCsGold(), 0.85);
        i += 1;
    }

    let index = this.MpCsRosterIndex(this.m_csCursor);

    if index < 0 {
        MpCsText(parent, x + 26.0, y + 40.0, "NO CHARACTER", 40, n"Bold", MpCsInkFaint());
        MpCsRect(parent, x + 26.0, y + 100.0, w - 52.0, 1.0, MpCsRedDim(), 0.8);
        MpCsText(parent, x + 26.0, y + 124.0,
                 "This slot is empty. CREATE NEW CHARACTER", 19, n"Regular", MpCsInkDim());
        MpCsText(parent, x + 26.0, y + 150.0,
                 "runs the creator and fills it.", 19, n"Regular", MpCsInkDim());
        return;
    }

    let u = Cast<Uint32>(index);
    let name = network.GetRosterName(u);
    let shown = NotEquals(name, "") ? name : "unnamed";

    MpCsText(parent, x + 26.0, y + 34.0, shown, 40, n"Bold", MpCsGold());

    // Level as a filled chip - the mockup's inverted badge, dark ink on gold.
    MpCsRect(parent, x + w - 130.0, y + 44.0, 104.0, 30.0, MpCsGold(), 1.0);
    MpCsText(parent, x + w - 116.0, y + 49.0, s"LEVEL \(network.GetRosterLevel(u))", 15,
             n"Medium", new HDRColor(0.1, 0.08, 0.0, 1.0));

    MpCsRect(parent, x + 26.0, y + 96.0, w - 52.0, 1.0, MpCsRedDim(), 0.8);

    // Key/value rows. Every one of these is a fact the server sent, which is the point of
    // the panel: it is the roster made legible, not a description of it.
    let lifepath = network.GetRosterLifepath(u);
    let rowY = y + 122.0;

    this.MpCsRow(parent, x, rowY, w, "LIFEPATH",
                 NotEquals(lifepath, "") ? lifepath : "not recorded");
    this.MpCsRow(parent, x, rowY + 46.0, w, "SLOT", s"0\(this.m_csCursor + 1) OF 04");
    this.MpCsRow(parent, x, rowY + 92.0, w, "STATUS",
                 network.HasRosterSpawnedBefore(u) ? "played" : "never played");
    this.MpCsRow(parent, x, rowY + 138.0, w, "IN PLAY",
                 network.IsRosterActive(u) ? "yes - this is you" : "no");

    MpCsText(parent, x + 26.0, y + h - 92.0, "PLAY ENTERS THE WORLD AS THIS", 14, n"Medium",
             MpCsGold());
    MpCsText(parent, x + 26.0, y + h - 62.0, "CHARACTER.", 14, n"Medium", MpCsGold());
}

@addMethod(SingleplayerMenuGameController)
public func MpCsRow(parent: ref<inkCanvas>, x: Float, y: Float, w: Float, key: String,
                    value: String) -> Void {
    MpCsText(parent, x + 26.0, y, key, 13, n"Regular", MpCsInkFaint());
    MpCsText(parent, x + 240.0, y - 4.0, value, 17, n"Regular", MpCsInk());
    MpCsRect(parent, x + 26.0, y + 30.0, w - 52.0, 1.0, MpCsRedDim(), 0.35);
}

// ============================================================================ status

@addMethod(SingleplayerMenuGameController)
public func MpCsStatusLine(parent: ref<inkCanvas>, x: Float, y: Float) -> Void {
    let network = GameInstance.GetNetworkWorldSystem();
    let message = "";

    if IsDefined(network) {
        let error = network.GetCharacterError();

        // A refusal outranks everything - it is the answer to the press they just made.
        if NotEquals(error, "") {
            message = error;
        } else {
            if !network.HasCharacter() {
                message = "No character yet - CREATE NEW CHARACTER makes one.";
            }
        }
    }

    this.m_csStatus = MpCsText(parent, x, y, message, 19, n"Regular", MpCsGold());
}

/**
 * Say something on the status line without rebuilding the screen.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsSay(message: String) -> Void {
    if IsDefined(this.m_csStatus) {
        this.m_csStatus.SetText(message);
    }
}

// ============================================================================ roster

/**
 * The roster index sitting in a given slot, or -1.
 *
 * Slots are not contiguous - retiring the character in slot 1 of three leaves 0 and 2
 * occupied - so every lookup goes through here rather than indexing the roster directly.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsRosterIndex(slot: Int32) -> Int32 {
    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) {
        return -1;
    }

    let i = 0u;

    while i < network.GetRosterCount() {
        if network.GetRosterSlot(i) == slot {
            return Cast<Int32>(i);
        }

        i += 1u;
    }

    return -1;
}

/**
 * The lowest UNLOCKED slot with nobody in it, or -1 when there is no room.
 *
 * Unlocked, not merely drawn: the screen shows four cards to everybody, but a public
 * account owns one of them. Creating into a locked slot would be refused by the server
 * anyway - this is what stops the client asking.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsFirstFreeSlot() -> Int32 {
    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) {
        return -1;
    }

    let unlocked = network.GetCharacterSlots();

    if unlocked > MpCsMaxSlots() {
        unlocked = MpCsMaxSlots();
    }

    let slot = 0;

    while slot < unlocked {
        if this.MpCsRosterIndex(slot) < 0 {
            return slot;
        }

        slot += 1;
    }

    return -1;
}

/**
 * The slot the server says is in play, or the first occupied one, or 0.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsActiveSlot() -> Int32 {
    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) {
        return 0;
    }

    let first = -1;
    let i = 0u;

    while i < network.GetRosterCount() {
        let slot = network.GetRosterSlot(i);

        if slot >= 0 {
            if network.IsRosterActive(i) {
                return slot;
            }

            if first < 0 {
                first = slot;
            }
        }

        i += 1u;
    }

    return first >= 0 ? first : 0;
}
