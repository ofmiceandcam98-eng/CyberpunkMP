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
// Hazard yellow, and it has to stay READABLE, not merely correct.
//
// DESIGN-LANGUAGE's token is #f3c50f = (0.95, 0.77, 0.06). This was (2.0, 1.68, 0.24) -
// more than DOUBLE the token on two channels. That is emissive bloom, and it looked fine
// against the launcher's --ground #0d0e11 while being illegible over this screen's bright
// city backdrop. zeldfep, 2026-09-09: "the text fields are illegible for yellow text make
// it a bit darker". Now just above the token, so it still reads as emissive without
// blowing out. A token is where you START - it still has to be read against what is
// actually behind it.
public func MpCsGold() -> HDRColor = new HDRColor(1.12, 0.92, 0.13, 1.0)
public func MpCsGoldDim() -> HDRColor = new HDRColor(0.49, 0.42, 0.11, 1.0)
// The launcher's reds (#f73932, #79221b), which is what design D was signed off in.
public func MpCsRed() -> HDRColor = new HDRColor(0.97, 0.22, 0.20, 1.0)
public func MpCsRedDim() -> HDRColor = new HDRColor(0.47, 0.13, 0.11, 1.0)
public func MpCsInk() -> HDRColor = new HDRColor(0.95, 0.96, 0.97, 1.0)
public func MpCsInkDim() -> HDRColor = new HDRColor(0.59, 0.63, 0.67, 1.0)
public func MpCsInkFaint() -> HDRColor = new HDRColor(0.37, 0.42, 0.46, 1.0)
public func MpCsPlate() -> HDRColor = new HDRColor(0.04, 0.03, 0.055, 1.0)
// The plate under something gold and chosen: the selected card, a locked SELECT.
public func MpCsGoldPlate() -> HDRColor = new HDRColor(0.09, 0.07, 0.016, 1.0)
public func MpCsVoid() -> HDRColor = new HDRColor(0.016, 0.016, 0.031, 1.0)
// Anything printed ON the gold plates.
public func MpCsOnGold() -> HDRColor = new HDRColor(0.1, 0.08, 0.0, 1.0)

// The font the game itself uses. Borrowing it rather than shipping one is why this screen
// reads as part of Cyberpunk instead of as an overlay sitting on top of it.
public func MpCsFont() -> String = "base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily"

// The render's wide face, for the title, the dossier name and the go plate. It ships with the
// game (found in the archive file tables, styles Regular, Medium, Bold, Black).
public func MpCsFontWide() -> String = "base\\gameplay\\gui\\fonts\\orbitron\\orbitron.inkfontfamily"

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
 * LETTER-SPACING, BY HAND, because ink does not have it.
 *
 * SetLetterSpacing does not exist on inkText - probed, it does not resolve. The mockup leans
 * on it heavily: .42em on the eyebrow, .34em on the subtitle, .16em on every button. At
 * these sizes that effect IS a space between characters, so inserting one is not a hack
 * standing in for the real thing; it is the same result by the only available route.
 *
 * Only for SHORT labels. Every character becomes two, so a long string doubles in width and
 * wraps - which is why names, lifepaths and anything server-sent never go through here.
 */
public func MpCsSpaced(text: String) -> String {
    let out = "";
    let i = 0;
    let n = StrLen(text);

    while i < n {
        out += StrMid(text, i, 1);

        if i < n - 1 {
            out += " ";
        }

        i += 1;
    }

    return out;
}

/**
 * Text with a coloured ghost behind it, which is how the mockup's title reads as lit.
 *
 * The CSS is a red glow plus a red and a cyan offset - chromatic aberration, the signature
 * the whole design language is built on. ink has no text shadow, so the same text is drawn
 * three times: cyan left, red right, the real one on top.
 */
