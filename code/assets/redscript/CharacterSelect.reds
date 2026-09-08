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

/*
 * BACKED OUT ON PURPOSE - do not reopen.
 *
 * ESC worked from the moment it was bound. It closed the screen and the screen came
 * straight back, because closing refreshes the menu, refreshing runs
 * PopulateMenuItemList, and that opens the selector whenever the account is connected.
 * Measured in zeldfep's log, all in the same millisecond:
 *
 *   input: back handled=false
 *   back pressed - closing the screen
 *   character screen open - 4 of 4 slot(s) unlocked
 *
 * Three builds of "ESC does not work" were ESC working perfectly and being undone one line
 * later. This flag is the difference between "not open" and "closed deliberately", which
 * the rebuild had no way to tell apart.
 *
 * Cleared by pressing CONNECT, which is the one gesture that unambiguously asks for the
 * screen back.
 */
@addField(SingleplayerMenuGameController)
let m_csDismissed: Bool;

// Second press on an already-selected empty slot is what actually starts the creator.
// Cleared whenever the caret moves, so arming one slot and clicking another never creates
// in a slot nobody was looking at.
@addField(SingleplayerMenuGameController)
let m_csCreateArmed: Bool;

// First DEL asks, second deletes. Cleared whenever the caret moves.
@addField(SingleplayerMenuGameController)
let m_csDeleteArmed: Bool;

