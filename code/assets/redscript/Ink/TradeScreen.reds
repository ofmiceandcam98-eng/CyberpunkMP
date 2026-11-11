// THE TRADE PANEL - the view model and the render.
//
// WHAT CHANGED ON 2026-11-10, AND WHY. Until then this file drew MOCK data on the chat HUD and
// nothing on it could be clicked: a HUD layer has no cursor at all, and every attempt to raise
// one there (a modal context push, SetCursorVisibility) either did nothing or broke chat typing.
// Now:
//
//   - The panel is drawn inside a Codeware CustomPopup (TradePopup.reds) - an in-game
//     notification with isBlocking + useCursor, the same mechanism the game uses for the car
//     colour picker and the frame switcher. That is a real cursor over the running world with no
//     menu screen, no slow-mo and no blur: the panel sits where it always sat, beside chat.
//   - Everything it shows is the server's NotifyTrade (MpTrViewFromNetwork), and everything it
//     does is an existing /trade chat command (ChatController.MpTrAct). The panel holds no deal
//     of its own - the session in the server's TradeStore is the only one, which is what keeps a
//     duplicated click from becoming a duplicated item.
//   - When the mouse is given back (Esc, or HIDE) the trade carries on and a small chip beside
//     chat says so; Enter brings the panel back. Chat stays live the whole time.
//   - The look is the selector's design-D family, picked by zeldfep from a proposal the same day
//     ("I like the look minus the edge hatch, build it"): a stepped silhouette, two module bays,
//     an exchange spine, and confirmation wires into the launcher kit's plate. See "the render".
//
// Mockup and the running notes: /mnt/vol/projects/_internal-docs/trade-screen.html (internal).
// The rules this follows are docs/DESIGN-LANGUAGE.md "In-game ink screens".

module CyberpunkMP.Ink

import CyberpunkMP.*
import CyberpunkMP.World.*

// ============================================================================ palette
//
// From the mockup's :root, converted to HDRColor. Cyan is the trade accent (the selector's is
// gold); red is the shared warning colour; gold marks quantities and money. Kept at or just above
// 1.0 - this panel is see-through over live gameplay, and bloomed colour is illegible there.

public func MpTrCyan() -> HDRColor = new HDRColor(0.24, 0.91, 0.94, 1.0)      // #3ce8ef
public func MpTrCyanDim() -> HDRColor = new HDRColor(0.11, 0.43, 0.47, 1.0)   // #1d6e77
public func MpTrRed() -> HDRColor = new HDRColor(0.96, 0.22, 0.29, 1.0)       // #f4374a
public func MpTrRedDim() -> HDRColor = new HDRColor(0.48, 0.12, 0.17, 1.0)    // #7a1f2c
public func MpTrGold() -> HDRColor = new HDRColor(0.96, 0.82, 0.20, 1.0)      // #f5d033
public func MpTrInk() -> HDRColor = new HDRColor(0.91, 0.93, 0.95, 1.0)       // #e8eef2
public func MpTrInkDim() -> HDRColor = new HDRColor(0.62, 0.67, 0.72, 1.0)    // #9dabb7
public func MpTrInkFaint() -> HDRColor = new HDRColor(0.43, 0.47, 0.52, 1.0)  // #6d7986
public func MpTrPanel() -> HDRColor = new HDRColor(0.063, 0.047, 0.086, 1.0)  // panel fill
public func MpTrVoid() -> HDRColor = new HDRColor(0.031, 0.031, 0.047, 1.0)   // #08080c
// The 2026-11-10 look's additions: the kit plate's base, the dim half of the eddie readout, the
// hairline between parts, and a firmer panel ground (no blur - text has to hold on its own).
public func MpTrCyanDeep() -> HDRColor = new HDRColor(0.043, 0.196, 0.216, 1.0) // #0b3237
public func MpTrGoldDim() -> HDRColor = new HDRColor(0.42, 0.353, 0.09, 1.0)    // #6b5a17
public func MpTrLine() -> HDRColor = new HDRColor(0.165, 0.227, 0.267, 1.0)     // #2a3a44
public func MpTrPanelDeep() -> HDRColor = new HDRColor(0.051, 0.039, 0.071, 1.0) // #0d0a12
// The rail arrow's base under its stripes - the launcher's own value for the same arrow.
public func MpTrGoldBase() -> HDRColor = new HDRColor(0.847, 0.682, 0.043, 1.0)  // #d8ae0b

// ============================================================================ geometry
//
// Authored in 1920x1080 units, like every ink screen here. The panel's own box:
public func MpTrPanelX() -> Float = 690.0
public func MpTrPanelY() -> Float = 200.0
public func MpTrPanelW() -> Float = 1120.0
public func MpTrPanelH() -> Float = 600.0

// zeldfep's tuned size from the v2 overlay (2026-09-14), kept as the meaning of "zoom" so
// /trbig and /trsmall still read the same.
public func MpTrZoom() -> Float = 2.6

/**
 * Where the panel sits, as a FRACTION OF THE SCREEN (the popup root), top-left corner.
 *
 * Derived, not guessed: the v2 overlay was placed at 1.04 and 0.12 of the chat root, which
 * measured 1000x1000 in zeldfep's and Cam's logs (2026-11-09, "[TradeScreen] opened - root
 * 1000x1000"). With the chat box anchored at margin (60, 0, 0, 200) on a 3840x2160 HUD, that
 * corner is (1100, 1080) - 0.2865 and 0.5 of the screen. The 3840-wide HUD is the one number
 * here that is inferred rather than read; "[TradeScreen] panel at" logs the real rect, and
 * /tr* nudges plus /trsave correct it in game without a ship.
 */
public func MpTrScreenFracX() -> Float = 0.2865
public func MpTrScreenFracY() -> Float = 0.5

/**
 * Canvas scale in the popup for a root this wide.
 *
 * The v2 overlay scaled by (chat root 1000 / 1920) x zoom on a 3840-wide HUD, so the panel
 * spanned 1120 x 1.354 / 3840 of the screen. Keeping that ratio on any root:
 * rootWidth x zoom / (1920 x 3840 / 1000) = rootWidth x zoom / 7372.8.
 */
public func MpTrScaleFor(rootWidth: Float, zoom: Float) -> Float = rootWidth * zoom / 7372.8

// ============================================================================ the view model
//
// One snapshot of the trade, built from NotifyTrade (or the /tradeui preview). Rebuilt whole on
// every notification, never patched - the same rule as the native mirror under it.

public class MpTrItem {
    public let id: Uint64;
    public let qty: Int32;        // held (a holding) or offered (an offer row)
    public let offered: Int32;    // holdings only: how many of it are in my offer now
    public let name: String;
    public let kind: String;
}

public class MpTrView {
    public let state: Uint32;     // TradeState from the server's TradeStore.h
    public let live: Bool;
    public let mock: Bool;
    public let partner: String;
    public let incoming: Bool;
    public let version: Uint32;
    public let myMoney: Int32;
    public let theirMoney: Int32;
    public let myConfirmed: Bool;
    public let theirConfirmed: Bool;
    public let available: Int32;
    public let mine: array<ref<MpTrItem>>;
    public let theirs: array<ref<MpTrItem>>;
    public let holdings: array<ref<MpTrItem>>;
    public let reason: String;
    public let session: String;   // the server's trade id, for the label line
}

// Interaction state the server does not hold: scroll positions, an amount sent and not yet
// echoed (-1 for none - ChatController.MpTrUiNow sets it, since a field default cannot be
// negative), and a line of feedback.
public class MpTrUi {
    public let listTop: Int32;
    public let theirTop: Int32;
    public let pending: Int32;
    public let inFlight: Int32;
    public let notice: String;
    // The BLUR switch on the label line: the game's own popup blur behind the panel, and a 60% fill
    // instead of 72% - the render's "Game blur". A TRIAL, so zeldfep can judge it in game against
    // the plain fill (2026-11-11); kept for the whole game session, off at start.
    public let blur: Bool;
}

/**
 * An item's name and kind, from its TweakDBID, CLIENT-side.
 *
 * The server holds numbers on purpose (CharacterRecord.h: names are a client-side convenience),
 * so the panel resolves them. A record that does not resolve still gets a row - hiding it would
 * hide something the player is about to hand over.
 */
public func MpTrMakeItem(net: ref<NetworkWorldSystem>, id: Uint64, qty: Int32) -> ref<MpTrItem> {
    let item = new MpTrItem();
    item.id = id;
    item.qty = qty;
    item.name = "Unknown item";
    item.kind = "Item";

    let rec = TweakDBInterface.GetItemRecord(net.TdbidFromNumber(id));

    if IsDefined(rec) {
        let name = GetLocalizedTextByKey(rec.DisplayName());
        if StrLen(name) > 0 {
            item.name = name;
        }

        let type = rec.ItemType();
        if IsDefined(type) {
            let kind = GetLocalizedTextByKey(type.LocalizedType());
            if StrLen(kind) > 0 {
                item.kind = kind;
            }
        }
    }

    return item;
}