public func MpCsGlowText(parent: ref<inkCompoundWidget>, x: Float, y: Float, text: String,
                         size: Int32, style: CName, colour: HDRColor) -> Void {
    MpCsText(parent, x - 3.0, y, text, size, style, new HDRColor(0.47, 0.86, 1.0, 1.0));
    MpCsText(parent, x + 3.0, y, text, size, style, MpCsRed());
    MpCsText(parent, x, y, text, size, style, colour);
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

// ============================================================================ design D's kit
//
// zeldfep, 2026-11-09: "keep the old backdrop and the polygon in drawing in the middle ... keep
// everything else". Design D is the original render's panels around today's backdrop and
// rings; the mock is _internal-docs/design-revamp/selector-2026-11-09.
//
// Everything below is still rectangles and text, the two things proven on this screen. The
// cut corners and the lean do not need a drawn texture or a new widget type.

/**
 * Text in the wide face.
 */
public func MpCsTextWide(parent: ref<inkCompoundWidget>, x: Float, y: Float, text: String,
                         size: Int32, style: CName, color: HDRColor) -> ref<inkText> {
    let t = MpCsText(parent, x, y, text, size, style, color);

    t.SetFontFamily(MpCsFontWide());
    t.SetFontStyle(style);

    return t;
}

/**
 * Hang text from a fraction of its own size, so x/y become its centre (0.5) or right edge (1.0).
 */
public func MpCsAlign(t: ref<inkText>, fractionX: Float, fractionY: Float) -> Void {
    t.SetFitToContent(true);
    t.SetAnchorPoint(new Vector2(fractionX, fractionY));
}

/**
 * Shorten to a character budget. Measuring needs a finished layout and this screen is rebuilt
 * whole, so each caller states how many of its widest letters fit.
 */
public func MpCsClip(text: String, budget: Int32) -> String {
    if StrLen(text) <= budget {
        return text;
    }

    return StrLeft(text, budget - 2) + "...";
}

/**
 * One straight line at any angle: a thin rectangle, rotated about its middle.
 */
public func MpCsLine(parent: ref<inkCompoundWidget>, x1: Float, y1: Float, x2: Float, y2: Float,
                     thickness: Float, color: HDRColor, opacity: Float) -> Void {
    let dx = x2 - x1;
    let dy = y2 - y1;
    let l = new inkRectangle();

    l.SetAnchor(inkEAnchor.TopLeft);
    l.SetAnchorPoint(new Vector2(0.5, 0.5));
    l.SetMargin(new inkMargin((x1 + x2) / 2.0, (y1 + y2) / 2.0, 0.0, 0.0));
    l.SetSize(new Vector2(SqrtF(dx * dx + dy * dy), thickness));
    l.SetTintColor(color);
    l.SetOpacity(opacity);
    l.SetRotation(Rad2Deg(AtanF(dy, dx)));
    l.Reparent(parent);
}

/**
 * How far a row leans, in degrees. Measured off the render: every row points at a horizon at
 * y 586, so the side panels read as curved screens. side is +1 on the left, -1 on the right.
 */
public func MpCsTilt(yc: Float, side: Float) -> Float = (586.0 - yc) * 0.00473 * side

/**
 * A leaning row. Draw into what this returns using ordinary stage coordinates.
 *
 * ROTATION, NOT SHEAR. At two degrees they are the same picture, and rotation is proven on
 * this screen by the rings.
 *
 * The outer canvas sits on the pivot and turns about its own corner, sized the same way the
 * screen's root canvas is (left alone). The inner canvas moves the origin back to the stage's.
 */
public func MpCsFan(parent: ref<inkCompoundWidget>, px: Float, py: Float, degrees: Float) -> ref<inkCanvas> {
    let pivot = new inkCanvas();

    pivot.SetAnchor(inkEAnchor.TopLeft);
    pivot.SetAnchorPoint(new Vector2(0.0, 0.0));
    pivot.SetMargin(new inkMargin(px, py, 0.0, 0.0));
    pivot.SetRenderTransformPivot(new Vector2(0.0, 0.0));
    pivot.SetRotation(degrees);
    pivot.SetInteractive(false);
    pivot.Reparent(parent);

    let stage = new inkCanvas();

    stage.SetAnchor(inkEAnchor.TopLeft);
    stage.SetAnchorPoint(new Vector2(0.0, 0.0));
    stage.SetMargin(new inkMargin(-px, -py, 0.0, 0.0));
    stage.SetInteractive(false);
    stage.Reparent(pivot);

    return stage;
}

public func MpCsFanL(parent: ref<inkCompoundWidget>, yc: Float) -> ref<inkCanvas> =
    MpCsFan(parent, 60.0, yc, MpCsTilt(yc, 1.0))

public func MpCsFanR(parent: ref<inkCompoundWidget>, yc: Float) -> ref<inkCanvas> =
    MpCsFan(parent, 1868.0, yc, MpCsTilt(yc, -1.0))

/**
 * The polygon's top (upper=true) or bottom edge at x. The shapes here are all convex.
 */
public func MpCsPolyY(xs: array<Float>, ys: array<Float>, x: Float, upper: Bool) -> Float {
    let n = ArraySize(xs);
    let i = 0;
    let found = false;
    let best = 0.0;

    while i < n {
        let j = (i + 1) % n;
        let low = MinF(xs[i], xs[j]);
        let high = MaxF(xs[i], xs[j]);

        if x >= low && x <= high && high - low > 0.0001 {
            let y = ys[i] + (ys[j] - ys[i]) * (x - xs[i]) / (xs[j] - xs[i]);

            if !found || (upper && y < best) || (!upper && y > best) {
                best = y;
                found = true;
            }
        }

        i += 1;
    }

    return best;
}

/**
 * The polygon's corner x positions, sorted, without repeats.
 */
public func MpCsPolyStops(xs: array<Float>) -> array<Float> {
    let out: array<Float>;
    let i = 0;

    while i < ArraySize(xs) {
        let at = 0;
        let repeat = false;

        while at < ArraySize(out) && out[at] <= xs[i] + 0.001 {
            if AbsF(out[at] - xs[i]) <= 0.001 {
                repeat = true;
            }

            at += 1;
        }

        if !repeat {
            ArrayInsert(out, at, xs[i]);
        }

        i += 1;
    }

    return out;
}

/**
 * A filled convex polygon, out of rectangles.
 *
 * Between each pair of corners: one large rectangle for everything flat, and narrow strips
 * only along the slanted edges, each stepping at most two units. The strips hide under the
 * outline, and any seam between them stays in the sliver along the edge instead of running
 * across a plate.
 */
public func MpCsPolyFill(parent: ref<inkCompoundWidget>, xs: array<Float>, ys: array<Float>,
                         color: HDRColor, opacity: Float) -> Void {
    let stops = MpCsPolyStops(xs);
    let s = 0;

    while s < ArraySize(stops) - 1 {
        let a = stops[s];
        let b = stops[s + 1];
        let topA = MpCsPolyY(xs, ys, a + 0.001, true);
        let topB = MpCsPolyY(xs, ys, b - 0.001, true);
        let lowA = MpCsPolyY(xs, ys, a + 0.001, false);
        let lowB = MpCsPolyY(xs, ys, b - 0.001, false);
        let coreTop = MaxF(topA, topB);
        let coreLow = MinF(lowA, lowB);

        if coreLow > coreTop {
            MpCsRect(parent, a, coreTop, b - a, coreLow - coreTop, color, opacity);
        }

        let rise = MaxF(AbsF(topB - topA), AbsF(lowB - lowA));

        if rise > 0.5 {
            let count = CeilF(rise / 2.0);
            let width = (b - a) / Cast<Float>(count);
            let i = 0;

            while i < count {
                let left = a + Cast<Float>(i) * width;
                let middle = left + width / 2.0;
                let top = MpCsPolyY(xs, ys, middle, true);
                let low = MpCsPolyY(xs, ys, middle, false);

                if coreTop - top > 0.1 {
                    MpCsRect(parent, left, top, width + 0.25, coreTop - top, color, opacity);
                }

                if low - coreLow > 0.1 {
                    MpCsRect(parent, left, coreLow, width + 0.25, low - coreLow, color, opacity);
                }

                i += 1;
            }
        }

        s += 1;
    }
}

/**
 * The polygon's outline.
 */
public func MpCsPolyEdge(parent: ref<inkCompoundWidget>, xs: array<Float>, ys: array<Float>,
                         thickness: Float, color: HDRColor, opacity: Float) -> Void {
    let n = ArraySize(xs);
    let i = 0;

    while i < n {
        let j = (i + 1) % n;

        MpCsLine(parent, xs[i], ys[i], xs[j], ys[j], thickness, color, opacity);
        i += 1;
    }
}

public func MpCsCutXs(x: Float, w: Float, c: Float) -> array<Float> = [x, x + w - c, x + w, x + w, x + c, x]

public func MpCsCutYs(y: Float, h: Float, c: Float) -> array<Float> = [y, y, y + c, y + h, y + h, y + h - c]

/**
 * A character card's outline, grown by d for the selected card's halo.
 */
public func MpCsCardXs(x: Float, w: Float, d: Float) -> array<Float> =
    [x - d, x + w - 20.0 + d, x + w + d, x + w + d, x + w - 10.0 + d, x + 12.0 - d, x - d]

public func MpCsCardYs(y: Float, h: Float, d: Float) -> array<Float> =
    [y - d, y - d, y + 20.0 - d, y + h - 10.0 + d, y + h + d, y + h + d, y + h - 12.0 + d]

public func MpCsLifepathPart(lifepath: String, big: Bool) -> CName {
    let key = StrUpper(lifepath);

    if StrContains(key, "NOMAD") {
        return big ? n"LifepathNomad_Big" : n"LifepathNomad1";
    }

    if StrContains(key, "STREET") {
        return big ? n"LifepathStreetKid_Big" : n"LifepathStreetKid1";
    }

    if StrContains(key, "CORP") {
        return big ? n"LifepathCorpo_Big" : n"LifepathCorpo1";
    }

    return n"";
}

/**
 * The game's own lifepath emblem, from the save screen's atlas. Parts read from the atlas.
 */
public func MpCsLifepathIcon(parent: ref<inkCompoundWidget>, x: Float, y: Float, w: Float, h: Float,
                             lifepath: String, big: Bool, color: HDRColor, opacity: Float) -> Void {
    let part = MpCsLifepathPart(lifepath, big);

    if Equals(part, n"") {
        return;
    }

    let i = new inkImage();

    i.SetAnchor(inkEAnchor.TopLeft);
    i.SetAnchorPoint(new Vector2(0.0, 0.0));
    i.SetMargin(new inkMargin(x, y, 0.0, 0.0));
    i.SetSize(new Vector2(w, h));
    i.SetAtlasResource(r"base\\gameplay\\gui\\fullscreen\\load_game\\save_game.inkatlas");
    i.SetTexturePart(part);
    i.SetTintColor(color);
    i.SetOpacity(opacity);
    i.Reparent(parent);
}

/**
 * The render's two lines of voice and four of graffiti, per lifepath. The nomad's are the
 * render's own; the street kid's and the corporate's were written for the mock.
 */
public func MpCsLifepathLines(lifepath: String) -> array<String> {
    let key = StrUpper(lifepath);

    if StrContains(key, "NOMAD") {
        return ["\"ROADS TEACH MORE", "THAN PEOPLE.\"", "SOME", "BORDERS", "ARE", "A SUGGESTION."];
    }

    if StrContains(key, "STREET") {
        return ["\"THE STREET KEEPS", "THE RECEIPTS.\"", "EVERY", "BLOCK", "OWES", "SOMEBODY."];
    }

    if StrContains(key, "CORP") {
        return ["\"READ THE CONTRACT.", "THEN READ IT AGAIN.\"", "LOYALTY", "IS A", "LINE", "ITEM."];
    }

    let none: array<String>;

    return none;
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

// GetWindowSpacePosition() returned 0,0 on every single click - measured, not assumed - so
// screen space is the only reading this event actually carries. Recorded here because the
// next person to look for a second coordinate source should not have to find that out again.

// Second press on an already-selected empty slot is what actually starts the creator.
// Cleared whenever the caret moves, so arming one slot and clicking another never creates
// in a slot nobody was looking at.
@addField(SingleplayerMenuGameController)
let m_csCreateArmed: Bool;

// First DEL asks, second deletes. Cleared whenever the caret moves.
@addField(SingleplayerMenuGameController)
let m_csDeleteArmed: Bool;

// SELECT's padlock. While it is on, the caret stays put: the cards, the arrow keys and DELETE
// all refuse, and ENTER CITY plays the character it is on. MpCsClose drops it, so it lasts as
// long as the screen is open. Read it through MpCsIsLocked, which also asks the roster.
@addField(SingleplayerMenuGameController)
let m_csLocked: Bool;

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

    // DO NOT UNDO (zeldfep, 2026-09-14: "stop breaking this specific part we've looped like 5
    // times"). First open of this selector session (m_csOpen is still false here - it is set
    // true near the end): tell the server we left the world so it releases our puppet. Without
    // this the server keeps the puppet alive and refuses every select/delete with "leave the
    // world first", and re-selecting respawns the template (Phantom Veronica) instead of our
    // pick. This one call is what makes switch, delete AND appearance work; the server side is
    // HandleLeaveWorldRequest (see its DO-NOT-UNDO banner in ChatSystem.cpp).
    if !this.m_csOpen {
        network.LeaveWorld();
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
         * way out but killing the game.
         *
         * DO NOT set this true. The rule outlived the mechanism it was written for: the
         * card hit rects it used to name were removed in f0e8556 in favour of
         * OnGlobalRelease - the controller's own event - with MpCsClickAt resolving the
         * click coordinates to a card. Nothing on this screen is interactive now, and
         * nothing needs to be, which is exactly why the screen works.
         *
         * If something here ever does need input, give THAT widget its own hit rect.
         * Never arm the whole canvas.
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

    this.MpCsScreenEdge(c);

    // ---------------------------------------------------------------- title
    let lead = MpCsFan(c, 60.0, 30.0, 1.6);

    MpCsText(lead, 66.0, 20.0, "NEXUS OS v2.4.1", 13, n"Medium", MpCsInkDim());
    MpCsText(lead, 66.0, 36.0, "USER://PROFILE_MANAGER", 13, n"Medium", MpCsInkDim());

    let title = MpCsFan(c, 60.0, 85.0, 1.7);

    MpCsTextWide(title, 66.0, 56.0, "CHARACTER SELECTION", 44, n"Bold", MpCsRedDim());
    MpCsTextWide(title, 64.0, 56.0, "CHARACTER SELECTION", 44, n"Bold", MpCsRed());

    let strap = MpCsFan(c, 60.0, 124.0, 1.7);

    MpCsText(strap, 66.0, 114.0, MpCsSpaced("SAME CITY. DIFFERENT STORIES."), 16, n"Medium", MpCsInkDim());

    MpCsText(c, 1392.0, 30.0, "CITY ACCESS TERMINAL", 13, n"Medium", MpCsInkDim());
    MpCsText(c, 1392.0, 46.0, "SEQ.2047-9-443", 13, n"Medium", MpCsInkDim());
    MpCsText(c, 1392.0, 62.0, "AUTH: OK", 13, n"Medium", MpCsInkDim());
    MpCsText(c, 1760.0, 28.0, "BEFORE", 15, n"Medium", MpCsInkDim());
    MpCsText(c, 1760.0, 48.0, "TOMORROW", 15, n"Medium", MpCsInkDim());
    MpCsRect(c, 1760.0, 74.0, 22.0, 2.0, MpCsInkDim(), 0.6);

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
        this.MpCsCard(c, slot, slot < unlocked);
        slot += 1;
    }

    // ---------------------------------------------------------------- detail
    this.MpCsScanRings(c);
    this.MpCsDetail(c);

    // ---------------------------------------------------------------- actions
    //
    // The verbs, drawn where the render has them. These are LABELS: a press lands through
    // MpCsClickAt, which knows where each one was drawn.
    this.MpCsActions(c);
    this.MpCsEnterButton(c);
    this.MpCsStatusLine(c, 62.0, 932.0);

    // The keys, on screen. A screen driven by keys nobody is told about is a screen that
    // does not work, and this one hid its own exit for a whole build. Not while a refusal is
    // up: that plate sits here and it matters more.
    if Equals(network.GetCharacterError(), "") {
        let hint = MpCsFan(c, 60.0, 968.0, -1.3);

        MpCsText(hint, 62.0, 960.0, MpCsSpaced(this.MpCsIsLocked() ? "LOCKED IN - PRESS ENTER CITY" : "CLICK A SLOT, THEN CONFIRM"),
                 15, n"Medium", MpCsGold());
    }

    let quip = MpCsFan(c, 60.0, 1000.0, -1.3);

    MpCsText(quip, 58.0, 986.0, "IDENTITY IS A TOOL.", 15, n"Medium", MpCsInkFaint());
    MpCsText(quip, 58.0, 1006.0, "MAKE IT YOURS.", 15, n"Medium", MpCsInkFaint());

    MpCsText(c, 1524.0, 1006.0, "HUMANITY // MACHINE // OPPORTUNITY", 13, n"Medium", MpCsInkDim());
    MpCsRect(c, 1806.0, 1012.0, 12.0, 3.0, MpCsRed(), 1.0);
    MpCsRect(c, 1822.0, 1012.0, 12.0, 3.0, MpCsRed(), 1.0);
    MpCsRect(c, 1838.0, 1012.0, 30.0, 2.0, MpCsRed(), 0.7);

    this.MpCsMapBuild(c);

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
    this.m_csLocked = false;
}

/**
 * The screen's own edge: hairlines that lean with the panels, and two corner ticks.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsScreenEdge(parent: ref<inkCanvas>) -> Void {
    let line = MpCsRed();
    let a = 0.4;

    MpCsLine(parent, 760.0, 14.0, 30.0, 10.0, 1.0, line, a);
    MpCsLine(parent, 30.0, 10.0, 30.0, 1046.0, 1.0, line, a);
    MpCsLine(parent, 30.0, 1046.0, 770.0, 1006.0, 1.0, line, a);

    MpCsLine(parent, 1350.0, 14.0, 1890.0, 8.0, 1.0, line, a);
    MpCsLine(parent, 1890.0, 8.0, 1890.0, 1050.0, 1.0, line, a);
    MpCsLine(parent, 1890.0, 1050.0, 1500.0, 1024.0, 1.0, line, a);

    MpCsRect(parent, 34.0, 12.0, 34.0, 2.0, line, 0.75);
    MpCsRect(parent, 34.0, 12.0, 2.0, 34.0, line, 0.75);
    MpCsRect(parent, 1852.0, 10.0, 34.0, 2.0, line, 0.75);
    MpCsRect(parent, 1884.0, 10.0, 2.0, 34.0, line, 0.75);
}

/**
 * One character card, 520 x 130 at a 158 pitch.
 *
 * Face cell, emblem cell, words. The selected card is the one piece of gold in the roster: a
 * gold outline with a halo and the SELECTED chip. Every other card carries a red bar on its
 * outer edge.
 *
 * THE FACE CELL HOLDS THE INITIAL until the picture feature exists (Atlas
 * character-select-mockup-items-never-built). It is a place on the card now, so the picture
 * has somewhere to go.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsCard(parent: ref<inkCanvas>, slot: Int32, unlocked: Bool) -> Void {
    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) {
        return;
    }

    let index = this.MpCsRosterIndex(slot);
    let occupied = index >= 0;
    let selected = unlocked && slot == this.m_csCursor;
    let x = 60.0;
    let y = 163.0 + Cast<Float>(slot) * 158.0;
    let w = 520.0;
    let h = 130.0;
    let row = MpCsFanL(parent, y + h / 2.0);
    let px = x + 14.0;
    let py = y + 7.0;
    let pw = 136.0;
    let ph = h - 14.0;
    let ex = px + pw + 6.0;
    let ew = 68.0;
    let tx = ex + ew + 18.0;
    let xs = MpCsCardXs(x, w, 0.0);
    let ys = MpCsCardYs(y, h, 0.0);

    // Locked in: the other cards have stopped answering, so they stop looking as if they would.
    if this.MpCsIsLocked() && !selected {
        row.SetOpacity(0.45);
    }

    /*
     * A LOCKED SLOT IS DRAWN, DIMMED, AND DOES NOTHING.
     *
     * Deliberately not hidden: the point of showing it is that somebody can see there are
     * three more and that they are obtainable. Deliberately not pressable either - a card
     * that highlights and then refuses is a worse answer than a card that never pretended.
     */
    if !unlocked {
        MpCsPolyFill(row, xs, ys, MpCsPlate(), 0.55);
        MpCsPolyEdge(row, xs, ys, 1.0, MpCsRed(), 0.18);
        MpCsRect(row, x, y + 4.0, 7.0, h - 16.0, MpCsRedDim(), 0.6);
        MpCsPolyEdge(row, MpCsCutXs(px, pw, 12.0), MpCsCutYs(py, ph, 12.0), 1.0, MpCsRed(), 0.16);
        MpCsPolyEdge(row, MpCsCutXs(ex, ew, 8.0), MpCsCutYs(py, ph, 8.0), 1.0, MpCsRed(), 0.14);
        MpCsText(row, tx, y + 34.0, "LOCKED", 26, n"Medium", MpCsInkFaint());
        MpCsText(row, tx, y + 72.0, "AN EXTRA CHARACTER SLOT", 17, n"Medium", MpCsInkFaint());

        return;
    }

    if selected {
        MpCsPolyEdge(row, MpCsCardXs(x, w, 4.0), MpCsCardYs(y, h, 4.0), 2.0, MpCsGold(), 0.22);
        MpCsPolyEdge(row, MpCsCardXs(x, w, 8.0), MpCsCardYs(y, h, 8.0), 3.0, MpCsGold(), 0.08);
    }

    MpCsPolyFill(row, xs, ys, selected ? MpCsGoldPlate() : MpCsPlate(), selected ? 0.88 : 0.8);
    MpCsPolyEdge(row, xs, ys, selected ? 2.5 : 1.0, selected ? MpCsGold() : MpCsRed(), selected ? 1.0 : 0.34);

    if !selected {
        MpCsRect(row, x, y + 3.0, 7.0, h - 15.0, MpCsRed(), 1.0);
        MpCsRect(row, x + w - 56.0, y + h - 16.0, 22.0, 2.0, MpCsInkDim(), 0.5);
        MpCsRect(row, x + w - 56.0, y + h - 11.0, 22.0, 2.0, MpCsInkDim(), 0.5);
        MpCsRect(row, x + w - 30.0, y + h - 16.0, 10.0, 7.0, MpCsInkDim(), 0.25);
    }

    // The face cell and the emblem cell.
    let cellXs = MpCsCutXs(px - 1.0, pw + 2.0, 12.0);
    let cellYs = MpCsCutYs(py - 1.0, ph + 2.0, 12.0);

    MpCsPolyFill(row, cellXs, cellYs, MpCsPlate(), 1.0);
    MpCsPolyEdge(row, cellXs, cellYs, 1.0, selected ? MpCsGold() : MpCsRed(), selected ? 1.0 : 0.3);
    MpCsPolyEdge(row, MpCsCutXs(ex, ew, 8.0), MpCsCutYs(py, ph, 8.0), 1.0, selected ? MpCsGold() : MpCsRed(), selected ? 0.5 : 0.22);

    if !occupied {
        MpCsPolyEdge(row, MpCsCutXs(px + 44.0, 48.0, 6.0), MpCsCutYs(py + 34.0, 48.0, 6.0), 1.5,
                     selected ? MpCsGold() : MpCsInkDim(), 1.0);

        let plus = MpCsText(row, px + 68.0, py + 58.0, "+", 36, n"Medium", selected ? MpCsGold() : MpCsInkDim());

        MpCsAlign(plus, 0.5, 0.5);

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
         *   not selected  ->  EMPTY SLOT 0N
         *   selected      ->  CLICK AGAIN TO CREATE
         *   armed         ->  the next click runs the creator
         */
        let prompt = s"EMPTY SLOT 0\(slot + 1)";

        if selected {
            prompt = this.m_csCreateArmed ? "CREATING..." : "CLICK AGAIN TO CREATE";
        }

        MpCsText(row, tx, y + 34.0, "ADD NEW CHARACTER", 24, n"Medium", selected ? MpCsGold() : MpCsInk());
        MpCsText(row, tx, y + 72.0, prompt, 17, n"Medium", selected ? MpCsGold() : MpCsInkDim());

        return;
    }

    let u = Cast<Uint32>(index);
    let name = network.GetRosterName(u);
    let shown = NotEquals(name, "") ? name : "unnamed";
    let lifepath = network.GetRosterLifepath(u);

    let initial = MpCsText(row, px + pw / 2.0, py + ph / 2.0, StrUpper(StrLeft(shown, 1)), 52, n"Bold", MpCsInkDim());

    MpCsAlign(initial, 0.5, 0.5);

    MpCsLifepathIcon(row, ex + 12.0, py + 36.0, 44.0, 44.0, lifepath, false,
                     selected ? MpCsGold() : MpCsInkDim(), 1.0);

    // The chip takes the right of the top line on the selected card, so the name gets less.
    MpCsText(row, tx, y + 20.0, StrUpper(MpCsClip(shown, selected ? 11 : 20)), 27, n"Medium",
             selected ? MpCsGold() : MpCsInk());

    if selected && this.m_csDeleteArmed {
        MpCsText(row, tx, y + 58.0, "ARE YOU SURE?", 19, n"Bold", MpCsRed());
        MpCsText(row, tx, y + 86.0, "[ DEL ] AGAIN TO DELETE", 17, n"Bold", MpCsRed());
    } else {
        // Lifepath and state: what the protocol flag day was FOR - with four characters the
        // name alone stops being enough to tell them apart.
        let meta = NotEquals(lifepath, "") ? lifepath : "no lifepath on record";

        if !network.HasRosterSpawnedBefore(u) {
            meta += "  -  NEVER PLAYED";
        }

        MpCsText(row, tx, y + 58.0, StrUpper(meta), 18, n"Medium", MpCsInkDim());
        MpCsText(row, tx, y + 86.0, s"LV. \(network.GetRosterLevel(u))", 18, n"Medium", MpCsInkDim());
    }

    if selected {
        MpCsRect(row, x + w - 124.0, y + 24.0, 104.0, 28.0, MpCsGold(), 1.0);

        // LOCKED IN, not LOCKED: a card that says LOCKED is a slot this account may not use.
        let chip = MpCsText(row, x + w - 72.0, y + 38.0, this.MpCsIsLocked() ? "LOCKED IN" : "SELECTED", 17, n"Bold", MpCsOnGold());

        MpCsAlign(chip, 0.5, 0.5);
    }
}