/**
 * THE MENU GOES AWAY WHILE THE SELECTOR IS UP.
 *
 * This is what turns an overlay into a screen, and after 2026-09-08 it is a CORRECTNESS fix
 * rather than a cosmetic one. zeldfep on test.30: "3rd slot is broken its allowing me to hit
 * the buttons behind the overaly" - PLAY, CREATE NEW CHARACTER and DELETE sit directly under
 * the cards, so a click that misses a hit rect lands on a menu item nobody aimed at. Hiding
 * the list removes the whole class of that.
 *
 * It is also the ONLY lever available. A real screen would be its own menu SCENARIO, the way
 * Settings is - but the menu NAME a scenario opens is engine-side registration with no
 * moddable resource anywhere in the archives, so a new scenario would have nothing to open.
 *
 * RESTORE IS UNCONDITIONAL AND HAPPENS FIRST. PopulateMenuItemList calls Show before it
 * decides anything, so every path through the menu puts the list back whether or not the
 * selector was open and whether or not Close ran. A hidden list with no way to un-hide it is
 * a main menu nobody can use - a worse bug than the one being fixed.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsHideMenuList() -> Void {
    if !IsDefined(this.m_menuListController) {
        return;
    }

    let root = this.m_menuListController.GetRootWidget();

    if IsDefined(root) {
        root.SetVisible(false);
    }
}

@addMethod(SingleplayerMenuGameController)
public func MpCsShowMenuList() -> Void {
    if !IsDefined(this.m_menuListController) {
        return;
    }

    let root = this.m_menuListController.GetRootWidget();

    if IsDefined(root) {
        root.SetVisible(true);
    }
}

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

        /*
         * TOP-LEFT ANCHOR AND PIVOT, because this canvas gets SCALED and scaling happens
         * around the pivot.
         *
         * This was inkEAnchor.Fill, and with the x2 scale below that threw the whole
         * composition up and to the left off the screen: a Fill widget pivots at its
         * centre, so doubling it grows in every direction instead of down and right.
         * zeldfep's screenshot showed the result exactly - a black screen with the BOTTOM
         * edge of the dossier panel stranded at the very top.
         *
         * Anchored top-left with an anchor point of (0,0), the scale grows the way the
         * coordinates were authored: origin stays put, everything extends right and down.
         */
        canvas.SetAnchor(inkEAnchor.TopLeft);
        canvas.SetAnchorPoint(new Vector2(0.0, 0.0));
        canvas.SetMargin(new inkMargin(0.0, 0.0, 0.0, 0.0));

        /*
         * NOT INTERACTIVE. This is the soft-lock that shipped in test.27.
         *
         * A full-screen interactive canvas swallows every click and key press, and there is
         * nothing behind it that can still be reached - so the menu became unusable with no
         * way out but killing the game. Interactivity belongs on the CARD HIT RECTS and
         * nowhere else; each of those sets it for itself in MpCsArm.
         *
         * DO NOT set this true. If something on this screen needs input, give that widget
         * its own hit rect rather than arming the whole canvas.
         */
        canvas.SetInteractive(false);
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
    // Sized rather than Fill-anchored, for the same reason the canvas is: inside a scaled
    // canvas a Fill child covers the canvas's own box, which the scale has already moved.
    // 1920x1080 in authored units is exactly the screen once the scale is applied.
    MpCsRect(c, 0.0, 0.0, 1920.0, 1080.0, MpCsVoid(), 1.0);

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
    this.MpCsActions(c, 68.0, 910.0);
    this.MpCsEnterButton(c, 1332.0, 894.0);
    this.MpCsStatusLine(c, 68.0, 984.0);

    // The keys, on screen. A screen driven by keys nobody is told about is a screen that
    // does not work, and this one hid its own exit for a whole build.
    MpCsText(c, 68.0, 1030.0, "CLICK A SLOT TO SELECT      CLICK IT AGAIN TO ENTER OR CREATE", 15,
             n"Medium", MpCsGold());

    /*
     * THE MENU GOES AWAY. zeldfep, 2026-09-08: "still acting as an overlay the buttons
     * behind this screen should not be able to be touched".
     *
     * Safe to do now in a way it was not an hour ago: ENTER is CONFIRMED to arrive and to
     * work, measured in his own log - "input: activate" followed by "entering the city".
     * So the screen always has a way off it even though ESC is still unidentified. That was
     * the missing piece every previous time this was hidden and became a trap.
     */
    this.MpCsHideMenuList();

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
     * THE PROBE IS GONE. It cost three builds and never answered.
     *
     * It asked whether a CLI-authored .inkwidget can be spawned, and produced silence every
     * time: test.26 had no branch for a callback that never fires, test.27 and .28 added a
     * control and a 3-second deadline that writes a verdict unconditionally - and THAT never
     * fired either. Three instruments, three silences, and somewhere in there it locked the
     * main menu.
     *
     * The question is still worth answering, but not here. A diagnostic does not get to sit
     * in the screen a person is trying to use; it belongs somewhere a failure costs nothing.
     *
     * WHAT IT DID ESTABLISH, and this was worth the trip: the menu root lays out at
     * 3840x2160, measured rather than assumed, which is the whole reason this composition
     * scales by two.
     */
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

    this.MpCsShowMenuList();
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
        /*
         * AN EMPTY SLOT ASKS FOR A CHARACTER. zeldfep, 2026-09-08: "the empty slots should
         * prompt add new character".
         *
         * Three states rather than two, and the third is a deliberate speed bump. Creation
         * runs the game's whole character creator and leaves the menu, so a single stray
         * click should not start it - the same reasoning as the two-press DELETE, and the
         * same reasoning that just saved a character when clicks were falling through to
         * the trash can.
         *
         *   not selected  ->  + ADD NEW CHARACTER
         *   selected      ->  CLICK AGAIN TO CREATE
         *   armed         ->  the next click runs the creator
         */
        let prompt = "+  ADD NEW CHARACTER";

        if selected {
            prompt = this.m_csCreateArmed ? "CREATING..." : "CLICK AGAIN TO CREATE";
        }

        MpCsText(parent, cx + 128.0, y + 62.0, prompt, 13, n"Regular",
                 selected ? MpCsGold() : MpCsInkFaint());

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
    if selected && this.m_csDeleteArmed {
        MpCsText(parent, cx + 128.0, y + 58.0, "ARE YOU SURE?  [ DEL ] AGAIN TO DELETE", 14,
                 n"Bold", MpCsRed());
    }

    let level = network.GetRosterLevel(Cast<Uint32>(index));

    MpCsText(parent, cx + w - 84.0, y + 32.0, s"LV \(level)", 26, n"Regular",
             selected ? MpCsGold() : MpCsInkDim());

}

/**
 * Make a card pressable.
 *
 * A transparent interactive rectangle laid over the whole card, named for its slot. The
 * name is how the callback knows which card was hit - ink hands the handler the widget it
 * landed on, and reading a name off it is far less fragile than keeping a parallel array
 * of references in step with a roster that changes shape.
 */