/**
 * Things the panel never offers to trade, even though the server would take them.
 *
 * CYBERWARE: removing a traded item from the giver's game is RemoveItemByTDBID, and on installed
 * chrome that is surgery the player did not ask for - the strip code already learned that
 * taking what sits in an arm slot leaves a character with no arms. QUEST items: handing one
 * over can break the quest it belongs to. Eddies are the stepper's job, not a row.
 */
public func MpTrNotTradeable(net: ref<NetworkWorldSystem>, id: Uint64) -> Bool {
    let tdbid = net.TdbidFromNumber(id);

    if Equals(tdbid, ItemID.GetTDBID(MarketSystem.Money())) {
        return true;
    }

    let rec = TweakDBInterface.GetItemRecord(tdbid);

    if IsDefined(rec) {
        if rec.TagsContains(n"Cyberware") || rec.TagsContains(n"Quest") {
            return true;
        }
    }

    return false;
}

public func MpTrViewFromNetwork(net: ref<NetworkWorldSystem>) -> ref<MpTrView> {
    let v = new MpTrView();
    v.state = net.GetTradeState();
    v.live = net.IsTradeLive();
    v.partner = net.GetTradePartner();
    v.incoming = net.IsTradeIncoming();
    v.version = net.GetTradeVersion();
    v.myMoney = net.GetTradeMoney(true);
    v.theirMoney = net.GetTradeMoney(false);
    v.myConfirmed = net.IsTradeConfirmed(true);
    v.theirConfirmed = net.IsTradeConfirmed(false);
    v.available = net.GetTradeAvailableMoney();
    v.reason = net.GetTradeReason();
    v.session = net.GetTradeId();

    let n = net.GetTradeItemCount(true);
    let i: Uint32 = 0u;
    while i < n {
        ArrayPush(v.mine, MpTrMakeItem(net, net.GetTradeItemId(true, i),
                                       Cast<Int32>(net.GetTradeItemQuantity(true, i))));
        i += 1u;
    }

    n = net.GetTradeItemCount(false);
    i = 0u;
    while i < n {
        ArrayPush(v.theirs, MpTrMakeItem(net, net.GetTradeItemId(false, i),
                                         Cast<Int32>(net.GetTradeItemQuantity(false, i))));
        i += 1u;
    }

    n = net.GetTradeHoldingCount();
    i = 0u;
    while i < n {
        let id = net.GetTradeHoldingId(i);

        if !MpTrNotTradeable(net, id) {
            let item = MpTrMakeItem(net, id, Cast<Int32>(net.GetTradeHoldingQuantity(i)));
            let k = 0;
            while k < ArraySize(v.mine) {
                if v.mine[k].id == id {
                    item.offered = v.mine[k].qty;
                }
                k += 1;
            }
            ArrayPush(v.holdings, item);
        }

        i += 1u;
    }

    return v;
}

public func MpTrMockItem(id: Uint64, name: String, kind: String, qty: Int32, offered: Int32) -> ref<MpTrItem> {
    let item = new MpTrItem();
    item.id = id;
    item.name = name;
    item.kind = kind;
    item.qty = qty;
    item.offered = offered;
    return item;
}

/**
 * The /tradeui preview: the panel with made-up contents, so placement and the mouse can be
 * checked by one person with nobody to trade with. Its clicks change only this copy - nothing is
 * sent - and it says PREVIEW in its header so it cannot be mistaken for a real deal.
 */
public func MpTrMockView() -> ref<MpTrView> {
    let v = new MpTrView();
    v.state = 1u;
    v.live = true;
    v.mock = true;
    v.partner = "Noremac";
    v.session = "7f3a91c2e04b55d1";
    v.available = 20100;
    v.theirMoney = 2500;
    v.theirConfirmed = true;
    ArrayPush(v.holdings, MpTrMockItem(1ul, "Nekomata", "Precision Rifle", 1, 1));
    ArrayPush(v.holdings, MpTrMockItem(2ul, "MaxDoc Mk.1", "Consumable", 12, 0));
    ArrayPush(v.holdings, MpTrMockItem(3ul, "Scrap Electronics", "Crafting Material", 37, 0));
    ArrayPush(v.holdings, MpTrMockItem(4ul, "Kang Tao G-58 Dian", "Smart SMG", 1, 0));
    ArrayPush(v.holdings, MpTrMockItem(5ul, "Bounce Back Mk.2", "Consumable", 4, 0));
    ArrayPush(v.holdings, MpTrMockItem(6ul, "Militech Paraline", "Cyberdeck", 1, 0));
    ArrayPush(v.holdings, MpTrMockItem(7ul, "Rifle Ammo", "Ammo", 180, 0));
    ArrayPush(v.holdings, MpTrMockItem(8ul, "Biker Jacket", "Outer Torso", 1, 0));
    ArrayPush(v.mine, MpTrMockItem(1ul, "Nekomata", "Precision Rifle", 1, 0));
    ArrayPush(v.theirs, MpTrMockItem(11ul, "Militech M-10AF Lexington", "Pistol", 1, 0));
    ArrayPush(v.theirs, MpTrMockItem(12ul, "MaxDoc Mk.2", "Consumable", 6, 0));
    ArrayPush(v.theirs, MpTrMockItem(13ul, "Kiroshi Optics Mk.1", "Clothing", 1, 0));
    return v;
}

// ============================================================================ hit regions
//
// Every clickable thing is registered here in the SAME authored units it is drawn in, so drawing
// and hit-testing share one space (the design-language rule that cost the selector three builds).
// In the popup each region also becomes a transparent interactive rectangle on top of everything
// else, which is what gives the cursor its hover glow; the list is also kept for the coordinate
// fallback in case a widget never reports a click.

public class MpTrHit {
    public let id: Int32;
    public let x: Float;
    public let y: Float;
    public let w: Float;
    public let h: Float;
}

public class MpTrHits {
    public let list: array<ref<MpTrHit>>;
    // The rails drawn this time round (MpTrDrawRail) - a drag is not a click, so they are kept
    // apart from the click regions.
    public let rails: array<ref<MpTrRailMap>>;

    public func Clear() -> Void {
        ArrayClear(this.list);
        ArrayClear(this.rails);
    }

    // The rail whose column is at (x, y); -1 for none.
    public func RailAt(x: Float, y: Float) -> Int32 {
        let i = 0;
        while i < ArraySize(this.rails) {
            let r = this.rails[i];
            if x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h {
                return i;
            }
            i += 1;
        }
        return -1;
    }

    // The rail whose LIST is under (x, y), for the wheel; -1 for none.
    public func ListAt(x: Float, y: Float) -> Int32 {
        let i = 0;
        while i < ArraySize(this.rails) {
            let r = this.rails[i];
            if x >= r.ax && x <= r.ax + r.aw && y >= r.ay && y <= r.ay + r.ah {
                return i;
            }
            i += 1;
        }
        return -1;
    }

    public func Rail(list: Int32) -> ref<MpTrRailMap> {
        let i = 0;
        while i < ArraySize(this.rails) {
            if this.rails[i].list == list {
                return this.rails[i];
            }
            i += 1;
        }
        return null;
    }

    public func Add(id: Int32, x: Float, y: Float, w: Float, h: Float) -> Void {
        let hit = new MpTrHit();
        hit.id = id;
        hit.x = x;
        hit.y = y;
        hit.w = w;
        hit.h = h;
        ArrayPush(this.list, hit);
    }

    // Latest first: a quantity button drawn over its row wins over the row.
    public func At(x: Float, y: Float) -> Int32 {
        let i = ArraySize(this.list) - 1;
        while i >= 0 {
            let hit = this.list[i];
            if x >= hit.x && x <= hit.x + hit.w && y >= hit.y && y <= hit.y + hit.h {
                return hit.id;
            }
            i -= 1;
        }
        return -1;
    }
}

// Resting opacity of a hit rectangle: invisible to the eye, but not zero - in case ink skips a
// fully transparent widget when it hit-tests, which would leave every button deaf.
public func MpTrHitRest() -> Float = 0.01