/**
 * Names the action by testing candidates. The clumsy version, restored, because it is the
 * one that works.
 *
 * ToString(e.GetActionName()) COMPILES AND IS USELESS - it prints the literal string
 * "[inkActionName]", the type rather than the value. scc accepting it is not the same as it
 * doing anything, which is the same lesson as everything else today: compiling is not
 * evidence, and the only proof is a line in a log from a real machine.
 *
 * The list below is no longer guesswork. Every name here has either been OBSERVED arriving
 * in zeldfep's logs - click, activate, back, cancel, close_popup - or is cheap to keep
 * alongside one that was. That is why the two useful bindings on this screen exist at all.
 */
public func MpCsActionName(e: ref<inkPointerEvent>) -> String {
    if e.IsAction(n"back") { return "back"; }
    if e.IsAction(n"cancel") { return "cancel"; }
    if e.IsAction(n"close_popup") { return "close_popup"; }
    if e.IsAction(n"click") { return "click"; }
    if e.IsAction(n"activate") { return "activate"; }
    if e.IsAction(n"one_click_confirm") { return "one_click_confirm"; }
    if e.IsAction(n"delete_save") { return "delete_save"; }
    if e.IsAction(n"navigate_up") { return "navigate_up"; }
    if e.IsAction(n"navigate_down") { return "navigate_down"; }
    if e.IsAction(n"navigate_left") { return "navigate_left"; }
    if e.IsAction(n"navigate_right") { return "navigate_right"; }
    if e.IsAction(n"mouse_move") { return "mouse_move"; }
    if e.IsAction(n"next_menu") { return "next_menu"; }
    if e.IsAction(n"child_menu") { return "child_menu"; }

    return "?";
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

    /*
     * THE CLICK IS TESTED BEFORE THE HANDLED GUARD, and that is not a style choice.
     *
     * zeldfep's logs show clicks arriving BOTH ways - "input: click handled=false" and
     * "input: click handled=true". Something upstream marks some of them handled, and with
     * the guard first those were skipped entirely: only the activate half of the gesture ran,
     * so a click anywhere entered the world and no "click at" line was ever written. That is
     * exactly the report - "the whole window acts like a button for enter the city".
     *
     * Reading a position out of an already-handled event costs nothing and changes nothing
     * for anyone else; this screen covers the whole surface while it is open, so there is no
     * other consumer whose click we could be stealing.
     */
    if e.IsAction(n"click") {
        /*
         * Screen pixels in, authored units out.
         *
         * GetWindowSpacePosition() was tried alongside this and returned 0,0 on every click -
         * measured, not assumed - so screen space is the only reading the event carries.
         */
        let pos = e.GetScreenSpacePosition();


        /*
         * Screen pixels in, authored units out. Everything downstream - the hit regions, the
         * crosshair, the draw map - is in authored units, so the conversion happens once here
         * rather than being remembered at each use.
         */
        let perUnit = this.MpCsPixelsPerUnit();
        let ax = pos.X / perUnit;
        let ay = pos.Y / perUnit;

        MpCsLog(s"click screen=\(pos.X),\(pos.Y)  authored=\(ax),\(ay)");

        // The click map sees every click first. True means it was for the tool.
        if this.MpCsMapTake(ax, ay, pos.X, pos.Y) {
            e.Handle();
            return true;
        }

        this.MpCsClickAt(ax, ay);
        this.MpCsMapRefresh();

        /*
         * EVERY click is consumed while this screen is up, not just the ones that hit
         * something.
         *
         * NOT because the menu is visible - it is not. I read CHARACTER SELECTION, CREATE
         * NEW, LOAD CHARACTER, SETTINGS and EXIT in zeldfep's screenshot and called the hide
         * broken; that blue text is PART OF THE BACKDROP RENDER, which has mock UI painted
         * into it. zeldfep: "thats part of the backround", "the blue one". The hide works.
         *
         * The guard is kept anyway, on its own merit: a click landing in a gap between cards
         * should stop at this screen rather than reaching anything underneath, whatever
         * happens to be there. That is how DELETE got armed twice this morning, back when
         * the cards had no hit regions at all.
         *
         * Left as a marker: I have now twice diagnosed a bug from a screenshot without
         * checking whether the pixels were ours. The backdrop is a photograph of a UI.
         */
        e.Handle();
        return true;
    }

    if e.IsHandled() {
        return wrappedMethod(e);
    }

    // BACK: close the screen, restore the menu, stop here. Handling the event keeps the
    // game's own OnBack from also firing and dropping the player out of the menu entirely.
    if e.IsAction(n"back") || e.IsAction(n"cancel") {
        MpCsLog(s"back pressed - closing the screen and staying closed");
        this.m_csDismissed = true;
        this.MpCsClose();

        // ONLY restore the main menu when there is no live world to stay in. The selector
        // opens on the main-menu controller, but the auto-spawn ordering bug leaves it up
        // AFTER the player is already in the world - and restoring the menu there tears the
        // session down. Measured 2026-09-10: link 1ms/100%%, back pressed, then 'saving before
        // the world is torn down' + world detach + a BadConnection drop. So while connected,
        // just close the overlay and stay put; restore the menu only from the actual menu.
        let net = GameInstance.GetNetworkWorldSystem();
        if !IsDefined(net) || !net.IsConnected() {
            this.MpCsShowMenuList();
            this.MpRefreshMenu();
        }

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
        // Kept for if delete_save ever starts arriving here; the working path is the DELETE
        // button hit region in MpCsClickAt. Both route through the same slot-explicit MpCsDelete.
        this.MpCsDelete();
        e.Handle();
        return true;
    }

    // CREATE, on an empty slot only. Runs the game's whole creator and leaves the menu, so
    // it is deliberately not on a key anybody presses by accident.
    /*
     * THE OLD "activate ENTERS THE CITY" BRANCH LIVED HERE AND IS GONE.
     *
     * zeldfep: "the whole window acts like a button for enter the city". Exactly right, and
     * this was why. A mouse click arrives as click AND activate, so once clicks were
     * hit-tested by position this branch still fired on the activate half - from anywhere on
     * the screen, ignoring where the cursor actually was.
     *
     * I added a consume-only activate branch further down when the hit-testing went in and
     * never removed this one, so the consume was unreachable and every click entered the
     * world. Position now decides everything; activate is only swallowed.
     */

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
    if this.MpCsIsLocked() {
        this.MpCsSayLocked();
        return;
    }

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
 * SELECT and DELETE, where the render has its buttons.
 *
 * zeldfep, 2026-09-08: "select and create new and delete from mock up add them we can add
 * the fatures later."
 *
 * Drawn as the render draws them, with one addition it did not need: THE KEY IS ON THE
 * BUTTON. Hand-built widgets cannot be clicked on this controller (proven across six builds),
 * so a button with no key printed on it is decoration that lies about what it does. The
 * label says what it does; the bracket says how.
 *
 * Enabled state follows what is actually possible right now, so a button never offers
 * something the server would refuse: SELECT and DELETE grey out on an empty slot, and DELETE
 * greys out while the pick is locked.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsActions(parent: ref<inkCanvas>) -> Void {
    let occupied = this.MpCsRosterIndex(this.m_csCursor) >= 0;

    /*
     * CREATE NEW IS GONE. zeldfep, 2026-09-08: "I think the create new button at the bottom
     * is redundant".
     *
     * It was: clicking an empty slot already runs the creator, in THAT slot, which is the
     * behaviour he asked for. A separate button was a second route to the same thing, and a
     * worse one - it could not say which slot it would fill, so it had to guess the lowest
     * free one. Two ways to do something, one of which cannot express the choice, is a way
     * to pick the wrong slot.
     */
    let locked = this.MpCsIsLocked();

    this.MpCsActionButton(parent, 47.0, 852.0, 222.0, locked ? "LOCKED" : "SELECT", "CLICK", MpCsGold(), occupied, true, locked);
    this.MpCsActionButton(parent, 285.0, 848.0, 222.0, "DELETE", "DEL", MpCsRed(), occupied && !locked, false, false);
}

@addMethod(SingleplayerMenuGameController)
public func MpCsActionButton(parent: ref<inkCanvas>, x: Float, y: Float, w: Float, label: String,
                             key: String, colour: HDRColor, enabled: Bool, padlock: Bool, shut: Bool) -> Void {
    let h = 66.0;
    let row = MpCsFanL(parent, y + h / 2.0);
    let xs = MpCsCutXs(x, w, 12.0);
    let ys = MpCsCutYs(y, h, 12.0);
    let ink = enabled ? colour : MpCsInkFaint();
    let ix = x + 20.0;
    let iy = y + 20.0;

    // Locked in wears the selected card's halo and plate, so the two read as one state.
    if shut {
        MpCsPolyEdge(row, MpCsCutXs(x - 4.0, w + 8.0, 14.0), MpCsCutYs(y - 4.0, h + 8.0, 14.0), 2.0, MpCsGold(), 0.22);
        MpCsPolyEdge(row, MpCsCutXs(x - 8.0, w + 16.0, 16.0), MpCsCutYs(y - 8.0, h + 16.0, 16.0), 3.0, MpCsGold(), 0.08);
    }

    MpCsPolyFill(row, xs, ys, shut ? MpCsGoldPlate() : MpCsPlate(), shut ? 0.88 : (enabled ? 0.82 : 0.45));
    MpCsPolyEdge(row, xs, ys, shut ? 2.5 : (enabled ? 1.5 : 1.0), enabled ? colour : MpCsRed(), enabled ? 1.0 : 0.25);

    if padlock {
        MpCsRect(row, ix + 3.0, iy + 11.0, 18.0, 13.0, ink, 1.0);

        if shut {
            MpCsLine(row, ix + 7.0, iy + 11.0, ix + 7.0, iy + 4.0, 2.0, ink, 1.0);
            MpCsLine(row, ix + 6.0, iy + 4.0, ix + 18.0, iy + 4.0, 2.0, ink, 1.0);
            MpCsLine(row, ix + 17.0, iy + 4.0, ix + 17.0, iy + 11.0, 2.0, ink, 1.0);
        } else {
            // Open: the shackle stands clear of the body and its right leg stops short of it.
            MpCsLine(row, ix + 7.0, iy + 11.0, ix + 7.0, iy - 2.0, 2.0, ink, 1.0);
            MpCsLine(row, ix + 6.0, iy - 2.0, ix + 18.0, iy - 2.0, 2.0, ink, 1.0);
            MpCsLine(row, ix + 17.0, iy - 2.0, ix + 17.0, iy + 3.0, 2.0, ink, 1.0);
        }
    } else {
        MpCsLine(row, ix + 3.0, iy + 6.0, ix + 21.0, iy + 6.0, 2.0, ink, 1.0);
        MpCsLine(row, ix + 9.0, iy + 6.0, ix + 9.0, iy + 2.0, 2.0, ink, 1.0);
        MpCsLine(row, ix + 8.0, iy + 2.0, ix + 16.0, iy + 2.0, 2.0, ink, 1.0);
        MpCsLine(row, ix + 15.0, iy + 2.0, ix + 15.0, iy + 6.0, 2.0, ink, 1.0);
        MpCsLine(row, ix + 6.0, iy + 6.0, ix + 7.0, iy + 24.0, 2.0, ink, 1.0);
        MpCsLine(row, ix + 6.0, iy + 24.0, ix + 18.0, iy + 24.0, 2.0, ink, 1.0);
        MpCsLine(row, ix + 17.0, iy + 24.0, ix + 18.0, iy + 6.0, 2.0, ink, 1.0);
    }

    MpCsText(row, x + 54.0, y + 19.0, label, 22, n"Bold", ink);

    let bracket = MpCsText(row, x + w - 16.0, y + 25.0, s"[ \(key) ]", 15, n"Medium", enabled ? MpCsGold() : MpCsInkFaint());

    MpCsAlign(bracket, 1.0, 0.0);
}

/**
 * The go plate, bottom right.
 *
 * The only solid gold thing on the screen, because it is the only action that ends it. Every
 * other control is an outline; this one is filled, which is the design language's way of
 * saying "this is the thing you came here to press".
 *
 * It reads what the caret is on, so it never offers to enter the world as nobody: an empty
 * slot turns it into CREATE, which is what pressing it actually does there.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsEnterButton(parent: ref<inkCanvas>) -> Void {
    let network = GameInstance.GetNetworkWorldSystem();
    let index = this.MpCsRosterIndex(this.m_csCursor);
    let occupied = index >= 0;
    let x = 1347.0;
    let y = 850.0;
    let w = 372.0;
    let h = 86.0;

    /*
     * THE CONFIRMATION, AND IT NAMES WHO. zeldfep: "select should have a confirmation box on
     * it so its actionable 'yes I want to pick this character'".
     *
     * "Click the card again" was a confirmation nobody could see - the same gesture as
     * selecting, distinguished only by being the second one. This says what will happen and
     * to whom, so the press is a decision rather than a repetition. The render's plate says
     * ENTER CITY and its tab says something else; here the tab carries the confirmation:
     *
     *   a character  ->  ENTER CITY   | YES - PLAY AS <NAME>
     *   an empty slot ->  CREATE      | YES - CREATE IN SLOT N
     *
     * The name is the point. With four slots and a roster that changes, "enter the world" is
     * not enough to be sure which person you are about to be - and being the wrong one is
     * exactly what this screen exists to prevent.
     */
    let plate = "NO SLOT";
    let ask = "SELECT A SLOT";
    let who = "ON THE LEFT";
    let armed = false;

    if occupied && IsDefined(network) {
        let name = network.GetRosterName(Cast<Uint32>(index));
        let shown = NotEquals(name, "") ? name : "unnamed";

        plate = "ENTER CITY";
        ask = "YES - PLAY AS";
        who = StrUpper(MpCsClip(shown, 12));
        armed = true;
    } else {
        if this.m_csCursor >= 0 {
            plate = "CREATE";
            ask = "YES - CREATE IN";
            who = s"SLOT 0\(this.m_csCursor + 1)";
            armed = true;
        }
    }

    let row = MpCsFan(parent, 1889.0, y + h / 2.0, MpCsTilt(y + h / 2.0, -1.0));
    let ink = armed ? MpCsOnGold() : MpCsInkFaint();

    // The offset base and the face, the launcher's plate.
    MpCsPolyFill(row, [x + 7.0, x + w + 7.0, x + w + 7.0, x + 25.0, x + 7.0],
                 [y + 7.0, y + 7.0, y + h + 7.0, y + h + 7.0, y + h - 11.0], MpCsGoldDim(), armed ? 1.0 : 0.2);
    MpCsPolyFill(row, [x, x + w, x + w, x + 18.0, x], [y, y, y + h, y + h, y + h - 18.0], MpCsGold(), armed ? 1.0 : 0.24);

    MpCsLine(row, x + 22.0, y + 29.0, x + 34.0, y + 43.0, 4.0, ink, 1.0);
    MpCsLine(row, x + 34.0, y + 43.0, x + 22.0, y + 57.0, 4.0, ink, 1.0);
    MpCsLine(row, x + 38.0, y + 29.0, x + 50.0, y + 43.0, 4.0, ink, 1.0);
    MpCsLine(row, x + 50.0, y + 43.0, x + 38.0, y + 57.0, 4.0, ink, 1.0);

    MpCsTextWide(row, x + 70.0, y + 24.0, plate, 36, n"Bold", ink);

    // The tab that names who.
    let tabX = x + w + 4.0;
    let tabW = 166.0;
    let tabXs = MpCsCutXs(tabX, tabW, 12.0);
    let tabYs = MpCsCutYs(y + 2.0, h - 4.0, 12.0);

    MpCsPolyFill(row, tabXs, tabYs, MpCsPlate(), 0.9);
    MpCsPolyEdge(row, tabXs, tabYs, 1.5, MpCsGold(), armed ? 1.0 : 0.3);
    MpCsText(row, tabX + 14.0, y + 17.0, ask, 17, n"Medium", armed ? MpCsGold() : MpCsInkFaint());
    MpCsText(row, tabX + 14.0, y + 44.0, who, 20, n"Bold", armed ? MpCsInk() : MpCsInkFaint());
}