/*
 * BACK IN USE as of test.31. test.30 answered the question it was parked for: with nothing
 * of ours interactive the menu was usable, so the lock WAS ours - and since the probe was
 * deleted in the same build, the hit rects are no longer the suspect they were.
 *
 * They come back with the menu list hidden underneath them, which is the part that was
 * missing. zeldfep on test.30: "3rd slot is broken its allowing me to hit the buttons behind
 * the overaly" - with no hit rect to catch it, a click on a card fell through to whatever
 * menu item happened to sit at that spot.
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

    // Read BEFORE the caret moves: "was this already the selected slot" is the whole
    // difference between a first click and a confirming second one.
    let wasSelected = this.m_csCursor == slot;

    if !wasSelected {
        this.m_csCreateArmed = false;
    }

    this.m_csCursor = slot;

    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) || !network.IsConnected() {
        MpCsLog(s"card pressed with no connection");
        this.MpCsOpen();
        return true;
    }

    let empty = this.MpCsRosterIndex(slot) < 0;

    if empty {
        /*
         * FIRST CLICK SELECTS AND ASKS. SECOND CLICK CREATES.
         *
         * Arming is per-caret: moving to a different slot clears it, so a click on slot 2
         * followed by a click on slot 3 can never create in slot 2. Walking away from the
         * screen clears it too, because the flag lives on the controller and the controller
         * does not survive leaving the menu - the same property the DELETE arm relies on.
         */
        if wasSelected && this.m_csCreateArmed {
            this.m_csCreateArmed = false;
            MpCsLog(s"empty slot \(slot + 1) confirmed - running the creator");

            this.MpCsSay("Starting the character creator...");
            this.MpCsClose();

            // The menu's own entry does the rest: it points the account at a free slot,
            // arms the appearance capture and runs the game's New Game flow. Going through
            // it rather than round it means one creation path, not two.
            let data = new PauseMenuListItemData();
            data.eventName = n"OnMultiplayerNewCharacter";
            this.HandleMenuItemActivate(data);
            return true;
        }

        this.m_csCreateArmed = true;
        network.SelectCharacterSlot(slot);
        this.MpCsOpen();
        this.MpCsSay("Click that slot again to make a character in it.");
        return true;
    }

    this.m_csCreateArmed = false;

    network.SelectCharacterSlot(slot);
    this.MpCsOpen();
    this.MpCsSay("switching...");

    // The answer comes back as a fresh roster, so poll for it rather than assuming the
    // switch took. SelectCharacterSlot only says the request was sent.
    let poll = new MpSelectorPoll();
    poll.controller = this;
    poll.attempts = 0;
    poll.enterWhenKnown = false;

    GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(poll, 0.25, false);
    return true;
}

/**
 * The action's REAL name, not a guess at it.
 *
 * This replaces a list of fifteen candidates tested with IsAction, which was the honest
 * shape while GetActionName() had no verified path to a String - it returns an inkActionName,
 * not a CName. scc says ToString() takes it, so the guessing ends here.
 *
 * It matters because the guessing was wrong. ESC does not arrive as 'back': zeldfep pressed
 * it, the handler ran, and the only names that appeared were "activate", "click" and "?".
 * Four builds bound a key by reading CDPR's own pregame menus and assuming the same action
 * reaches a wrapped handler, and all four did nothing. One line of measurement beats another
 * reading of the source.
 */
public func MpCsActionName(e: ref<inkPointerEvent>) -> String {
    return ToString(e.GetActionName());
}

/**
 * INPUT COMES THROUGH THE GAME'S OWN HANDLER, NOT THROUGH OUR WIDGETS.
 *
 * Hand-built rectangles with SetInteractive(true) and an OnRelease callback never once
 * received a click across test.27 through test.2 - they either did nothing or took the whole
 * menu down with them. Cam's comment in MainMenu.reds called this years before I proved it:
 * "a hand-built clickable button would need its own input handling and hover states", and
 * menu ITEMS are used everywhere in this codebase precisely because they are the surface
 * that reliably works.
 *
 * OnGlobalRelease is the controller's own event (singleplayerMenu.script:870). It receives
 * actions globally - no hit-testing, no widget tree, no interactivity flags - which is why
 * it works when nothing else did. The game drives its own menu from exactly these actions.
 *
 * BACK IS THE IMPORTANT ONE. zeldfep, 2026-09-08: "need a back button for sure" - said while
 * stuck on a screen with the menu hidden behind it and no way out but killing the game.
 * Escape now closes the selector and puts the menu back, and it is handled BEFORE anything
 * else so it cannot be swallowed.
 */