// The interactive rectangles, drawn LAST so they sit over every label. Next to transparent until
// the cursor is on one (the popup turns the hovered one up to a faint cyan wash).
public func MpTrDrawHits(c: ref<inkCanvas>, hits: ref<MpTrHits>, owner: ref<IScriptable>) -> Void {
    let i = 0;
    while i < ArraySize(hits.list) {
        let hit = hits.list[i];
        let r = MpCsRect(c, hit.x, hit.y, hit.w, hit.h, MpTrCyan(), MpTrHitRest());
        r.SetName(StringToName(s"mptr_\(hit.id)"));
        r.SetInteractive(true);
        r.RegisterToCallback(n"OnRelease", owner, n"OnMpTrHitRelease");
        r.RegisterToCallback(n"OnHoverOver", owner, n"OnMpTrHitOver");
        r.RegisterToCallback(n"OnHoverOut", owner, n"OnMpTrHitOut");
        i += 1;
    }
}

// "mptr_12" back to 12; -1 for anything else.
public func MpTrHitId(widget: wref<inkWidget>) -> Int32 {
    if !IsDefined(widget) {
        return -1;
    }
    let name = NameToString(widget.GetName());
    if !StrBeginsWith(name, "mptr_") {
        return -1;
    }
    return StringToInt(StrMid(name, 5));
}

// Region ids. Rows and their quantity buttons add the holding's index.
public func MpTrIdHide() -> Int32 = 1
public func MpTrIdCancel() -> Int32 = 2
public func MpTrIdConfirm() -> Int32 = 3
public func MpTrIdBlur() -> Int32 = 4            // the BLUR trial switch (MpTrUi.blur)
public func MpTrIdMoney() -> Int32 = 10          // 10..15: -1000 -100 -1 +1 +100 +1000
// 20..23 were UP and DN on each side, replaced by the rails (MpTrDrawRail) on 2026-11-11.
public func MpTrIdRow() -> Int32 = 100
public func MpTrIdLess() -> Int32 = 1000
public func MpTrIdMore() -> Int32 = 2000

public func MpTrMoneyStep(id: Int32) -> Int32 {
    switch id - MpTrIdMoney() {
        case 0: return -1000;
        case 1: return -100;
        case 2: return -1;
        case 3: return 1;
        case 4: return 100;
        default: return 1000;
    }
}

// ============================================================================ small helpers

// 20100 -> "20,100". Eddies are read at a glance across a room; the separator is the glance.
public func MpTrFmt(n: Int32) -> String {
    let digits = ToString(n < 0 ? -n : n);
    let out = n < 0 ? "-" : "";
    let len = StrLen(digits);
    let i = 0;
    while i < len {
        if i > 0 && (len - i) % 3 == 0 {
            out += ",";
        }
        out += StrMid(digits, i, 1);
        i += 1;
    }
    return out;
}

// Long names are cut rather than wrapped: a wrapped row pushes the next one out of its hit box.
public func MpTrClip(text: String, max: Int32) -> String {
    if StrLen(text) <= max {
        return text;
    }
    return StrLeft(text, max - 3) + "...";
}

// "7f3a91c2e04b55d1" -> "7F3A-91C2". The label line names the real trade, not a made-up number.
public func MpTrSession(id: String) -> String {
    if StrLen(id) < 8 {
        return "----";
    }
    return StrUpper(StrLeft(id, 4)) + "-" + StrUpper(StrMid(id, 4, 4));
}

// Text hung from its own centre, left middle or right middle - MpCsAlign, the selector's proven
// way. zeldfep, 2026-11-10, on the proposal page: "some of these aren't centered". Offsets
// estimated from character counts drift with the font's real widths; a text hung from a fraction
// of its own size cannot.
//
// Vertically, cy is where the middle of the CAPITALS lands, not the middle of the font's box.
// zeldfep, 2026-11-11, on UP/DN and the title tab: "still not centered". The box runs from the
// font's ascent to its descent, and the descent hangs below the capitals, so hanging the box from
// its middle seats Rajdhani capitals 0.035 of the size high and Orbitron 0.025 low. Measured from
// the fonts themselves (per 100: Rajdhani ascent 93, descent 35, capitals 65; Orbitron 101, 24,
// 72). That ink's box is ascent-to-descent is INFERRED - a screenshot in game settles it.
public func MpTrCapDrop(size: Int32) -> Float {
    return 0.035 * Cast<Float>(size);
}

public func MpTrCapDropWide(size: Int32) -> Float {
    return -0.025 * Cast<Float>(size);
}

public func MpTrTextC(p: ref<inkCompoundWidget>, cx: Float, cy: Float, text: String, size: Int32,
                      style: CName, color: HDRColor) -> ref<inkText> {
    let t = MpCsText(p, cx, cy + MpTrCapDrop(size), text, size, style, color);
    MpCsAlign(t, 0.5, 0.5);
    return t;
}

public func MpTrTextL(p: ref<inkCompoundWidget>, x: Float, cy: Float, text: String, size: Int32,
                      style: CName, color: HDRColor) -> ref<inkText> {
    let t = MpCsText(p, x, cy + MpTrCapDrop(size), text, size, style, color);
    MpCsAlign(t, 0.0, 0.5);
    return t;
}

public func MpTrTextR(p: ref<inkCompoundWidget>, x: Float, cy: Float, text: String, size: Int32,
                      style: CName, color: HDRColor) -> ref<inkText> {
    let t = MpCsText(p, x, cy + MpTrCapDrop(size), text, size, style, color);
    MpCsAlign(t, 1.0, 0.5);
    return t;
}

// The wide face hung from its left middle - the title in the tab and on the chip.
public func MpTrWideL(p: ref<inkCompoundWidget>, x: Float, cy: Float, text: String, size: Int32,
                      style: CName, color: HDRColor) -> ref<inkText> {
    let t = MpCsText(p, x, cy + MpTrCapDropWide(size), text, size, style, color);
    MpCsAlign(t, 0.0, 0.5);
    return MpTrWide(t, style);
}

// The wide display face (Orbitron, shipped with the game) - for the title and the partner's name.
public func MpTrWide(t: ref<inkText>, style: CName) -> ref<inkText> {
    t.SetFontFamily(MpCsFontWide());
    t.SetFontStyle(style);
    return t;
}

// An outline as one run of segments through a flat x,y list - how a silhouette that is not convex
// gets its edge, since MpCsPolyEdge closes a single convex shape.
public func MpTrRun(p: ref<inkCompoundWidget>, pts: array<Float>, thick: Float, color: HDRColor, op: Float) -> Void {
    let i = 0;
    while i + 3 < ArraySize(pts) {
        MpCsLine(p, pts[i], pts[i + 1], pts[i + 2], pts[i + 3], thick, color, op);
        i += 2;
    }
}

// A convex four-sided shape, corners in order round the edge.
public func MpTrQuad(p: ref<inkCompoundWidget>, x1: Float, y1: Float, x2: Float, y2: Float, x3: Float,
                     y3: Float, x4: Float, y4: Float, color: HDRColor, op: Float) -> Void {
    let xs: array<Float> = [x1, x2, x3, x4];
    let ys: array<Float> = [y1, y2, y3, y4];
    MpCsPolyFill(p, xs, ys, color, op);
}

// Registration corners: the launcher's bracket button, a label inside four corners and no plate.
public func MpTrCorners(p: ref<inkCompoundWidget>, x: Float, y: Float, w: Float, h: Float, len: Float,
                        color: HDRColor, op: Float) -> Void {
    MpCsRect(p, x, y, len, 2.0, color, op);
    MpCsRect(p, x, y, 2.0, len, color, op);
    MpCsRect(p, x + w - len, y, len, 2.0, color, op);
    MpCsRect(p, x + w - 2.0, y, 2.0, len, color, op);
    MpCsRect(p, x, y + h - 2.0, len, 2.0, color, op);
    MpCsRect(p, x, y + h - len, 2.0, len, color, op);
    MpCsRect(p, x + w - len, y + h - 2.0, len, 2.0, color, op);
    MpCsRect(p, x + w - 2.0, y + h - len, 2.0, len, color, op);
}

// Steep 2:1 hatch, n whole stripes leaning right. On the trade panel this is ONLY the CONFIRM
// plate's block - the design language's "this is the primary control" stripe. zeldfep, 2026-11-10:
// the look "minus the edge hatch", so the frame carries none.
public func MpTrHatch(p: ref<inkCompoundWidget>, x: Float, y: Float, h: Float, n: Int32, sw: Float,
                      gap: Float, color: HDRColor, op: Float) -> Void {
    let i = 0;
    while i < n {
        let a = x + Cast<Float>(i) * (sw + gap);
        MpTrQuad(p, a, y + h, a + h / 2.0, y, a + h / 2.0 + sw, y, a + sw, y + h, color, op);
        i += 1;
    }
}