// ---------------------------------------------------------------------- what a click tests
//
// One set of numbers. MpCsHitAt decides with them and the click map at the bottom of this file
// draws them, so an outline on screen is always the region that is live. A box is
// [left, top, right, bottom] in authored units.

public func MpCsHitEnter() -> array<Float> = [1340.0, 826.0, 1895.0, 952.0]

// MpCsActions draws DELETE at x 285-507, y 848-914, leaning up by about eight at its centre.
public func MpCsHitDelete() -> array<Float> = [279.0, 832.0, 513.0, 922.0]

// MpCsActions draws SELECT at x 47-269, y 852-918, leaning up by about five at its right end.
// Same margins as DELETE; the two boxes stop four units short of each other.
public func MpCsHitSelect() -> array<Float> = [41.0, 841.0, 275.0, 932.0]

public func MpCsCardFirst() -> Float = 163.0

public func MpCsCardPitch() -> Float = 158.0

public func MpCsCardHeight() -> Float = 130.0

/**
 * The roster block, with slack above and below so a click just over the first card or under
 * the last still reads as aiming at it. Forty, the bias measured below: the title sits just over
 * the first card and the buttons just under the last.
 *
 * The x range stays a plain bound: there is only one column, so nothing to be nearest TO.
 */
public func MpCsHitRoster() -> array<Float> =
    [40.0, MpCsCardFirst() - 40.0, 620.0,
     MpCsCardFirst() + Cast<Float>(MpCsMaxSlots() - 1) * MpCsCardPitch() + MpCsCardHeight() + 40.0]

/**
 * Where a card's centre line is at a given x. Each card leans about x 60, so it depends on where
 * along the card you are.
 */