@wrapMethod(SingleplayerMenuGameController)
protected cb func OnGlobalRelease(e: ref<inkPointerEvent>) -> Bool {
    if !this.m_csOpen {
        return wrappedMethod(e);
    }

    /*
     * LOG WHAT ACTUALLY ARRIVES. Three builds have now bound keys by reading CDPR's own
     * pregame menus and assuming the same actions reach us - back, activate,
     * one_click_confirm, delete_save, navigate_up/down - and ESC still does nothing.
     *
     * That is guessing. This prints the name of every action that reaches this handler, so
     * one session in the menu turns the binding from an inference into a fact. It also
     * answers the prior question: if NOTHING is logged, the wrap itself is not being called
     * and the action names were never the problem.
     *
     * Comes out once the bindings are known - it is noisy by design.
     */
    MpCsLog(s"input: \(MpCsActionName(e)) handled=\(e.IsHandled())");

    if e.IsHandled() {
        return wrappedMethod(e);
    }

    // BACK: close the screen, restore the menu, stop here. Handling the event keeps the
    // game's own OnBack from also firing and dropping the player out of the menu entirely.
    if e.IsAction(n"back") || e.IsAction(n"cancel") {
        MpCsLog(s"back pressed - closing the screen and staying closed");
        this.m_csDismissed = true;
        this.MpCsClose();
        this.MpCsShowMenuList();
        this.MpRefreshMenu();
        e.Handle();
        return true;
    }

    // UP/DOWN walk the slots and select as they go. No confirm step: moving to a character
    // IS choosing it, which is what the caret has always meant on this screen, and it saves
    // inventing a second key nobody was told about.
    /*
     * DELETE, AND IT ASKS FIRST. zeldfep: "add the delete and a are you sure you want to
     * delete prompt to it."
     *
     * Two presses, and the prompt is on the screen between them rather than in a popup - the
     * card itself says ARE YOU SURE, so the thing being destroyed is what is asking. The arm
     * clears the moment the caret moves, so arming slot 1 and stepping to slot 2 can never
     * delete slot 1. Same shape as the chat command's guard, which is the one that saved a
     * character this morning when clicks were falling through to the trash can.
     *
     * A character is hours of somebody's evening and the store retires rather than destroys,
     * but neither is a reason to delete on one press.
     */
    if e.IsAction(n"delete_save") {
        if this.MpCsRosterIndex(this.m_csCursor) < 0 {
            this.MpCsSay("Nothing in that slot to delete.");
            e.Handle();
            return true;
        }

        if this.m_csDeleteArmed {
            this.m_csDeleteArmed = false;
            MpCsLog(s"delete confirmed for slot \(this.m_csCursor + 1)");

            this.MpCsSay("Deleting...");

            let data = new PauseMenuListItemData();
            data.eventName = n"OnMultiplayerDeleteCharacter";

            // The menu's own handler arms on the first call and sends on the second, so it
            // is called twice: the confirmation already happened HERE, on the card.
            this.HandleMenuItemActivate(data);
            this.HandleMenuItemActivate(data);

            e.Handle();
            return true;
        }

        this.m_csDeleteArmed = true;
        this.MpCsOpen();
        this.MpCsSay("ARE YOU SURE? Press DEL again to delete this character.");
        e.Handle();
        return true;
    }

    // CREATE, on an empty slot only. Runs the game's whole creator and leaves the menu, so
    // it is deliberately not on a key anybody presses by accident.
    /*
     * ENTER DOES THE OBVIOUS THING FOR WHATEVER THE CARET IS ON.
     *
     * zeldfep, 2026-09-08: "enter city need to work for sure" - the mockup's big gold
     * button, and the reason the screen exists at all. A selection screen you cannot leave
     * INTO THE GAME is a menu that wastes your time.
     *
     *   a character  ->  enter the world as them
     *   an empty slot ->  run the creator and fill it
     *
     * One key for both, because from the player's side it is one intention: "this is who I
     * am playing". Which of the two happens is a property of the card, not of the key, and
     * the card says which on its face.
     */
    if e.IsAction(n"activate") || e.IsAction(n"one_click_confirm") {
        if this.MpCsRosterIndex(this.m_csCursor) < 0 {
            MpCsLog(s"create confirmed for empty slot \(this.m_csCursor + 1)");
            this.MpCsClose();

            let data = new PauseMenuListItemData();
            data.eventName = n"OnMultiplayerNewCharacter";
            this.HandleMenuItemActivate(data);

            e.Handle();
            return true;
        }

        MpCsLog(s"entering the city as the character in slot \(this.m_csCursor + 1)");
        this.MpCsSay("Entering Night City...");
        this.MpCsClose();

        // The menu's own PLAY entry, not a second route into the world. It arms the join,
        // closes the screen and loads - and going through it means there is one entry path
        // to keep correct rather than two that can drift.
        let play = new PauseMenuListItemData();
        play.eventName = n"OnMultiplayerContinue";
        this.HandleMenuItemActivate(play);

        e.Handle();
        return true;
    }

    /*
     * CLICK WALKS THE SLOTS, because click is one of the two actions MEASURED to arrive.
     *
     * zeldfep: "the 4 empty slots should all point to new character". They already do - what
     * they were missing was any way to REACH them. navigate_up/navigate_down were bound from
     * reading CDPR's pregame menus and never arrive here, so the caret could not move and
     * three of the four slots were unreachable no matter what they offered.
     *
     * His logs name exactly two actions that reach this handler: "click" and "activate".
     * Building on those two is the difference between a screen that works today and a fifth
     * guess at a key name. Click cycles, Enter acts on what the caret is on - which is a
     * complete interaction with nothing unproven in it.
     *
     * navigate_up/down stay bound underneath. They cost nothing, and if they ever do arrive
     * on some other input device the screen gets arrow keys for free.
     */
    /*
     * ONE MOUSE CLICK ARRIVES AS BOTH "click" AND "activate", measured in zeldfep's log:
     *
     *   input: click    handled=false
     *   input: activate handled=false
     *   entering the city as the character in slot 1
     *
     * So they are not two inputs to bind separately - they are one gesture reported twice,
     * and "ENTER NIGHT CITY working" was a CLICK, not the Enter key. Any design that treats
     * them as distinct (click cycles, Enter confirms) would cycle and immediately act on the
     * slot it had just moved to.
     *
     * WHERE the cursor is turns one gesture into a whole screen. GetScreenSpacePosition() is
     * on the base input event, so the click can be hit-tested against the coordinates this
     * screen was drawn from - no widget interactivity, no hit rects, none of the machinery
     * that has failed on every build since test.27. The screen already knows where it put
     * everything; it just never asked where the mouse was.
     *
     * activate is consumed without acting, so the pair cannot fire twice.
     */
    if e.IsAction(n"click") {
        let pos = e.GetScreenSpacePosition();

        // Logged so the coordinate space can be checked against where things were drawn
        // rather than assumed. Authored units are 1920x1080; if screen space is not that,
        // this line is what says so.
        MpCsLog(s"click at \(pos.X), \(pos.Y)");

        this.MpCsClickAt(pos.X, pos.Y);
        e.Handle();
        return true;
    }

    // Consumed deliberately: it is the second half of the click above, not a separate press.
    if e.IsAction(n"activate") || e.IsAction(n"one_click_confirm") {
        e.Handle();
        return true;
    }

    if e.IsAction(n"navigate_up") || e.IsAction(n"navigate_down") {
        let step = e.IsAction(n"navigate_down") ? 1 : -1;

        this.MpCsStep(step);
        e.Handle();
        return true;
    }

    return wrappedMethod(e);
}