// Chevrons, each as two convex arms. Left-pointing is the kit plate's trailing mark.
public func MpTrChevL(p: ref<inkCompoundWidget>, x: Float, y: Float, s: Float, color: HDRColor, op: Float) -> Void {
    MpTrQuad(p, x, y + s * 0.5, x + s * 0.55, y, x + s * 0.85, y, x + s * 0.3, y + s * 0.5, color, op);
    MpTrQuad(p, x, y + s * 0.5, x + s * 0.3, y + s * 0.5, x + s * 0.85, y + s, x + s * 0.55, y + s, color, op);
}

public func MpTrChevR(p: ref<inkCompoundWidget>, x: Float, y: Float, s: Float, color: HDRColor, op: Float) -> Void {
    MpTrQuad(p, x + s * 0.85, y + s * 0.5, x + s * 0.3, y, x, y, x + s * 0.55, y + s * 0.5, color, op);
    MpTrQuad(p, x + s * 0.85, y + s * 0.5, x + s * 0.55, y + s * 0.5, x, y + s, x + s * 0.3, y + s, color, op);
}

// A pointy-top hexagon about (cx, cy): the exchange emblem.
public func MpTrHex(p: ref<inkCompoundWidget>, cx: Float, cy: Float, r: Float, fill: HDRColor, fillOp: Float,
                    edge: HDRColor) -> Void {
    let xs: array<Float> = [cx, cx + r * 0.866, cx + r * 0.866, cx, cx - r * 0.866, cx - r * 0.866];
    let ys: array<Float> = [cy - r, cy - r * 0.5, cy + r * 0.5, cy + r, cy + r * 0.5, cy - r * 0.5];
    MpCsPolyFill(p, xs, ys, fill, fillOp);
    MpCsPolyEdge(p, xs, ys, 2.0, edge, 1.0);
}

// A wire's end node: a small diamond.
public func MpTrNode(p: ref<inkCompoundWidget>, cx: Float, cy: Float, fill: HDRColor, fillOp: Float, edge: HDRColor) -> Void {
    let xs: array<Float> = [cx, cx + 5.0, cx, cx - 5.0];
    let ys: array<Float> = [cy - 5.0, cy, cy + 5.0, cy];
    MpCsPolyFill(p, xs, ys, fill, fillOp);
    MpCsPolyEdge(p, xs, ys, 1.5, edge, 1.0);
}

// A row of square pips; the first `lit` take the lit colour.
public func MpTrPips(p: ref<inkCompoundWidget>, x: Float, y: Float, n: Int32, s: Float, gap: Float,
                     color: HDRColor, lit: Int32, litColor: HDRColor) -> Void {
    let i = 0;
    while i < n {
        MpCsRect(p, x + Cast<Float>(i) * (s + gap), y, s, s, i < lit ? litColor : color, i < lit ? 1.0 : 0.55);
        i += 1;
    }
}

// The launcher kit's plate, the screen's one filled control: offset base, solid cut-corner plate,
// raised tab with four dots, a hatch block, the label, trailing chevrons. `lit` false is the
// locked-in look - a darker plate whose label and marks carry the light instead.
public func MpTrPlate(p: ref<inkCompoundWidget>, x: Float, y: Float, label: String, lit: Bool, dots: Int32) -> Void {
    let w = 262.0;
    let h = 52.0;
    let tabX = x + 40.0;
    let body = lit ? MpTrCyan() : MpTrCyanDim();
    let mark = lit ? MpTrVoid() : MpTrCyan();
    MpCsPolyFill(p, MpCsCutXs(x + 6.0, w, 12.0), MpCsCutYs(y + 6.0, h, 12.0), MpTrCyanDeep(), 1.0);
    MpCsPolyFill(p, MpCsCutXs(x, w, 12.0), MpCsCutYs(y, h, 12.0), body, 1.0);
    MpTrQuad(p, tabX, y, tabX + 10.0, y - 12.0, tabX + 70.0, y - 12.0, tabX + 80.0, y, body, 1.0);
    MpTrPips(p, tabX + 16.0, y - 9.0, 4, 7.0, 5.0, MpTrCyanDeep(), dots, MpTrVoid());
    MpTrHatch(p, x + 16.0, y + 12.0, 28.0, 3, 5.0, 6.0, MpTrVoid(), 0.55);
    // A long label steps down a size: ink spaces letters by inserting spaces (MpCsSpaced), so the
    // only way to keep EXCHANGING clear of the chevrons is a smaller face.
    MpTrTextC(p, x + 138.0, y + h / 2.0, MpCsSpaced(label), StrLen(label) <= 8 ? 19 : 15, n"Bold", mark);
    MpTrChevL(p, x + w - 46.0, y + 18.0, 16.0, mark, 1.0);
    MpTrChevL(p, x + w - 30.0, y + 18.0, 16.0, mark, 1.0);
}

// The bracket button: label centred inside registration corners.
public func MpTrBracket(p: ref<inkCompoundWidget>, x: Float, y: Float, w: Float, h: Float, label: String,
                        size: Int32, color: HDRColor) -> Void {
    MpTrCorners(p, x, y, w, h, 10.0, color, 1.0);
    MpTrTextC(p, x + w / 2.0, y + h / 2.0, MpCsSpaced(label), size, n"Bold", color);
}

// A small bracket with a short label - the eddie steps, the stack steppers, scroll.
public func MpTrStep(p: ref<inkCompoundWidget>, x: Float, y: Float, w: Float, h: Float, label: String,
                     size: Int32, color: HDRColor) -> Void {
    MpTrCorners(p, x, y, w, h, 6.0, color, 1.0);
    MpTrTextC(p, x + w / 2.0, y + h / 2.0, label, size, n"Bold", color);
}

// ============================================================================ the render
//
// zeldfep, 2026-11-10, choosing the proposal ("I like the look minus the edge hatch, build it"):
// the panel as a piece of hardware in the selector's design-D family. A raised title tab and
// chamfers make the silhouette, a machine-label line names the channel, two module bays carry
// each side (cyan yours, red theirs), a hex spine sits between them, and two wires run from the
// bays into the kit plate, lighting as each side locks in. Drawn in the panel's authored box,
// (690, 200) and 1120x600, so the placement and the clicks are what they were.

public func MpTrFrame(c: ref<inkCanvas>, ox: Float, oy: Float, v: ref<MpTrView>, ui: ref<MpTrUi>,
                      hits: ref<MpTrHits>) -> Void {
    let W = 1120.0;
    let H = 600.0;
    // Fill: the body, the title tab and the bottom lug, side by side - each convex. Thinner with
    // the game's blur on, because the street behind is already soft (MpTrUi.blur).
    let fill = ui.blur ? 0.6 : 0.72;
    MpCsPolyFill(c, MpCsCutXs(ox, W, 30.0), MpCsCutYs(oy, H, 30.0), MpTrPanelDeep(), fill);
    MpTrQuad(c, ox + 24.0, oy, ox + 48.0, oy - 26.0, ox + 420.0, oy - 26.0, ox + 446.0, oy, MpTrPanelDeep(), fill);
    MpTrQuad(c, ox + 40.0, oy + H, ox + 58.0, oy + H + 18.0, ox + 470.0, oy + H + 18.0, ox + 488.0, oy + H, MpTrPanelDeep(), fill);
    // Edge: one run round the whole silhouette.
    let edge: array<Float> = [ox, oy, ox + 24.0, oy, ox + 48.0, oy - 26.0, ox + 420.0, oy - 26.0, ox + 446.0, oy,
                              ox + W - 30.0, oy, ox + W, oy + 30.0, ox + W, oy + H, ox + 488.0, oy + H,
                              ox + 470.0, oy + H + 18.0, ox + 58.0, oy + H + 18.0, ox + 40.0, oy + H,
                              ox + 30.0, oy + H, ox, oy + H - 30.0, ox, oy];
    MpTrRun(c, edge, 2.0, MpTrCyan(), 0.8);
    // Mass against the hairline.
    MpTrQuad(c, ox + 446.0, oy, ox + 472.0, oy, ox + 452.0, oy - 20.0, ox + 446.0, oy - 14.0, MpTrCyan(), 0.9);
    MpCsRect(c, ox + W - 120.0, oy, 74.0, 5.0, MpTrCyan(), 0.9);
    MpCsRect(c, ox, oy + H - 90.0, 5.0, 56.0, MpTrCyan(), 0.9);
    // The title in the tab, and the machine-label line above the frame. The tab is open at the
    // bottom - it runs into the panel - so its middle (-13) reads high; zeldfep, 2026-11-11: "drop
    // it down a smidge". Both texts share the -11 line.
    MpTrWideL(c, ox + 66.0, oy - 11.0, MpCsSpaced("TRADE"), 22, n"Bold", MpTrCyan());
    MpTrTextL(c, ox + 206.0, oy - 11.0, MpCsSpaced("// EXCHANGE"), 12, n"Medium", MpTrInkFaint());
    let session = v.mock ? "PREVIEW" : MpTrSession(v.session);
    MpTrTextL(c, ox, oy - 50.0, MpCsSpaced("NC.EXCHANGE v2.5") + "   //   " + MpCsSpaced("SECURE CHANNEL") + "   //   "
              + MpCsSpaced("SESSION " + session), 12, n"Medium", MpTrInkFaint());
    MpTrTextR(c, ox + W - 124.0, oy - 50.0, MpCsSpaced("FACE TO FACE") + "   //   " + MpCsSpaced("4 M"), 12, n"Medium", MpTrInkFaint());
    // The BLUR trial switch closes the label line: corner brackets like HIDE, cyan when on.
    let bx = ox + W - 110.0;
    let on = ui.blur;
    MpTrCorners(c, bx, oy - 64.0, 110.0, 28.0, 7.0, on ? MpTrCyan() : MpTrInkDim(), 1.0);
    MpTrTextL(c, bx + 12.0, oy - 50.0, MpCsSpaced("BLUR"), 12, n"Medium", on ? MpTrCyan() : MpTrInkFaint());
    MpTrTextR(c, bx + 98.0, oy - 50.0, MpCsSpaced(on ? "ON" : "OFF"), 12, n"Bold", on ? MpTrCyan() : MpTrInkDim());
    hits.Add(MpTrIdBlur(), bx, oy - 64.0, 110.0, 28.0);
    // The lug carries the rule that decides most arguments.
    MpTrTextL(c, ox + 66.0, oy + H + 9.0, "///", 13, n"Bold", MpTrRed());
    MpTrTextL(c, ox + 94.0, oy + H + 9.0, MpCsSpaced("ANY CHANGE CLEARS BOTH CONFIRMATIONS"), 12, n"Medium", MpTrInkDim());
    MpTrPips(c, ox + W - 60.0, oy + H - 12.0, 4, 5.0, 3.0, MpTrCyanDim(), 0, MpTrCyanDim());
}