public func MpCsCardCentre(slot: Int32, x: Float) -> Float {
    let middle = MpCsCardFirst() + Cast<Float>(slot) * MpCsCardPitch() + MpCsCardHeight() / 2.0;

    return middle + (x - 60.0) * TanF(Deg2Rad(MpCsTilt(middle, 1.0)));
}

public func MpCsInBox(x: Float, y: Float, box: array<Float>) -> Bool =
    x >= box[0] && x <= box[2] && y >= box[1] && y <= box[3]

public func MpCsHitIdNone() -> Int32 = -1

public func MpCsHitIdEnter() -> Int32 = 100

public func MpCsHitIdDelete() -> Int32 = 101

public func MpCsHitIdSelect() -> Int32 = 102

public func MpCsHitName(hit: Int32) -> String {
    if hit == MpCsHitIdEnter() {
        return "ENTER";
    }

    if hit == MpCsHitIdDelete() {
        return "DELETE";
    }

    if hit == MpCsHitIdSelect() {
        return "SELECT";
    }

    return hit >= 0 ? s"CARD \(hit + 1)" : "NOTHING";
}

/**
 * What a point on the screen is, without doing anything about it: a slot index, one of the
 * MpCsHitId values, or none. ENTER is tested first with a generous margin: it overlaps nothing,
 * so being loose costs nothing and being tight costs a press that appears to do nothing.
 *
 * NEAREST CARD WINS, rather than strict bands.
 *
 * zeldfep's three measurements all landed in the LOWER half of their card:
 *
 *   372 -> slot 1 (band 322-412, centre 367)
 *   590 -> slot 3 (band 520-610, centre 565)
 *   653 -> slot 4 (band 619-709, centre 664)
 *
 * Consistent with a downward bias of roughly 40px between where the cursor looks and
 * where the click reports - which is exactly his "I have to click a little above where
 * the button actually is", and why "slot 2 is hard to get": it is the one card bracketed
 * on both sides, so the bias pushes a centred aim into slot 3.
 *
 * Measuring that bias and subtracting a constant would work until it was wrong - a
 * different resolution, a different UI scale, and it is off again with nobody knowing
 * why. Nearest-centre is correct for ANY offset smaller than half the card pitch, and it
 * degrades gently rather than snapping to the wrong card at a boundary. It also removes
 * the dead gaps between cards, where a click previously did nothing at all.
 */