/**
 * Move the caret by one, wrapping, and tell the server where it landed.
 *
 * Only over slots this account may use - a caret that stops on a locked slot would be
 * offering something that cannot be picked.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsStep(delta: Int32) -> Void {
    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) {
        return;
    }

    let unlocked = network.GetCharacterSlots();

    if unlocked > MpCsMaxSlots() {
        unlocked = MpCsMaxSlots();
    }

    if unlocked < 1 {
        return;
    }

    let next = this.m_csCursor + delta;

    if next < 0 {
        next = unlocked - 1;
    }

    if next >= unlocked {
        next = 0;
    }

    this.m_csCursor = next;
    this.m_csCreateArmed = false;
    this.m_csDeleteArmed = false;

    MpCsLog(s"caret moved to slot \(next + 1)");

    // Only ask the server for slots that hold somebody. Selecting an empty one is how
    // creation is aimed, and that should be a deliberate press rather than a side effect of
    // scrolling past it.
    if this.MpCsRosterIndex(next) >= 0 {
        network.SelectCharacterSlot(next);
    }

    this.MpCsOpen();
}

/**
 * THE MOCKUP'S THREE ACTIONS, at its own coordinates (left 68, bottom 112).
 *
 * zeldfep, 2026-09-08: "select and create new and delete from mock up add them we can add
 * the fatures later."
 *
 * Drawn as the mockup draws them - clipped-corner plates, uppercase, letter-spaced - with
 * one addition it did not need: THE KEY IS ON THE BUTTON. Hand-built widgets cannot be
 * clicked on this controller (proven across six builds), so a button with no key printed on
 * it is decoration that lies about what it does. The label says what it does; the bracket
 * says how.
 *
 * Enabled state follows what is actually possible right now, so a button never offers
 * something the server would refuse: DELETE greys out on an empty slot, CREATE greys out on
 * an occupied one.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsActions(parent: ref<inkCanvas>, x: Float, y: Float) -> Void {
    let occupied = this.MpCsRosterIndex(this.m_csCursor) >= 0;

    this.MpCsActionButton(parent, x, y, 236.0, "SELECT", "CLICK", true);
    this.MpCsActionButton(parent, x + 252.0, y, 300.0, "CREATE NEW", "ENTER", !occupied);
    this.MpCsActionButton(parent, x + 568.0, y, 236.0, "DELETE", "DEL", occupied);
}

@addMethod(SingleplayerMenuGameController)
public func MpCsActionButton(parent: ref<inkCanvas>, x: Float, y: Float, w: Float,
                             label: String, key: String, enabled: Bool) -> Void {
    let h = 58.0;
    let edge = enabled ? MpCsRed() : MpCsRedDim();
    let text = enabled ? MpCsInk() : MpCsInkFaint();

    MpCsRect(parent, x, y, w, h, MpCsPlate(), enabled ? 0.72 : 0.4);
    MpCsBorder(parent, x, y, w, h, edge, enabled ? 0.9 : 0.35);
    MpCsNotch(parent, x + w, y, 18.0, MpCsVoid());
    MpCsNotch(parent, x, y + h, 18.0, MpCsVoid());

    MpCsText(parent, x + 18.0, y + 10.0, label, 19, n"Bold", text);
    MpCsText(parent, x + 18.0, y + 34.0, s"[ \(key) ]", 12, n"Regular",
             enabled ? MpCsGold() : MpCsInkFaint());
}

/**
 * ENTER NIGHT CITY - the mockup's one filled button, bottom right.
 *
 * The only solid gold thing on the screen, because it is the only action that ends it. Every
 * other control is an outline; this one is filled, which is the design language's way of
 * saying "this is the thing you came here to press".
 *
 * It reads what the caret is on, so it never offers to enter the world as nobody: an empty
 * slot turns it into CREATE, which is what ENTER actually does there.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsEnterButton(parent: ref<inkCanvas>, x: Float, y: Float) -> Void {
    let occupied = this.MpCsRosterIndex(this.m_csCursor) >= 0;
    let w = 520.0;
    let h = 92.0;

    MpCsRect(parent, x, y, w, h, MpCsGold(), occupied ? 1.0 : 0.28);
    MpCsNotch(parent, x + w, y, 26.0, MpCsVoid());
    MpCsNotch(parent, x, y + h, 26.0, MpCsVoid());

    let ink = occupied ? new HDRColor(0.1, 0.08, 0.0, 1.0) : MpCsInkFaint();

    MpCsText(parent, x + 34.0, y + 16.0, occupied ? "ENTER NIGHT CITY" : "CREATE CHARACTER",
             34, n"Bold", ink);
    MpCsText(parent, x + 34.0, y + 62.0, "[ ENTER ]", 14, n"Medium", ink);
}

/**
 * Turns a cursor position into an action, by asking where the screen drew things.
 *
 * The composition is authored in 1920x1080 units and the canvas is scaled to the root, so a
 * screen-space pixel maps straight onto an authored coordinate on a 1080p display. The
 * logged click position is what confirms that on any other resolution.
 *
 * Hit regions, in the order they are tested:
 *
 *   the four cards       select that slot; a second click on the SAME card acts on it
 *   ENTER NIGHT CITY     act on whatever the caret is on
 *
 * Acting means the obvious thing for the slot: enter the world as a character, or run the
 * creator on an empty one. That is why all four empty slots "point to new character" - each
 * one is a create target in its own right, which zeldfep asked for and which was already
 * true in the drawing and simply unreachable.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsClickAt(x: Float, y: Float) -> Void {
    // The ENTER button first - it overlaps nothing and is the most consequential.
    if x >= 1332.0 && x <= 1852.0 && y >= 894.0 && y <= 986.0 {
        this.MpCsAct();
        return;
    }

    let slot = 0;

    while slot < MpCsMaxSlots() {
        let top = 322.0 + Cast<Float>(slot) * 99.0;

        // The selected card sits 14px right; accept from the un-offset edge so the hit area
        // does not move under the cursor when the caret lands on it.
        if x >= 68.0 && x <= 650.0 && y >= top && y <= top + 90.0 {
            if this.m_csCursor == slot {
                this.MpCsAct();
            } else {
                this.MpCsSelect(slot);
            }

            return;
        }

        slot += 1;
    }
}

/**
 * Do the obvious thing for the slot the caret is on.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsAct() -> Void {
    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) || !network.IsConnected() {
        return;
    }

    if this.MpCsRosterIndex(this.m_csCursor) < 0 {
        MpCsLog(s"create in empty slot \(this.m_csCursor + 1)");
        this.MpCsClose();

        let data = new PauseMenuListItemData();
        data.eventName = n"OnMultiplayerNewCharacter";
        this.HandleMenuItemActivate(data);
        return;
    }

    MpCsLog(s"entering the city as the character in slot \(this.m_csCursor + 1)");
    this.MpCsSay("Entering Night City...");
    this.MpCsClose();

    let play = new PauseMenuListItemData();
    play.eventName = n"OnMultiplayerContinue";
    this.HandleMenuItemActivate(play);
}

/**
 * Move the caret to a specific slot, refusing locked ones.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsSelect(slot: Int32) -> Void {
    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) {
        return;
    }

    let unlocked = network.GetCharacterSlots();

    if unlocked > MpCsMaxSlots() {
        unlocked = MpCsMaxSlots();
    }

    if slot < 0 || slot >= unlocked {
        this.MpCsSay("That slot is locked.");
        return;
    }

    this.m_csCursor = slot;
    this.m_csCreateArmed = false;
    this.m_csDeleteArmed = false;

    MpCsLog(s"caret set to slot \(slot + 1)");

    if this.MpCsRosterIndex(slot) >= 0 {
        network.SelectCharacterSlot(slot);
    }

    this.MpCsOpen();
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
 * Where the caret is, clamped to a slot this account may actually use.
 *
 * The create path asks this to find out whether somebody has already chosen an empty slot
 * to fill. A caret sitting on a locked slot is not a choice, so it reads as "no choice".
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsCursorSlot() -> Int32 {
    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) {
        return -1;
    }

    let unlocked = network.GetCharacterSlots();

    if unlocked > MpCsMaxSlots() {
        unlocked = MpCsMaxSlots();
    }

    if this.m_csCursor < 0 || this.m_csCursor >= unlocked {
        return -1;
    }

    return this.m_csCursor;
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