// The top band: who it is with, your spendable eddies, HIDE.
public func MpTrHeader(c: ref<inkCanvas>, ox: Float, oy: Float, v: ref<MpTrView>, eyebrow: String,
                       hits: ref<MpTrHits>) -> Void {
    let W = 1120.0;
    let name = StrUpper(MpTrClip(v.partner, 14));
    MpTrTextL(c, ox + 30.0, oy + 24.0, MpCsSpaced(eyebrow), 12, n"Medium", MpTrInkFaint());
    MpTrWide(MpCsText(c, ox + 30.0, oy + 34.0, name, 34, n"Bold", MpTrInk()), n"Bold");
    MpCsRect(c, ox + 30.0, oy + 78.0, 150.0, 3.0, MpTrRed(), 0.95);
    MpCsRect(c, ox + 184.0, oy + 78.0, 22.0, 3.0, MpTrRed(), 0.5);
    MpCsRect(c, ox + 210.0, oy + 78.0, 8.0, 3.0, MpTrRed(), 0.3);
    // A leader from the name toward their bay - they are the right-hand side.
    let lead = MaxF(300.0, 60.0 + Cast<Float>(StrLen(name)) * 27.0);
    MpCsLine(c, ox + lead, oy + 62.0, ox + 640.0, oy + 62.0, 1.5, MpTrRedDim(), 0.9);
    MpCsLine(c, ox + 640.0, oy + 62.0, ox + 656.0, oy + 78.0, 1.5, MpTrRedDim(), 0.9);
    MpTrNode(c, ox + 660.0, oy + 82.0, MpTrVoid(), 1.0, MpTrRed());
    if v.live && v.state <= 1u {
        MpTrTextR(c, ox + W - 150.0, oy + 22.0, MpCsSpaced("YOUR EDDIES"), 12, n"Medium", MpTrInkFaint());
        MpTrTextR(c, ox + W - 176.0, oy + 52.0, MpTrFmt(v.available), 30, n"Bold", MpTrGold());
        MpTrTextL(c, ox + W - 170.0, oy + 56.0, "€$", 15, n"Bold", MpTrGoldDim());   // the game's own eddie mark
    }
    // HIDE gives the mouse back; the trade carries on. Only CANCEL walks away.
    MpTrCorners(c, ox + W - 130.0, oy + 22.0, 96.0, 44.0, 9.0, MpTrInkDim(), 1.0);
    MpTrTextC(c, ox + W - 82.0, oy + 35.0, "ESC", 12, n"Medium", MpTrInkFaint());
    MpTrTextC(c, ox + W - 82.0, oy + 53.0, MpCsSpaced("HIDE"), 14, n"Bold", MpTrInkDim());
    hits.Add(MpTrIdHide(), ox + W - 130.0, oy + 22.0, 96.0, 44.0);
    MpCsRect(c, ox + 30.0, oy + 96.0, W - 60.0, 1.5, MpTrLine(), 1.0);
}

// A module bay: a notched frame on a hairline, a lit tab with its number, a spec line.
public func MpTrBay(c: ref<inkCanvas>, x: Float, y: Float, w: Float, num: String, title: String,
                    color: HDRColor, spec: String, specPad: Float) -> Void {
    let h = 360.0;
    let edge: array<Float> = [x, y + 14.0, x, y + h, x + w - 16.0, y + h, x + w, y + h - 16.0, x + w, y, x + 186.0, y];
    MpTrRun(c, edge, 1.2, color, 0.35);
    MpCsPolyFill(c, MpCsCutXs(x, 180.0, 8.0), MpCsCutYs(y - 2.0, 26.0, 8.0), color, 0.95);
    MpTrTextL(c, x + 10.0, y + 11.0, num, 15, n"Bold", MpTrVoid());
    MpTrTextL(c, x + 38.0, y + 11.0, MpCsSpaced(title), 14, n"Bold", MpTrVoid());
    MpTrTextR(c, x + w - specPad, y + 11.0, spec, 12, n"Medium", MpTrInkFaint());
}

// The eddie line: a label, the figure, and a 20-segment readout of how much of the spendable money
// is in. Yours carries the six steps either side of the figure.
public func MpTrEddies(c: ref<inkCanvas>, x: Float, y: Float, w: Float, amount: Int32, cap: Int32,
                       mine: Bool, pending: Bool, hits: ref<MpTrHits>) -> Void {
    MpTrTextL(c, x, y + 6.0, MpCsSpaced(mine ? "YOU PUT IN" : "THEY PUT IN"), 12, n"Medium", MpTrInkFaint());
    if mine {
        let cx = x;
        let downs: array<String> = ["-1K", "-100", "-1"];
        let k = 0;
        while k < 3 {
            MpTrStep(c, cx, y + 18.0, 42.0, 34.0, downs[k], 14, MpTrInkDim());
            hits.Add(MpTrIdMoney() + k, cx, y + 18.0, 42.0, 34.0);
            cx += 46.0;
            k += 1;
        }
        MpTrTextC(c, cx + 76.0, y + 35.0, MpTrFmt(amount), 32, n"Bold", pending ? MpTrInkDim() : MpTrGold());
        cx += 158.0;
        let ups: array<String> = ["+1", "+100", "+1K"];
        k = 0;
        while k < 3 {
            MpTrStep(c, cx, y + 18.0, 42.0, 34.0, ups[k], 14, MpTrCyan());
            hits.Add(MpTrIdMoney() + 3 + k, cx, y + 18.0, 42.0, 34.0);
            cx += 46.0;
            k += 1;
        }
    } else {
        MpTrTextL(c, x, y + 35.0, MpTrFmt(amount), 32, n"Bold", MpTrGold());
    }
    let seg = 20;
    let sw = (w - 19.0 * 3.0) / 20.0;
    let lit = 0;
    if cap > 0 {
        lit = Cast<Int32>(Cast<Float>(seg) * MinF(1.0, Cast<Float>(amount) / Cast<Float>(cap)) + 0.5);
    } else {
        if amount > 0 { lit = seg; }
    }
    let i = 0;
    while i < seg {
        MpCsRect(c, x + Cast<Float>(i) * (sw + 3.0), y + 58.0, sw, 7.0, i < lit ? MpTrGold() : MpTrGoldDim(), i < lit ? 0.95 : 0.35);
        i += 1;
    }
}