public func MpCsHitAt(x: Float, y: Float) -> Int32 {
    if MpCsInBox(x, y, MpCsHitEnter()) {
        return MpCsHitIdEnter();
    }

    if MpCsInBox(x, y, MpCsHitDelete()) {
        return MpCsHitIdDelete();
    }

    if MpCsInBox(x, y, MpCsHitSelect()) {
        return MpCsHitIdSelect();
    }

    if !MpCsInBox(x, y, MpCsHitRoster()) {
        return MpCsHitIdNone();
    }

    let best = MpCsHitIdNone();
    let bestDistance = 99999.0;
    let slot = 0;

    while slot < MpCsMaxSlots() {
        let distance = AbsF(y - MpCsCardCentre(slot, x));

        if distance < bestDistance {
            bestDistance = distance;
            best = slot;
        }

        slot += 1;
    }

    return best;
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
 *   SELECT               lock or unlock the character the caret is on
 *
 * Acting means the obvious thing for the slot: enter the world as a character, or run the
 * creator on an empty one. That is why all four empty slots "point to new character" - each
 * one is a create target in its own right, which zeldfep asked for and which was already
 * true in the drawing and simply unreachable.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsClickAt(x: Float, y: Float) -> Void {
    let hit = MpCsHitAt(x, y);

    if hit == MpCsHitIdSelect() {
        this.MpCsToggleLock();
        return;
    }

    if hit == MpCsHitIdEnter() {
        this.MpCsAct();
        return;
    }

    // Click is the input that reliably arrives (the DEL key's delete_save never reaches
    // OnGlobalRelease), so deletion is driven from here. Two clicks: arm, then confirm (MpCsDelete).
    if hit == MpCsHitIdDelete() {
        this.MpCsDelete();
        return;
    }

    if hit < 0 {
        return;
    }

    // Locked in: a press on a card neither moves the caret nor acts on it.
    if this.MpCsIsLocked() {
        this.MpCsSayLocked();
        return;
    }

    if this.m_csCursor == hit {
        this.MpCsAct();
    } else {
        this.MpCsSelect(hit);
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

// After a delete is sent, the roster reply lands on the network thread with no script event.
// Poll until the deleted slot is empty in the roster (or the server set an error refusing it),
// then re-render the selector so the card actually disappears.
public class MpCsDeleteRefresh extends DelayCallback {
    public let controller: wref<SingleplayerMenuGameController>;
    public let slot: Int32;
    public let attempts: Int32;

    public func Call() -> Void {
        if !IsDefined(this.controller) {
            return;
        }
        let network = GameInstance.GetNetworkWorldSystem();
        if !IsDefined(network) {
            return;
        }

        // Either outcome is an answer: the slot is now empty (deleted) or the server said why not.
        if this.controller.MpCsRosterIndex(this.slot) < 0 || NotEquals(network.GetCharacterError(), "") {
            this.controller.MpCsOpen();
            return;
        }

        this.attempts += 1;
        if this.attempts >= 12 {
            MpCsLog(s"the server never answered the delete");
            return;
        }

        let again = new MpCsDeleteRefresh();
        again.controller = this.controller;
        again.slot = this.slot;
        again.attempts = this.attempts;
        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(again, 0.25, false);
    }
}

/**
 * DELETE the character in the selected slot. Two calls: the first arms and puts ARE YOU SURE on
 * the card, the second sends. SLOT-EXPLICIT - DeleteCharacterSlot(m_csCursor) names the slot on
 * the wire, so the server does not have to infer it from the active slot (which a live puppet can
 * make wrong, and which the pin-down showed was never reaching the server at all via the DEL key).
 *
 * DO NOT UNDO the slot-explicit path (zeldfep, 2026-09-14: "stop breaking this specific part
 * we've looped like 5 times"). Reverting to a slot-less DeleteCharacter() deletes whatever the
 * server thinks is active - which, with a live puppet, is the wrong character or nothing. The
 * store-level guarantee (delete removes EXACTLY the named slot, siblings intact, non-contiguous
 * survives) is locked in tools/tests/characterlifecycle_test.cpp.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsDelete() -> Void {
    if this.MpCsIsLocked() {
        this.MpCsSayLocked();
        return;
    }

    let network = GameInstance.GetNetworkWorldSystem();
    if !IsDefined(network) || !network.IsConnected() {
        MpCsLog(s"delete clicked with no connection");
        return;
    }

    if this.MpCsRosterIndex(this.m_csCursor) < 0 {
        this.MpCsSay("Nothing in that slot to delete.");
        return;
    }

    if this.m_csDeleteArmed {
        this.m_csDeleteArmed = false;
        MpCsLog(s"delete confirmed for slot \(this.m_csCursor + 1) - sending to server");
        this.MpCsSay("Deleting...");
        network.DeleteCharacterSlot(this.m_csCursor);
        // The delete reply lands on the network thread with no script event to hang off, so
        // poll for the roster to change and re-render the selector - otherwise the deleted card
        // stays on screen and it looks like the delete did nothing (it did; see the server log).
        let refresh = new MpCsDeleteRefresh();
        refresh.controller = this;
        refresh.slot = this.m_csCursor;
        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(refresh, 0.25, false);
        return;
    }

    this.m_csDeleteArmed = true;
    this.MpCsOpen();
    this.MpCsSay("ARE YOU SURE? Click DELETE again to delete this character.");
}

/**
 * True while the player has locked in the character the caret is on. It asks the roster as well
 * as the flag, so a slot that empties underneath the lock cannot stay locked.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsIsLocked() -> Bool {
    return this.m_csLocked && this.MpCsRosterIndex(this.m_csCursor) >= 0;
}

/**
 * SELECT'S PADLOCK. zeldfep, 2026-11-09: "make sure that its working lock so you can lock the
 * character you want to select".
 *
 * Not the same word as a LOCKED slot, which is one this account may not use. This one is the
 * player's own choice about the character the caret is on, so an empty slot refuses it: there
 * is nobody there to lock. Confirming stays with ENTER CITY, and it does not need the lock.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsToggleLock() -> Void {
    if this.MpCsRosterIndex(this.m_csCursor) < 0 {
        this.m_csLocked = false;
        this.MpCsSay("This slot is empty, so there is nobody to lock.");
        return;
    }

    this.m_csLocked = !this.m_csLocked;
    this.m_csCreateArmed = false;
    this.m_csDeleteArmed = false;

    MpCsLog(s"lock=\(this.m_csLocked) on slot \(this.m_csCursor + 1)");
    this.MpCsOpen();
}

/**
 * The answer to anything the lock has just stopped, in one place so every refusal says how to
 * undo it.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsSayLocked() -> Void {
    this.MpCsSay("Locked in. Click LOCKED to change character.");
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




/**
 * HOW MANY SCREEN PIXELS ONE AUTHORED UNIT IS WORTH.
 *
 * THE BUG THIS EXISTS FOR, and it took a day to see: the menu root is a FIXED virtual space.
 * It measures 3840x2160 on zeldfep's 1440p monitor AND on Cam's 1080p one - confirmed from
 * both their logs - so DRAWING is identical for everybody. But GetScreenSpacePosition returns
 * REAL DISPLAY PIXELS. On 1080p those happen to equal authored units and everything works by
 * coincidence, which is why Cam has never seen a problem. On 1440p every click reports 1.333x
 * too large, and the error grows with distance from the origin: a few pixels at the top card,
 * over a hundred by the bottom one.
 *
 * So the composition is right, the click is right, and the two are in different units.
 *
 * The game knows its own resolution, so ASK IT rather than calibrating per person. Anything
 * unreadable falls back to 1.0, which is exactly today's behaviour - correct on 1080p and
 * wrong elsewhere - so a failure here cannot be worse than what already ships.
 *
 * The raw string is logged. scc accepts both ConfigVarListString and ConfigVarListName casts,
 * and accepting a cast is not the same as the cast succeeding - the ToString(inkActionName)
 * lesson from earlier today, which compiled cleanly and printed the type name.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsPixelsPerUnit() -> Float {
    let settings = GameInstance.GetSettingsSystem(GetGameInstance());

    if !IsDefined(settings) {
        return 1.0;
    }

    let variable = settings.GetVar(n"/video/display", n"Resolution") as ConfigVarListString;

    if !IsDefined(variable) {
        MpCsLog(s"resolution: not a ConfigVarListString - falling back to 1:1");
        return 1.0;
    }

    let text = variable.GetValue();
    let parts = StrSplit(text, "x");

    if ArraySize(parts) < 2 {
        MpCsLog(s"resolution: '\(text)' does not parse - falling back to 1:1");
        return 1.0;
    }

    let height = StringToInt(parts[1]);

    if height < 240 {
        MpCsLog(s"resolution: '\(text)' gave height \(height) - falling back to 1:1");
        return 1.0;
    }

    // 1080 is the height the composition is authored in, not a magic number: MpCsRect and
    // every coordinate on this screen are in 1920x1080 units.
    let ratio = Cast<Float>(height) / 1080.0;

    MpCsLog(s"resolution: '\(text)' -> \(height)px tall, \(ratio) screen pixels per authored unit");
    return ratio;
}

/**
 * THE MIDDLE OF THE SCREEN: the rings, the polygon, and who it is.
 *
 * ink has no circle, so a ring is drawn as short tangential segments placed around a centre:
 * each one rotated to match its own angle, so the run of them reads as a curve rather than a
 * polygon of visible corners. Three rings at different radii, densities and opacities, which
 * is what makes it look like an instrument rather than a shape.
 *
 * The mockup animates two of them in opposite directions. These do not move: every widget
 * here is rebuilt whenever anything changes, so an animation would restart on every click
 * and read worse than stillness. Motion is worth having and wants the screen to stop
 * rebuilding wholesale first - noted rather than faked.
 *
 * THE POLYGON holds the selected character's face once the picture feature exists; until
 * then, the initial. Showing the whole character the player picks is a larger build, flagged
 * as the-selector-s-middle-should-show-the-whole-character-the-pl.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsScanRings(parent: ref<inkCanvas>) -> Void {
    let cx = 967.0;
    let cy = 470.0;

    let index = this.MpCsRosterIndex(this.m_csCursor);
    let occupied = index >= 0;

    // Gold for a character, dim red for an empty slot: the ring answers the same question
    // the card does, so it must not claim somebody is there when nobody is.
    let colour = occupied ? MpCsGold() : MpCsRedDim();

    this.MpCsRing(parent, cx, cy, 210.0, 48, 18.0, 3.0, colour, 0.85);
    this.MpCsRing(parent, cx, cy, 168.0, 24, 26.0, 2.0, colour, 0.45);
    this.MpCsRing(parent, cx, cy, 128.0, 72, 6.0, 2.0, colour, 0.30);

    // A regular hexagon is exactly three rectangles, each turned sixty degrees from the
    // last. Opaque, so where they cross does not show.
    let side = 124.0;
    let turn = 0;

    while turn < 3 {
        let piece = new inkRectangle();

        piece.SetAnchor(inkEAnchor.TopLeft);
        piece.SetAnchorPoint(new Vector2(0.5, 0.5));
        piece.SetMargin(new inkMargin(cx, cy, 0.0, 0.0));
        piece.SetSize(new Vector2(side, side * 1.7320508));
        piece.SetTintColor(MpCsPlate());
        piece.SetRotation(Cast<Float>(turn) * 60.0);
        piece.Reparent(parent);

        turn += 1;
    }

    let corner = 0;

    while corner < 6 {
        let a = Deg2Rad(Cast<Float>(corner) * 60.0);
        let b = Deg2Rad(Cast<Float>(corner + 1) * 60.0);

        MpCsLine(parent, cx + side * CosF(a), cy + side * SinF(a), cx + side * CosF(b), cy + side * SinF(b),
                 3.0, colour, 1.0);

        corner += 1;
    }

    // The subject, under the rings.
    let network = GameInstance.GetNetworkWorldSystem();

    if occupied && IsDefined(network) {
        let name = network.GetRosterName(Cast<Uint32>(index));
        let shown = NotEquals(name, "") ? name : "unnamed";
        let lifepath = network.GetRosterLifepath(Cast<Uint32>(index));

        MpCsAlign(MpCsText(parent, cx, cy, StrUpper(StrLeft(shown, 1)), 96, n"Bold", MpCsInkDim()), 0.5, 0.5);
        MpCsAlign(MpCsText(parent, cx, cy + 250.0, StrUpper(MpCsClip(shown, 22)), 44, n"Bold", MpCsInk()), 0.5, 0.0);

        if NotEquals(lifepath, "") {
            MpCsAlign(MpCsText(parent, cx, cy + 306.0, StrUpper(lifepath), 16, n"Medium", MpCsGold()), 0.5, 0.0);
        }
    } else {
        MpCsAlign(MpCsText(parent, cx, cy, "+", 96, n"Medium", MpCsInkFaint()), 0.5, 0.5);
        MpCsAlign(MpCsText(parent, cx, cy + 250.0, "NO SIGNAL", 44, n"Bold", MpCsInkFaint()), 0.5, 0.0);
        MpCsAlign(MpCsText(parent, cx, cy + 306.0, MpCsSpaced("SLOT EMPTY"), 16, n"Medium", MpCsInkFaint()), 0.5, 0.0);
    }
}

/**
 * One ring of tangential segments.
 *
 * Each segment is rotated to its own angle so the ring curves. Rotation is about the
 * widget's anchor POINT, which is why that is centred here and top-left everywhere else on
 * this screen - a segment rotated about its corner would swing off the ring entirely.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsRing(parent: ref<inkCanvas>, cx: Float, cy: Float, radius: Float, count: Int32,
                     length: Float, thickness: Float, colour: HDRColor, opacity: Float) -> Void {
    let i = 0;

    while i < count {
        let degrees = 360.0 * Cast<Float>(i) / Cast<Float>(count);
        let radians = degrees * 3.14159265 / 180.0;

        let x = cx + radius * CosF(radians);
        let y = cy + radius * SinF(radians);

        let segment = new inkRectangle();
        segment.SetAnchor(inkEAnchor.TopLeft);
        segment.SetAnchorPoint(new Vector2(0.5, 0.5));
        segment.SetMargin(new inkMargin(x, y, 0.0, 0.0));
        segment.SetSize(new Vector2(length, thickness));
        segment.SetTintColor(colour);
        segment.SetOpacity(opacity);

        // Tangent, not radius: +90 degrees so the segment lies along the circle.
        segment.SetRotation(degrees + 90.0);
        segment.Reparent(parent);

        i += 1;
    }
}

// ============================================================================ detail

/**
 * The dossier on whoever the caret is on.
 *
 * The render's panel: a trapezoid, because the panel curves away from the viewer. Every row
 * inside leans with it. Only facts the server sends today are on it; the render's street
 * cred, last location, play time and playstyle need data this screen does not get (Atlas
 * character-select-mockup-items-never-built).
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsDetail(parent: ref<inkCanvas>) -> Void {
    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) {
        return;
    }

    let L = 1355.0;
    let E = 1868.0;
    let xs = [L, E - 24.0, E, E, E - 20.0, L + 20.0, L];
    let ys = [118.0, 96.0, 120.0, 802.0, 824.0, 810.0, 790.0];

    MpCsPolyFill(parent, xs, ys, MpCsPlate(), 0.86);
    MpCsPolyEdge(parent, xs, ys, 1.0, MpCsRed(), 0.34);
    MpCsLine(parent, L, 119.0, L + 40.0, 117.0, 4.0, MpCsGold(), 1.0);

    let index = this.MpCsRosterIndex(this.m_csCursor);

    if index < 0 {
        MpCsTextWide(MpCsFanR(parent, 170.0), 1392.0, 146.0, "NO CHARACTER", 38, n"Bold", MpCsInkFaint());
        MpCsRect(MpCsFanR(parent, 318.0), 1392.0, 318.0, 450.0, 1.0, MpCsRedDim(), 0.75);

        let empty = MpCsFanR(parent, 370.0);

        MpCsText(empty, 1392.0, 346.0, "This slot is empty.", 24, n"Medium", MpCsInkDim());
        MpCsText(empty, 1392.0, 382.0, "Click it again to run the creator and fill it.", 22, n"Medium", MpCsInkDim());
        return;
    }

    let u = Cast<Uint32>(index);
    let name = network.GetRosterName(u);
    let shown = NotEquals(name, "") ? name : "unnamed";
    let lifepath = network.GetRosterLifepath(u);
    let lines = MpCsLifepathLines(lifepath);

    MpCsTextWide(MpCsFanR(parent, 170.0), 1392.0, 148.0, StrUpper(MpCsClip(shown, 11)), 38, n"Bold", MpCsGold());

    let level = MpCsFanR(parent, 156.0);

    MpCsPolyEdge(level, MpCsCutXs(1752.0, 100.0, 8.0), MpCsCutYs(134.0, 44.0, 8.0), 1.5, MpCsGold(), 1.0);
    MpCsAlign(MpCsTextWide(level, 1802.0, 156.0, s"LV. \(network.GetRosterLevel(u))", 23, n"Medium", MpCsGold()), 0.5, 0.5);

    let id = MpCsFanR(parent, 240.0);

    MpCsLifepathIcon(id, 1392.0, 210.0, 50.0, 50.0, lifepath, false, MpCsGold(), 1.0);
    MpCsText(id, 1456.0, 212.0, StrUpper(NotEquals(lifepath, "") ? lifepath : "no lifepath"), 27, n"Medium", MpCsInk());

    if ArraySize(lines) >= 6 {
        MpCsText(id, 1456.0, 252.0, lines[0], 18, n"Medium", MpCsInkDim());
        MpCsText(id, 1456.0, 275.0, lines[1], 18, n"Medium", MpCsInkDim());
    }

    MpCsRect(MpCsFanR(parent, 318.0), 1392.0, 318.0, 450.0, 1.0, MpCsInkDim(), 0.35);

    // Key/value rows. Every one of these is a fact the server sent, which is the point of
    // the panel: it is the roster made legible, not a description of it.
    this.MpCsRow(parent, 354.0, "LIFEPATH", NotEquals(lifepath, "") ? lifepath : "not recorded");
    this.MpCsRow(parent, 416.0, "SLOT", s"0\(this.m_csCursor + 1) OF 04");
    this.MpCsRow(parent, 459.0, "STATUS", network.HasRosterSpawnedBefore(u) ? "played" : "never played");
    this.MpCsRow(parent, 502.0, "IN PLAY", network.IsRosterActive(u) ? "yes - this is you" : "no");

    this.MpCsAttributes(parent, index);

    // The render's right-hand column: the emblem large, in red, over a line of graffiti. The
    // emblem is the game's own, so every lifepath has one.
    MpCsRect(parent, 1682.0, 556.0, 1.0, 236.0, MpCsRedDim(), 0.75);
    MpCsLifepathIcon(parent, 1716.0, 556.0, 136.0, 136.0, lifepath, true, MpCsRed(), 0.9);

    if ArraySize(lines) >= 6 {
        let i = 2;

        while i < 6 {
            MpCsText(parent, 1706.0, 706.0 + Cast<Float>(i - 2) * 22.0, lines[i], 18, n"Medium", MpCsRed());
            i += 1;
        }
    }
}

/**
 * A key/value row, centred on yc, with its rule underneath.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsRow(parent: ref<inkCanvas>, yc: Float, key: String, value: String) -> Void {
    let row = MpCsFanR(parent, yc);

    MpCsText(row, 1392.0, yc - 10.0, key, 18, n"Medium", MpCsInkDim());
    MpCsText(row, 1620.0, yc - 12.0, value, 19, n"Medium", MpCsInk());
    MpCsRect(row, 1392.0, yc + 21.0, 450.0, 1.0, MpCsRedDim(), 0.5);
}

// ============================================================================ status

/**
 * The server's answer, where the player can see it.
 *
 * "the server refused: Leave the world before switching characters" was written to the log
 * and NOWHERE ELSE. zeldfep clicked an empty slot, got no creator and no explanation, and
 * the only record of why was a warning line on a box in another room.
 *
 * A refusal is the answer to the thing somebody just pressed. It belongs on the screen that
 * asked, in the colour the design language uses for "this did not happen".
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsStatusLine(parent: ref<inkCanvas>, x: Float, y: Float) -> Void {
    let network = GameInstance.GetNetworkWorldSystem();
    let message = "";

    let refused = false;

    if IsDefined(network) {
        let error = network.GetCharacterError();

        // A refusal outranks everything - it is the answer to the press they just made.
        if NotEquals(error, "") {
            message = error;
            refused = true;
        } else {
            if !network.HasCharacter() {
                message = "No character yet - click an empty slot to make one.";
            }
        }
    }

    /*
     * A REFUSAL LOOKS LIKE A REFUSAL.
     *
     * This was gold at 19px on a screen already full of gold, and it disappeared. zeldfep
     * clicked an empty slot, the server answered "Leave the world before switching
     * characters", and all he saw was nothing happening - the message was on screen and
     * indistinguishable from the hint text it replaced.
     *
     * Red, larger, and on its own plate. The design language reserves red for the thing
     * that did not happen, and this is the one place on the screen that ever says so.
     */
    if refused {
        MpCsRect(parent, x - 14.0, y - 12.0, 1180.0, 54.0, MpCsRed(), 0.16);
        MpCsRect(parent, x - 14.0, y - 12.0, 5.0, 54.0, MpCsRed(), 1.0);

        this.m_csStatus = MpCsText(parent, x + 8.0, y, message, 22, n"Bold", MpCsRed());
        return;
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

/**
 * THE ATTRIBUTES: the game's own icon, the name and the number, as the render has them.
 *
 * The flag day that carries them: CharacterSummary gained a repeated Attribute so the
 * selector can show who somebody IS, not only what they are called.
 *
 * THE TYPE IS NOT INTERPRETED HERE. It arrives as the game's own gamedataStatType number and
 * a row is labelled from MpCsAttributeName, which falls back to the raw number.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsAttributes(parent: ref<inkCanvas>, index: Int32) -> Void {
    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) || index < 0 {
        return;
    }

    let u = Cast<Uint32>(index);
    let count = network.GetRosterAttributeCount(u);
    let head = MpCsFanR(parent, 546.0);

    MpCsText(head, 1392.0, 536.0, "ATTRIBUTES", 18, n"Medium", MpCsInkDim());

    if count == 0u {
        MpCsText(MpCsFanR(parent, 586.0), 1392.0, 576.0, "none recorded for this character", 18, n"Medium", MpCsInkFaint());
        return;
    }

    let i = 0u;

    // Cyberpunk has exactly five attributes.
    while i < count && i < 5u {
        let type = network.GetRosterAttributeType(u, i);
        let value = network.GetRosterAttributeValue(u, i);
        let yc = 586.0 + Cast<Float>(i) * 43.0;
        let row = MpCsFanR(parent, yc);

        MpCsAttributeIcon(row, 1390.0, yc - 15.0, 30.0, type, MpCsInkDim());
        MpCsText(row, 1434.0, yc - 10.0, MpCsAttributeName(type), 18, n"Medium", MpCsInkDim());
        MpCsAlign(MpCsText(row, 1658.0, yc - 12.0, s"\(value)", 21, n"Medium", MpCsInk()), 1.0, 0.0);
        MpCsRect(row, 1434.0, yc + 19.0, 226.0, 1.0, MpCsRedDim(), 0.45);

        i += 1u;
    }
}

/**
 * The game's own icon for an attribute, by the same comparison MpCsAttributeName uses.
 * Part names read from attributes_icons.inkatlas and PerkAttributeHelper.GetIconAtlasPart.
 */
public func MpCsAttributeIcon(parent: ref<inkCompoundWidget>, x: Float, y: Float, size: Float,
                              type: Uint32, color: HDRColor) -> Void {
    let part = n"";

    if type == Cast<Uint32>(EnumInt(gamedataStatType.Strength)) { part = n"ico_body"; }
    if type == Cast<Uint32>(EnumInt(gamedataStatType.Reflexes)) { part = n"ico_ref"; }
    if type == Cast<Uint32>(EnumInt(gamedataStatType.Intelligence)) { part = n"ico_int"; }
    if type == Cast<Uint32>(EnumInt(gamedataStatType.TechnicalAbility)) { part = n"ico_tech"; }
    if type == Cast<Uint32>(EnumInt(gamedataStatType.Cool)) { part = n"ico_cool"; }

    if Equals(part, n"") {
        return;
    }

    let i = new inkImage();

    i.SetAnchor(inkEAnchor.TopLeft);
    i.SetAnchorPoint(new Vector2(0.0, 0.0));
    i.SetMargin(new inkMargin(x, y, 0.0, 0.0));
    i.SetSize(new Vector2(size, size));
    i.SetAtlasResource(r"base\\gameplay\\gui\\common\\icons\\attributes_icons.inkatlas");
    i.SetTexturePart(part);
    i.SetTintColor(color);
    i.Reparent(parent);
}

/**
 * A name for an attribute type, or the raw number.
 *
 * The five attributes are gamedataStatType entries and their numeric values are not
 * recorded anywhere this side can check. Rather than invent five, every unknown type prints
 * itself - which is useless on screen for exactly one build and then becomes the evidence
 * that fills this in.
 *
 * Named 2026-09-10, and no number is hardcoded to do it: the capture side sends
 * Cast<Uint32>(EnumInt(gamedataStatType.X)) for exactly these five (Inventory.reds, the
 * attrs list), so comparing against the same expression here is symmetric by construction
 * and survives the enum renumbering across a game patch. The live evidence agreed before
 * this landed: the dossier showed 1518 and 1275, which are Strength and Reflexes.
 * Labels are the mockup's (character-select.html): the game calls Strength "Body" on
 * every player-facing surface, and the mockup abbreviates TechnicalAbility to "Tech".
 */
public func MpCsAttributeName(type: Uint32) -> String {
    if type == Cast<Uint32>(EnumInt(gamedataStatType.Strength)) { return "BODY"; }
    if type == Cast<Uint32>(EnumInt(gamedataStatType.Reflexes)) { return "REFLEXES"; }
    if type == Cast<Uint32>(EnumInt(gamedataStatType.Intelligence)) { return "INTELLIGENCE"; }
    if type == Cast<Uint32>(EnumInt(gamedataStatType.TechnicalAbility)) { return "TECH"; }
    if type == Cast<Uint32>(EnumInt(gamedataStatType.Cool)) { return "COOL"; }

    // An unknown type still prints itself - a wrong label is worse than a raw number.
    return s"ATTR \(type)";
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

// ============================================================================ the click map
//
// WHERE THE CLICKS ACTUALLY GO, DRAWN ON THE SCREEN. zeldfep, 2026-09-08: "how about you let
// me draw where the buttons are instead of guessing". The first version found the 1440p click
// bug in one sentence ("your draw box is correct the selection is wayyy off"), came out once
// that was fixed, and was kept on the server. It carried its own copy of the box numbers, so
// the first layout change made it wrong. This one reads MpCsHitEnter and friends, the same
// numbers MpCsHitAt tests, so an outline cannot disagree with a click.
//
// WHAT IT DRAWS: every click box with its coordinates, the lines where one card hands over to
// the next, a crosshair on the last click with what it hit, rulers on two edges, and two exact
// points for working out a transform. MEASURE turns clicks into corners and prints the box
// between them, with the same numbers going to the client log.
//
// CYAN IS THE CLICK. It is --glitch in docs/DESIGN-LANGUAGE.md, the one hue outside the palette,
// and it means the click and nothing else. White is what YOU measured. Nothing else on this
// screen uses either as an outline, so a screenshot cannot confuse the tool with the design.
//
// DEVELOPER ACCOUNTS ONLY, through the slot count: developers are given four, a public account
// one. If extra slots ever go on sale, this needs a real role check first.

public func MpCsMapCyan() -> HDRColor = new HDRColor(0.235, 0.91, 0.937, 1.0)

public func MpCsMapChipView() -> array<Float> = [640.0, 8.0, 760.0, 40.0]

public func MpCsMapChipMeasure() -> array<Float> = [768.0, 8.0, 926.0, 40.0]

public func MpCsMapInt(value: Float) -> String = IntToString(Cast<Int32>(value))

/**
 * Ink cannot measure text until a layout has run and this layer is rebuilt on every click, so
 * a plate is sized from the length. Measured on the Selector Test bench in Rajdhani Medium 15:
 * these labels run 6.3 to 7.0 units a character, so 7.4 errs wide rather than clipping.
 */
public func MpCsMapTextWidth(text: String) -> Float = Cast<Float>(StrLen(text)) * 7.4 + 14.0

/**
 * An inverted plate: solid colour, dark text. The label a box carries.
 */
public func MpCsMapTag(parent: ref<inkCompoundWidget>, x: Float, y: Float, text: String,
                       color: HDRColor) -> Void {
    let w = MpCsMapTextWidth(text);
    let left = ClampF(x, 4.0, 1916.0 - w);
    let top = ClampF(y, 4.0, 1054.0);

    MpCsRect(parent, left, top, w, 22.0, color, 1.0);
    MpCsText(parent, left + 7.0, top + 1.0, text, 15, n"Medium", MpCsVoid());
}

/**
 * Coloured text on a dark plate, for the readouts that should not shout. Text over the
 * backdrop always gets a plate: the picture behind it is a photograph of a busy UI.
 */
public func MpCsMapNote(parent: ref<inkCompoundWidget>, x: Float, y: Float, text: String,
                        color: HDRColor) -> Void {
    let w = MpCsMapTextWidth(text);
    let left = ClampF(x, 4.0, 1916.0 - w);
    let top = ClampF(y, 4.0, 1054.0);

    MpCsRect(parent, left, top, w, 22.0, MpCsVoid(), 0.86);
    MpCsText(parent, left + 7.0, top + 1.0, text, 15, n"Medium", color);
}

/**
 * A two-unit outline with heavier corners. Two units because the first version drew one and
 * zeldfep could not find it; the corners are what lets a box read over the busy backdrop.
 */
public func MpCsMapFrame(parent: ref<inkCompoundWidget>, x: Float, y: Float, w: Float, h: Float,
                         color: HDRColor) -> Void {
    let arm = 16.0;
    let thick = 5.0;

    MpCsRect(parent, x, y, w, 2.0, color, 1.0);
    MpCsRect(parent, x, y + h - 2.0, w, 2.0, color, 1.0);
    MpCsRect(parent, x, y, 2.0, h, color, 1.0);
    MpCsRect(parent, x + w - 2.0, y, 2.0, h, color, 1.0);

    MpCsRect(parent, x, y, arm, thick, color, 1.0);
    MpCsRect(parent, x, y, thick, arm, color, 1.0);
    MpCsRect(parent, x + w - arm, y, arm, thick, color, 1.0);
    MpCsRect(parent, x + w - thick, y, thick, arm, color, 1.0);
    MpCsRect(parent, x, y + h - thick, arm, thick, color, 1.0);
    MpCsRect(parent, x, y + h - arm, thick, arm, color, 1.0);
    MpCsRect(parent, x + w - arm, y + h - thick, arm, thick, color, 1.0);
    MpCsRect(parent, x + w - thick, y + h - arm, thick, arm, color, 1.0);
}

/**
 * One click box: a faint fill so the area reads as an area, its frame, and its numbers.
 */
public func MpCsMapBox(parent: ref<inkCompoundWidget>, box: array<Float>, name: String, tagY: Float) -> Void {
    let cyan = MpCsMapCyan();
    let w = box[2] - box[0];
    let h = box[3] - box[1];

    MpCsRect(parent, box[0], box[1], w, h, cyan, 0.08);
    MpCsMapFrame(parent, box[0], box[1], w, h, cyan);
    MpCsMapTag(parent, box[0], tagY,
               s"\(name)  X \(MpCsMapInt(box[0]))-\(MpCsMapInt(box[2]))  Y \(MpCsMapInt(box[1]))-\(MpCsMapInt(box[3]))",
               cyan);
}

/**
 * A cross with an open middle, so whatever is under the point stays visible.
 */
public func MpCsMapCross(parent: ref<inkCompoundWidget>, x: Float, y: Float, arm: Float, thick: Float,
                         color: HDRColor) -> Void {
    let half = thick / 2.0;

    MpCsRect(parent, x - arm - 8.0, y - half, arm, thick, color, 1.0);
    MpCsRect(parent, x + 8.0, y - half, arm, thick, color, 1.0);
    MpCsRect(parent, x - half, y - arm - 8.0, thick, arm, color, 1.0);
    MpCsRect(parent, x - half, y + 8.0, thick, arm, color, 1.0);
    MpCsRect(parent, x - 1.0, y - 1.0, 2.0, 2.0, color, 1.0);
}

@addField(SingleplayerMenuGameController)
let m_csMapRoot: wref<inkCanvas>;

@addField(SingleplayerMenuGameController)
let m_csMapHidden: Bool;

@addField(SingleplayerMenuGameController)
let m_csMapMeasure: Bool;

@addField(SingleplayerMenuGameController)
let m_csMapPerUnit: Float;

@addField(SingleplayerMenuGameController)
let m_csMapClicked: Bool;

@addField(SingleplayerMenuGameController)
let m_csMapX: Float;

@addField(SingleplayerMenuGameController)
let m_csMapY: Float;

@addField(SingleplayerMenuGameController)
let m_csMapPixelX: Float;

@addField(SingleplayerMenuGameController)
let m_csMapPixelY: Float;

@addField(SingleplayerMenuGameController)
let m_csMapHit: Int32;

@addField(SingleplayerMenuGameController)
let m_csMapCorners: Int32;

@addField(SingleplayerMenuGameController)
let m_csMapFirstX: Float;

@addField(SingleplayerMenuGameController)
let m_csMapFirstY: Float;

@addMethod(SingleplayerMenuGameController)
public func MpCsMapAllowed() -> Bool {
    let network = GameInstance.GetNetworkWorldSystem();

    return IsDefined(network) && network.GetCharacterSlots() > 1;
}

/**
 * The display as the game has it, without the log line MpCsPixelsPerUnit writes.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsMapDisplay() -> String {
    let settings = GameInstance.GetSettingsSystem(GetGameInstance());

    if !IsDefined(settings) {
        return "UNKNOWN";
    }

    let variable = settings.GetVar(n"/video/display", n"Resolution") as ConfigVarListString;

    return IsDefined(variable) ? variable.GetValue() : "UNKNOWN";
}

/**
 * Called last in MpCsOpen, so it sits on top. The layer is its own canvas because a click that
 * hits nothing must still move the crosshair, and rebuilding the whole screen for that would
 * throw away the backdrop to redraw a cross.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsMapBuild(parent: ref<inkCanvas>) -> Void {
    if !this.MpCsMapAllowed() {
        return;
    }

    let layer = new inkCanvas();

    layer.SetName(n"mp_cs_click_map");
    layer.SetAnchor(inkEAnchor.TopLeft);
    layer.SetAnchorPoint(new Vector2(0.0, 0.0));
    layer.SetMargin(new inkMargin(0.0, 0.0, 0.0, 0.0));
    layer.SetSize(new Vector2(1920.0, 1080.0));
    layer.SetInteractive(false);
    layer.Reparent(parent);

    this.m_csMapRoot = layer;

    if this.m_csMapPerUnit <= 0.0 {
        this.m_csMapPerUnit = this.MpCsPixelsPerUnit();
    }

    this.MpCsMapDraw();
}

@addMethod(SingleplayerMenuGameController)
public func MpCsMapRefresh() -> Void {
    if IsDefined(this.m_csMapRoot) {
        this.m_csMapRoot.RemoveAllChildren();
        this.MpCsMapDraw();
    }
}

@addMethod(SingleplayerMenuGameController)
public func MpCsMapDraw() -> Void {
    let c = this.m_csMapRoot;

    if !IsDefined(c) {
        return;
    }

    this.MpCsMapChip(c, MpCsMapChipView(), this.m_csMapHidden ? "MAP OFF" : "MAP ON", !this.m_csMapHidden);

    // Hidden means hidden: the design has to be judgeable with nothing of ours on top of it.
    if this.m_csMapHidden {
        return;
    }

    this.MpCsMapChip(c, MpCsMapChipMeasure(), this.m_csMapMeasure ? "MEASURE ON" : "MEASURE OFF", this.m_csMapMeasure);

    let cyan = MpCsMapCyan();
    let root = this.GetRootCompoundWidget().GetSize();

    MpCsMapNote(c, 934.0, 13.0,
                s"DISPLAY \(this.MpCsMapDisplay())   \(FloatToStringPrec(this.m_csMapPerUnit, 3)) PX PER UNIT   ROOT \(MpCsMapInt(root.X))x\(MpCsMapInt(root.Y))",
                cyan);

    // Measure swallows clicks, and a screen that silently stops pressing looks broken.
    if this.m_csMapMeasure {
        MpCsMapTag(c, 768.0, 44.0, "MEASURING: CLICKS PRESS NOTHING. CLICK TWO CORNERS.", MpCsInk());
    }

    this.MpCsMapRulers(c);
    this.MpCsMapRegions(c);
    this.MpCsMapTargets(c);
    this.MpCsMapMeasured(c);
    this.MpCsMapLastClick(c);
}

@addMethod(SingleplayerMenuGameController)
public func MpCsMapChip(parent: ref<inkCanvas>, box: array<Float>, label: String, lit: Bool) -> Void {
    let cyan = MpCsMapCyan();
    let w = box[2] - box[0];
    let h = box[3] - box[1];

    MpCsRect(parent, box[0], box[1], w, h, lit ? cyan : MpCsVoid(), lit ? 1.0 : 0.86);
    MpCsMapFrame(parent, box[0], box[1], w, h, cyan);

    let text = MpCsText(parent, box[0] + w / 2.0, box[1] + h / 2.0, label, 16, n"Bold", lit ? MpCsVoid() : cyan);

    MpCsAlign(text, 0.5, 0.5);
}

/**
 * Ticks every 100 units on the bottom and right edges, numbered every 200, so a screenshot can
 * be read for coordinates without anybody clicking anything.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsMapRulers(parent: ref<inkCanvas>) -> Void {
    let cyan = MpCsMapCyan();
    let i = 1;

    while i < 20 {
        let x = Cast<Float>(i) * 100.0;
        let major = i % 2 == 0;
        let length = major ? 16.0 : 9.0;

        MpCsRect(parent, x - 1.0, 1080.0 - length, 2.0, length, cyan, 1.0);

        if major {
            MpCsMapNote(parent, x - 22.0, 1038.0, IntToString(i * 100), cyan);
        }

        i += 1;
    }

    i = 1;

    while i < 11 {
        let y = Cast<Float>(i) * 100.0;
        let major = i % 2 == 0;
        let length = major ? 16.0 : 9.0;

        MpCsRect(parent, 1920.0 - length, y - 1.0, length, 2.0, cyan, 1.0);

        if major {
            MpCsMapNote(parent, 1852.0, y - 11.0, IntToString(i * 100), cyan);
        }

        i += 1;
    }
}

/**
 * Every region MpCsHitAt tests. The roster is one box with the lines inside it that decide
 * between cards: nearest centre wins, so the real edge of a card is halfway to its neighbour,
 * and on this design those halfway lines lean with the rows.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsMapRegions(parent: ref<inkCanvas>) -> Void {
    let cyan = MpCsMapCyan();
    let roster = MpCsHitRoster();
    let left = roster[0];
    let right = roster[2];

    MpCsMapBox(parent, MpCsHitEnter(), "ENTER", 828.0);
    MpCsMapBox(parent, MpCsHitDelete(), "DELETE", 810.0);
    MpCsMapBox(parent, MpCsHitSelect(), "SELECT", 810.0);
    MpCsMapBox(parent, roster, "CARDS", 125.0);

    let slot = 0;

    while slot < MpCsMaxSlots() {
        let top = MpCsCardFirst() + Cast<Float>(slot) * MpCsCardPitch();
        let centreRight = MpCsCardCentre(slot, right);

        MpCsLine(parent, left, MpCsCardCentre(slot, left), right, centreRight, 1.5, cyan, 0.6);
        MpCsMapTag(parent, right + 6.0, centreRight - 11.0,
                   s"CARD \(slot + 1)  Y \(MpCsMapInt(top))-\(MpCsMapInt(top + MpCsCardHeight()))", cyan);

        if slot + 1 < MpCsMaxSlots() {
            let fromY = (MpCsCardCentre(slot, left) + MpCsCardCentre(slot + 1, left)) / 2.0;
            let toY = (MpCsCardCentre(slot, right) + MpCsCardCentre(slot + 1, right)) / 2.0;

            MpCsLine(parent, left, fromY, right, toY, 3.0, cyan, 1.0);
        }

        slot += 1;
    }
}

/**
 * Two exact points. Reading = a * authored + b, so clicking both solves scale and offset for an
 * axis with nothing left to interpret, which a card-sized target cannot do.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsMapTargets(parent: ref<inkCanvas>) -> Void {
    let ink = MpCsInk();

    MpCsMapCross(parent, 960.0, 270.0, 18.0, 3.0, ink);
    MpCsMapNote(parent, 992.0, 259.0, "A  960, 270", ink);
    MpCsMapCross(parent, 960.0, 810.0, 18.0, 3.0, ink);
    MpCsMapNote(parent, 992.0, 799.0, "B  960, 810", ink);
}

@addMethod(SingleplayerMenuGameController)
public func MpCsMapMeasured(parent: ref<inkCanvas>) -> Void {
    if this.m_csMapCorners < 1 {
        return;
    }

    let ink = MpCsInk();

    MpCsMapCross(parent, this.m_csMapFirstX, this.m_csMapFirstY, 14.0, 3.0, ink);

    if this.m_csMapCorners < 2 {
        return;
    }

    let x = MinF(this.m_csMapFirstX, this.m_csMapX);
    let y = MinF(this.m_csMapFirstY, this.m_csMapY);
    let w = AbsF(this.m_csMapX - this.m_csMapFirstX);
    let h = AbsF(this.m_csMapY - this.m_csMapFirstY);

    MpCsRect(parent, x, y, w, h, ink, 0.1);
    MpCsMapFrame(parent, x, y, w, h, ink);
    MpCsMapTag(parent, x, y - 24.0, s"BOX  X \(MpCsMapInt(x))  Y \(MpCsMapInt(y))  W \(MpCsMapInt(w))  H \(MpCsMapInt(h))", ink);
}

/**
 * Where the game says the last click was. The cursor is where it was pressed; any gap between
 * the two is the correction, visible rather than worked out.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsMapLastClick(parent: ref<inkCanvas>) -> Void {
    if !this.m_csMapClicked {
        return;
    }

    let cyan = MpCsMapCyan();
    let x = this.m_csMapX;
    let y = this.m_csMapY;
    let first = s"\(MpCsHitName(this.m_csMapHit))  X \(MpCsMapInt(x))  Y \(MpCsMapInt(y))";
    let second = s"DISPLAY \(MpCsMapInt(this.m_csMapPixelX)), \(MpCsMapInt(this.m_csMapPixelY))";
    let w = MaxF(MpCsMapTextWidth(first), MpCsMapTextWidth(second));
    let tagX = x + 18.0;
    let tagY = y + 18.0;

    // Flip rather than clamp: a clamped readout would slide over the very point it describes.
    if tagX + w > 1910.0 {
        tagX = x - 18.0 - w;
    }

    if tagY + 46.0 > 1054.0 {
        tagY = y - 18.0 - 46.0;
    }

    MpCsMapCross(parent, x, y, 56.0, 4.0, cyan);
    MpCsMapTag(parent, tagX, tagY, first, cyan);
    MpCsMapNote(parent, tagX, tagY + 24.0, second, cyan);
}

/**
 * Sees every click before the screen does. True means the click was for the tool and goes no
 * further; false means the screen handles it as usual and the crosshair just records it.
 */
@addMethod(SingleplayerMenuGameController)
public func MpCsMapTake(x: Float, y: Float, pixelX: Float, pixelY: Float) -> Bool {
    if !this.MpCsMapAllowed() {
        return false;
    }

    if MpCsInBox(x, y, MpCsMapChipView()) {
        this.m_csMapHidden = !this.m_csMapHidden;

        // Measure swallows clicks. Left on behind a hidden map, the screen would look dead.
        if this.m_csMapHidden {
            this.m_csMapMeasure = false;
        }

        this.MpCsMapRefresh();
        return true;
    }

    if !this.m_csMapHidden && MpCsInBox(x, y, MpCsMapChipMeasure()) {
        this.m_csMapMeasure = !this.m_csMapMeasure;
        this.m_csMapCorners = 0;
        this.MpCsMapRefresh();
        return true;
    }

    this.m_csMapClicked = true;
    this.m_csMapX = x;
    this.m_csMapY = y;
    this.m_csMapPixelX = pixelX;
    this.m_csMapPixelY = pixelY;
    this.m_csMapHit = MpCsHitAt(x, y);

    MpCsLog(s"click map: \(MpCsHitName(this.m_csMapHit)) at \(MpCsMapInt(x)),\(MpCsMapInt(y))");

    if !this.m_csMapMeasure {
        return false;
    }

    if this.m_csMapCorners != 1 {
        this.m_csMapFirstX = x;
        this.m_csMapFirstY = y;
        this.m_csMapCorners = 1;
    } else {
        this.m_csMapCorners = 2;

        MpCsLog(s"click map: box x=\(MpCsMapInt(MinF(this.m_csMapFirstX, x))) y=\(MpCsMapInt(MinF(this.m_csMapFirstY, y))) w=\(MpCsMapInt(AbsF(x - this.m_csMapFirstX))) h=\(MpCsMapInt(AbsF(y - this.m_csMapFirstY)))");
    }

    this.MpCsMapRefresh();
    return true;
}