// A row, numbered like a rail. In the deal: a lit plate, a bar down its left, an outline.
public func MpTrRow(c: ref<inkCanvas>, x: Float, y: Float, w: Float, index: Int32, item: ref<MpTrItem>, lit: Bool) -> Void {
    if lit {
        MpCsRect(c, x, y, w, 42.0, MpTrCyan(), 0.13);
        MpCsRect(c, x, y, 4.0, 42.0, MpTrCyan(), 1.0);
        MpCsPolyEdge(c, MpCsCutXs(x, w, 8.0), MpCsCutYs(y, 42.0, 8.0), 1.2, MpTrCyan(), 1.0);
    } else {
        MpCsRect(c, x, y + 42.0, w, 1.0, MpTrLine(), 1.0);
    }
    let num = index < 9 ? "0" + ToString(index + 1) : ToString(index + 1);
    MpTrTextL(c, x + 12.0, y + 21.0, num, 13, n"Bold", lit ? MpTrCyan() : MpTrInkFaint());
    MpCsText(c, x + 42.0, y + 3.0, MpTrClip(item.name, 26), 19, n"Medium", MpTrInk());
    MpCsText(c, x + 42.0, y + 26.0, MpCsSpaced(StrUpper(MpTrClip(item.kind, 20))), 10, n"Medium", MpTrInkFaint());
}

// ============================================================================ the rail
//
// zeldfep, 2026-11-11: "instead of the up and down buttons can we replace those with just a rail
// in the window so scrolling works - check rail doc for launcher it should act the same". So this
// is the launcher's Settings rail (docs/DESIGN-LANGUAGE.md, "The rail is a ladder"; the launcher's
// railLadder and its drag/wheel handlers), at 4/3 the launcher's pixels and without its step tabs:
// - a ladder of rungs on a spine, a numbered tick at each place you can scroll to, and the
//   striped arrow that says where you are (striped hazard = position, the only stripe of its kind);
// - the arrow is the handle: drag it and the list follows, so it stays under the cursor;
// - a press on the bare ladder jumps there; the wheel over the rail moves two rungs a notch;
// - the caps at each end are dim while there is room to move and solid at the end.
// The one difference is forced by the list: it can only show whole rows, so the arrow steps from
// place to place instead of gliding between them. The numbers are the row that will be at the top.
public func MpTrRailW() -> Float = 42.0

public class MpTrRailMap {
    public let list: Int32;      // 0 your side, 1 theirs
    // The rail's column, where a press picks up or jumps the arrow.
    public let x: Float;
    public let y: Float;
    public let w: Float;
    public let h: Float;
    // The whole list, where the wheel scrolls this list a row a notch.
    public let ax: Float;
    public let ay: Float;
    public let aw: Float;
    public let ah: Float;
    // Where the arrow sits for each top row, 0 to the last.
    public let ys: array<Float>;
    public let top: Int32;
    public let pitch: Float;

    public func Nearest(y: Float) -> Int32 {
        let best = 0;
        let i = 1;
        while i < ArraySize(this.ys) {
            if AbsF(this.ys[i] - y) < AbsF(this.ys[best] - y) {
                best = i;
            }
            i += 1;
        }
        return best;
    }

    // Two rungs a notch, as on the launcher - but never less than one row, or a notch would do
    // nothing where the places are further apart than two rungs.
    public func Wheel(dir: Int32) -> Int32 {
        let last = ArraySize(this.ys) - 1;
        let next = this.Nearest(this.ys[this.top] + Cast<Float>(dir) * 2.0 * this.pitch);
        if next == this.top {
            next = this.top + dir;
        }
        return Max(0, Min(last, next));
    }
}

// The arrow: the launcher's 9px triangle at 4/3, a darker yellow under the hazard stripes. The
// stripes are the launcher's 45-degree tile cut to the triangle, worked out once.
public func MpTrRailArrow(c: ref<inkCanvas>, x: Float, cy: Float) -> Void {
    let y = cy - 6.0;
    let bx: array<Float> = [x, x + 12.0, x];
    let by: array<Float> = [y, y + 6.0, y + 12.0];
    MpCsPolyFill(c, bx, by, MpTrGoldBase(), 1.0);
    let ax: array<Float> = [x, x + 1.78, x];
    let ay: array<Float> = [y, y + 0.89, y + 2.67];
    MpCsPolyFill(c, ax, ay, MpTrGold(), 1.0);
    let mx: array<Float> = [x + 4.44, x + 7.11, x, x];
    let my: array<Float> = [y + 2.22, y + 3.56, y + 10.67, y + 6.67];
    MpCsPolyFill(c, mx, my, MpTrGold(), 1.0);
    let tx: array<Float> = [x + 9.78, x + 12.0, x + 5.33];
    let ty: array<Float> = [y + 4.89, y + 6.0, y + 9.33];
    MpCsPolyFill(c, tx, ty, MpTrGold(), 1.0);
}

// A caution cap: three hazard stripes at 45 degrees, the launcher's 24x5 at 4/3.
public func MpTrRailCap(c: ref<inkCanvas>, x: Float, y: Float, op: Float) -> Void {
    let h = 6.67;
    let i = 0;
    while i < 3 {
        let a = x + Cast<Float>(i) * 10.67;
        MpTrQuad(c, a, y + h, a + h, y, a + h + 5.33, y, a + 5.33, y + h, MpTrGold(), op);
        i += 1;
    }
}

/**
 * The rail for a list of `total` rows showing `rows`, in a column at (x, y) of height h. Draws
 * nothing when everything fits. The ladder is the launcher's railLadder: the places are put on a
 * rung grid of at most 9 (7px at 4/3), at least a rung or two apart, the first at the top and the
 * last at the bottom - and when there are more places than rungs, they share the span evenly.
 */
public func MpTrDrawRail(c: ref<inkCanvas>, x: Float, y: Float, h: Float, total: Int32, rows: Int32, top: Int32,
                         list: Int32, spine: HDRColor, area: Vector4, hits: ref<MpTrHits>) -> Void {
    let last = total - rows;
    if last < 1 {
        return;
    }
    let cap = 7.0;
    let gap = 8.0;
    let inset = 8.0;
    let trackTop = y + cap + gap;
    let trackH = h - 2.0 * (cap + gap);
    let span = trackH - 2.0 * inset;
    let y0 = trackTop + inset;

    let pitch = MinF(9.0, MaxF(3.0, Cast<Float>(FloorF(span / Cast<Float>(last)))));
    let rungs = FloorF(span / pitch);

    let rail = new MpTrRailMap();
    rail.list = list;
    rail.x = x;
    rail.y = y;
    rail.w = MpTrRailW();
    rail.h = h;
    rail.ax = area.X;
    rail.ay = area.Y;
    rail.aw = area.Z;
    rail.ah = area.W;
    rail.top = top;
    rail.pitch = pitch;

    if last <= rungs {
        let step = Max(1, Min(2, rungs / last));
        let at: array<Int32>;
        let i = 0;
        while i <= last {
            ArrayPush(at, RoundF(Cast<Float>(i) / Cast<Float>(last) * Cast<Float>(rungs)));
            i += 1;
        }
        i = 1;
        while i <= last {
            at[i] = Max(at[i], at[i - 1] + step);
            i += 1;
        }
        at[last] = Min(at[last], rungs);
        i = last - 1;
        while i >= 0 {
            at[i] = Min(at[i], at[i + 1] - step);
            i -= 1;
        }
        i = 0;
        while i <= last {
            ArrayPush(rail.ys, y0 + Cast<Float>(Max(0, at[i])) * pitch);
            i += 1;
        }
    } else {
        let i = 0;
        while i <= last {
            ArrayPush(rail.ys, y0 + span * Cast<Float>(i) / Cast<Float>(last));
            i += 1;
        }
    }
    ArrayPush(hits.rails, rail);

    // Caps, centred on the column; solid at the end you are at.
    let capX = x + (MpTrRailW() - 32.0) / 2.0;
    MpTrRailCap(c, capX, y, top == 0 ? 1.0 : 0.22);
    MpTrRailCap(c, capX, y + h - cap, top == last ? 1.0 : 0.22);

    // The spine and its rungs.
    let sx = x + 12.0;
    MpCsRect(c, sx - 0.75, trackTop, 1.5, trackH, spine, 1.0);
    let r = 0;
    while r <= rungs {
        MpCsRect(c, sx + 1.0, y0 + Cast<Float>(r) * pitch - 0.75, 5.0, 1.5, MpTrInkDim(), 0.9);
        r += 1;
    }

    // Numbered ticks, at least 14 apart; the last always has its number.
    let per = (rail.ys[last] - rail.ys[0]) / Cast<Float>(last);
    let every = 1;
    while per * Cast<Float>(every) < 14.0 {
        every += 1;
    }
    let i = 0;
    while i <= last {
        let numbered = i == last || (i % every == 0 && rail.ys[last] - rail.ys[i] >= 14.0);
        if numbered {
            let ty = rail.ys[i];
            let label = i < 9 ? "0" + ToString(i + 1) : ToString(i + 1);
            MpCsRect(c, sx, ty - 0.75, 15.0, 1.5, MpTrInk(), 1.0);
            MpTrTextL(c, x + 30.0, ty, label, 11, n"Bold", i == top ? MpTrInk() : MpTrInkDim());
        }
        i += 1;
    }

    // The arrow, left of the spine with its tip on it.
    MpTrRailArrow(c, x, rail.ys[Max(0, Min(last, top))]);
}

public func MpTrYourSide(c: ref<inkCanvas>, ox: Float, oy: Float, v: ref<MpTrView>, ui: ref<MpTrUi>, hits: ref<MpTrHits>) -> Void {
    let x = ox + 30.0;
    let w = 500.0;
    let y0 = oy + 110.0;
    let rows = 5;
    let total = ArraySize(v.holdings);
    if ui.listTop > total - rows { ui.listTop = total - rows; }
    if ui.listTop < 0 { ui.listTop = 0; }

    let ins = 0;
    let k = 0;
    while k < total {
        if v.holdings[k].offered > 0 { ins += 1; }
        k += 1;
    }
    let more = total - ui.listTop - rows;
    if more < 0 { more = 0; }
    MpTrBay(c, x, y0, w, "01", "YOUR SIDE", MpTrCyan(),
            MpCsSpaced(ToString(ins) + " IN") + "   //   " + MpCsSpaced(ToString(more) + " MORE"), 16.0);

    let shown = ui.pending >= 0 ? ui.pending : v.myMoney;
    MpTrEddies(c, x + 14.0, oy + 142.0, w - 28.0, shown, v.available, true, ui.pending >= 0, hits);

    if total == 0 {
        MpCsText(c, x + 24.0, oy + 236.0, "Nothing you carry can be traded.", 18, n"Regular", MpTrInkFaint());
    }

    // Rows stop short of the rail's column (MpTrRailW), which is kept whether or not the list
    // scrolls, so nothing moves when it starts to.
    let rowW = w - 20.0 - MpTrRailW() - 4.0;
    let right = x + 10.0 + rowW;
    let r = 0;
    while r < rows && ui.listTop + r < total {
        let idx = ui.listTop + r;
        let item = v.holdings[idx];
        let ry = oy + 226.0 + Cast<Float>(r) * 48.0;
        let lit = item.offered > 0;
        MpTrRow(c, x + 10.0, ry, rowW, idx, item, lit);
        hits.Add(MpTrIdRow() + idx, x + 10.0, ry, rowW, 42.0);

        if lit && item.qty > 1 {
            // A partly-offered stack gets a stepper; the row itself still takes it all back.
            let qx = right - 160.0;
            MpTrStep(c, qx, ry + 8.0, 26.0, 26.0, "-", 16, MpTrInkDim());
            MpTrTextC(c, qx + 70.0, ry + 21.0, ToString(item.offered) + "/" + ToString(item.qty), 17, n"Bold", MpTrGold());
            MpTrStep(c, qx + 114.0, ry + 8.0, 26.0, 26.0, "+", 16, MpTrCyan());
            hits.Add(MpTrIdLess() + idx, qx, ry + 8.0, 26.0, 26.0);
            hits.Add(MpTrIdMore() + idx, qx + 114.0, ry + 8.0, 26.0, 26.0);
        } else {
            if lit {
                MpCsPolyFill(c, MpCsCutXs(right - 64.0, 44.0, 6.0), MpCsCutYs(ry + 11.0, 20.0, 6.0), MpTrGold(), 1.0);
                MpTrTextC(c, right - 42.0, ry + 21.0, "IN", 14, n"Bold", MpTrVoid());
            } else {
                if item.qty > 1 {
                    MpTrTextR(c, right - 20.0, ry + 21.0, "x" + ToString(item.qty), 17, n"Medium", MpTrInkDim());
                }
            }
        }
        r += 1;
    }
    MpTrDrawRail(c, x + w - 8.0 - MpTrRailW(), oy + 226.0, 234.0, total, rows, ui.listTop, 0, MpTrCyanDim(),
                 new Vector4(x, oy + 220.0, w, 246.0), hits);
}

public func MpTrTheirSide(c: ref<inkCanvas>, ox: Float, oy: Float, v: ref<MpTrView>, ui: ref<MpTrUi>, hits: ref<MpTrHits>) -> Void {
    let x = ox + 590.0;
    let w = 500.0;
    let y0 = oy + 110.0;
    let rows = 5;
    let total = ArraySize(v.theirs);
    if ui.theirTop > total - rows { ui.theirTop = total - rows; }
    if ui.theirTop < 0 { ui.theirTop = 0; }
    MpTrBay(c, x, y0, w, "02", "THEIR SIDE", MpTrRed(),
            MpCsSpaced(ToString(total) + " ITEMS") + "   //   " + MpCsSpaced("READ ONLY"), 16.0);
    MpTrEddies(c, x + 14.0, oy + 142.0, w - 28.0, v.theirMoney, 0, false, false, hits);

    if total == 0 {
        MpCsText(c, x + 24.0, oy + 236.0, "Nothing yet.", 18, n"Regular", MpTrInkFaint());
    }
    let rowW = w - 20.0 - MpTrRailW() - 4.0;
    let right = x + 10.0 + rowW;
    let r = 0;
    while r < rows && ui.theirTop + r < total {
        let idx = ui.theirTop + r;
        let item = v.theirs[idx];
        let ry = oy + 226.0 + Cast<Float>(r) * 48.0;
        MpTrRow(c, x + 10.0, ry, rowW, idx, item, false);
        if item.qty > 1 {
            MpTrTextR(c, right - 20.0, ry + 21.0, "x" + ToString(item.qty), 17, n"Bold", MpTrGold());
        }
        r += 1;
    }
    MpTrDrawRail(c, x + w - 8.0 - MpTrRailW(), oy + 226.0, 234.0, total, rows, ui.theirTop, 1, MpTrRedDim(),
                 new Vector4(x, oy + 220.0, w, 246.0), hits);
}

// The spine between the bays: a ticked rail with the exchange emblem.
public func MpTrSpine(c: ref<inkCanvas>, ox: Float, oy: Float) -> Void {
    let x = ox + 560.0;
    MpCsRect(c, x - 0.75, oy + 112.0, 1.5, 356.0, MpTrLine(), 1.0);
    let y = 120.0;
    while y < 468.0 {
        if y < 262.0 || y > 318.0 {
            MpCsRect(c, x - 4.0, oy + y, 8.0, 1.5, MpTrInkFaint(), 0.6);
        }
        y += 12.0;
    }
    MpTrHex(c, x, oy + 290.0, 26.0, MpTrVoid(), 0.9, MpTrCyan());
    MpTrChevR(c, x - 15.0, oy + 276.0, 13.0, MpTrCyan(), 1.0);
    MpTrChevL(c, x + 2.0, oy + 291.0, 13.0, MpTrRed(), 1.0);
}

// The footer: each side's confirmation wire into CONFIRM's tab, CANCEL, and the plate. The wires
// ARE the confirmation lights now - lit, and their two dots filled, when that side has locked in.
public func MpTrFooter(c: ref<inkCanvas>, ox: Float, oy: Float, v: ref<MpTrView>, ui: ref<MpTrUi>, hits: ref<MpTrHits>) -> Void {
    let plateX = ox + 820.0;
    let plateY = oy + 524.0;
    let tabX = plateX + 40.0;
    let mine = v.myConfirmed;
    let theirs = v.theirConfirmed;

    // Yours: down from your bay, along y 502, then down into the tab's left.
    let mc = mine ? MpTrCyan() : MpTrLine();
    let mt = mine ? 2.5 : 1.5;
    let mw: array<Float> = [ox + 70.0, oy + 470.0, ox + 70.0, oy + 488.0, ox + 84.0, oy + 502.0,
                            tabX + 22.0, oy + 502.0, tabX + 22.0, plateY - 12.0];
    MpTrRun(c, mw, mt, mc, 1.0);
    MpTrNode(c, ox + 70.0, oy + 470.0, mine ? MpTrCyan() : MpTrVoid(), 1.0, mine ? MpTrCyan() : MpTrInkFaint());
    MpTrTextL(c, ox + 82.0, oy + 479.0, MpCsSpaced("YOU") + "   //   " + MpCsSpaced(mine ? "LOCKED IN" : "NOT YET"), 12,
              n"Bold", mine ? MpTrCyan() : MpTrInkFaint());

    // Theirs: down from their bay, along y 494 above yours, then down into the tab's right.
    let tc = theirs ? MpTrRed() : MpTrLine();
    let tt = theirs ? 2.5 : 1.5;
    let tw: array<Float> = [ox + 630.0, oy + 470.0, ox + 630.0, oy + 480.0, ox + 644.0, oy + 494.0,
                            tabX + 58.0, oy + 494.0, tabX + 58.0, plateY - 12.0];
    MpTrRun(c, tw, tt, tc, 1.0);
    MpTrNode(c, ox + 630.0, oy + 470.0, theirs ? MpTrRed() : MpTrVoid(), 1.0, theirs ? MpTrRed() : MpTrInkFaint());
    MpTrTextL(c, ox + 642.0, oy + 479.0, MpCsSpaced(StrUpper(MpTrClip(v.partner, 14))) + "   //   "
              + MpCsSpaced(theirs ? "LOCKED IN" : "NOT YET"), 12, n"Bold", theirs ? MpTrRed() : MpTrInkFaint());

    // CANCEL: red bracket - it ends the trade for both of you.
    MpTrBracket(c, plateX - 196.0, plateY + 4.0, 168.0, 44.0, "CANCEL", 17, MpTrRed());
    hits.Add(MpTrIdCancel(), plateX - 196.0, plateY + 4.0, 168.0, 44.0);

    let dots = (mine ? 2 : 0) + (theirs ? 2 : 0);
    if mine {
        MpTrPlate(c, plateX, plateY, theirs ? "EXCHANGING" : "LOCKED IN", false, dots);
    } else {
        if ui.pending >= 0 {
            // An amount on its way: confirming now would agree to the number before it.
            MpTrPlate(c, plateX, plateY, "UPDATING", false, dots);
        } else {
            MpTrPlate(c, plateX, plateY, "CONFIRM", true, dots);
            hits.Add(MpTrIdConfirm(), plateX, plateY - 12.0, 262.0, 64.0);
        }
    }
}

// An invitation, either way round: who, what happens next, and the two ways to answer.
public func MpTrInvite(c: ref<inkCanvas>, ox: Float, oy: Float, v: ref<MpTrView>, hits: ref<MpTrHits>) -> Void {
    MpTrHeader(c, ox, oy, v, v.incoming ? "INCOMING" : "OUTGOING", hits);
    if v.incoming {
        MpCsText(c, ox + 60.0, oy + 170.0, v.partner + " wants to trade with you.", 30, n"Medium", MpTrInk());
        MpCsText(c, ox + 60.0, oy + 216.0, "Accept to open the trade. Nothing moves until you both lock in.", 19, n"Regular", MpTrInkDim());
    } else {
        MpCsText(c, ox + 60.0, oy + 170.0, "Waiting for " + v.partner + " to accept...", 30, n"Medium", MpTrInk());
        MpCsText(c, ox + 60.0, oy + 216.0, "They have 30 seconds. Stay close - trades are face to face.", 19, n"Regular", MpTrInkDim());
    }
    MpTrHex(c, ox + 560.0, oy + 360.0, 60.0, MpTrVoid(), 0.6, MpTrCyan());
    MpTrChevR(c, ox + 526.0, oy + 330.0, 30.0, MpTrCyan(), 1.0);
    MpTrChevL(c, ox + 564.0, oy + 362.0, 30.0, MpTrRed(), 1.0);
    let plateX = ox + 820.0;
    let plateY = oy + 524.0;
    MpTrBracket(c, plateX - 196.0, plateY + 4.0, 168.0, 44.0, v.incoming ? "DECLINE" : "CANCEL", 17, MpTrRed());
    hits.Add(MpTrIdCancel(), plateX - 196.0, plateY + 4.0, 168.0, 44.0);
    if v.incoming {
        MpTrPlate(c, plateX, plateY, "ACCEPT", true, 0);
        hits.Add(MpTrIdConfirm(), plateX, plateY - 12.0, 262.0, 64.0);
    }
}

// An ending: what happened, one line on why, and CLOSE.
public func MpTrEnding(c: ref<inkCanvas>, ox: Float, oy: Float, v: ref<MpTrView>, hits: ref<MpTrHits>) -> Void {
    let done = v.state == 3u;
    MpTrTextL(c, ox + 30.0, oy + 24.0, MpCsSpaced("SESSION " + MpTrSession(v.session)) + "   //   " + MpCsSpaced("CLOSED"),
              12, n"Medium", MpTrInkFaint());
    MpCsRect(c, ox + 30.0, oy + 96.0, 1060.0, 1.5, MpTrLine(), 1.0);
    MpTrWide(MpCsText(c, ox + 60.0, oy + 140.0, MpCsSpaced(done ? "TRADE COMPLETE" : "TRADE CLOSED"), 34, n"Bold",
                      done ? MpTrCyan() : MpTrRed()), n"Bold");
    let why = StrLen(v.reason) > 0 ? v.reason : "The trade is over.";
    MpCsText(c, ox + 60.0, oy + 204.0, why, 22, n"Medium", MpTrInk());
    if done {
        MpCsText(c, ox + 60.0, oy + 240.0, "Your inventory has been updated.", 19, n"Regular", MpTrInkDim());
        MpTrPips(c, ox + 60.0, oy + 282.0, 20, 10.0, 4.0, MpTrGold(), 20, MpTrGold());
        MpCsText(c, ox + 60.0, oy + 302.0, MpCsSpaced("GAVE " + ToString(ArraySize(v.mine))) + "   //   "
                 + MpCsSpaced("GOT " + ToString(ArraySize(v.theirs))) + "   //   "
                 + MpCsSpaced(MpTrFmt(v.myMoney) + " OUT  " + MpTrFmt(v.theirMoney) + " IN"), 13, n"Medium", MpTrInkFaint());
    }
    MpTrBracket(c, ox + 890.0, oy + 528.0, 168.0, 44.0, "CLOSE", 17, MpTrInkDim());
    hits.Add(MpTrIdHide(), ox + 890.0, oy + 528.0, 168.0, 44.0);
}

/**
 * The whole panel, from a view, into a canvas authored at 1920x1080.
 *
 * ui may not be null; hits collects every clickable region in draw order, and MpTrDrawHits turns
 * them into the hover-lit rectangles on top. The region ids are unchanged from the first build,
 * so ChatController.MpTrAct did not move.
 */
public func MpTrBuild(c: ref<inkCanvas>, v: ref<MpTrView>, ui: ref<MpTrUi>, hits: ref<MpTrHits>) -> Void {
    let ox = MpTrPanelX();
    let oy = MpTrPanelY();
    MpTrFrame(c, ox, oy, v, ui, hits);

    if !v.live {
        MpTrEnding(c, ox, oy, v, hits);
        return;
    }
    if v.state == 0u {
        MpTrInvite(c, ox, oy, v, hits);
        return;
    }

    MpTrHeader(c, ox, oy, v, v.mock ? "PREVIEW  -  NOTHING IS SENT" : "TRADING WITH", hits);
    MpTrYourSide(c, ox, oy, v, ui, hits);
    MpTrSpine(c, ox, oy);
    MpTrTheirSide(c, ox, oy, v, ui, hits);
    MpTrFooter(c, ox, oy, v, ui, hits);
}

/**
 * The HUD chip: what stays beside chat while the mouse is the game's, or before an invitation is
 * opened. The panel's own vocabulary at a fraction of its size - a lit tab, the label line, the
 * key on a gold plate - placed the way the v2 overlay was, so it lands where the panel's corner is.
 */
public func MpTrHudChip(c: ref<inkCanvas>, title: String, line: String, hint: String, session: String) -> Void {
    let x = MpTrPanelX();
    let y = MpTrPanelY();
    let w = 620.0;
    let h = 88.0;
    MpCsPolyFill(c, MpCsCutXs(x, w, 16.0), MpCsCutYs(y, h, 16.0), MpTrPanelDeep(), 0.8);
    MpCsPolyEdge(c, MpCsCutXs(x, w, 16.0), MpCsCutYs(y, h, 16.0), 2.0, MpTrCyan(), 0.8);
    MpCsPolyFill(c, MpCsCutXs(x, 170.0, 8.0), MpCsCutYs(y - 2.0, 26.0, 8.0), MpTrCyan(), 0.95);
    MpTrWideL(c, x + 12.0, y + 11.0, MpCsSpaced(title), 13, n"Bold", MpTrVoid());
    MpTrTextL(c, x + 186.0, y + 11.0, MpCsSpaced("SESSION " + session), 11, n"Medium", MpTrInkFaint());
    MpTrTextL(c, x + 22.0, y + 56.0, line, 24, n"Medium", MpTrInk());
    if StrLen(hint) > 0 {
        MpCsPolyFill(c, MpCsCutXs(x + w - 150.0, 128.0, 8.0), MpCsCutYs(y + 34.0, 40.0, 8.0), MpTrGold(), 1.0);
        MpTrTextC(c, x + w - 86.0, y + 54.0, MpCsSpaced("ENTER"), 17, n"Bold", MpTrVoid());
        MpTrTextR(c, x + w - 162.0, y + 56.0, hint, 16, n"Medium", MpTrInkDim());
    }
}
