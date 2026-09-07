// NetworkWorldSystem lives in module CyberpunkMP.World. This file has no module of its
// own, so without this import the type is simply not in scope and
// GameInstance.GetNetworkWorldSystem() fails to resolve - taking every other script in
// the mod down with it, because redscript aborts all compilation on one bad file.
import CyberpunkMP.World.*

// Adds a MULTIPLAYER entry to the main menu, beside Continue / New Game / Load Game.
//
// Written against the game's own script source, which ships at
// <game>\tools\redmod\scripts\ and is authoritative for the installed patch:
//
//   cyberpunk/UI/fullscreen/pregame/singleplayerMenu.script:843
//       SingleplayerMenuGameController.PopulateMenuItemList()
//   cyberpunk/UI/fullscreen/pregame/singleplayerMenu.script:1001
//       HandleMenuItemActivate() - where Continue actually loads a save
//   cyberpunk/UI/menus/menuItemListGameController.script:56
//       AddMenuItem(label, spawnEvent: CName) -> pushes PauseMenuListItemData
//
// Everything lives on the CONTROLLER on purpose. The first attempt put the handler on
// MenuScenario_SingleplayerMenu and failed to compile with UNRESOLVED_FN on
// GetSystemRequestsHandler - that is a method declared on widgetController
// (core/ui/baseControllers/widgetController.script:84), not a free function, so it only
// resolves on classes that inherit it. The controller does; the scenario does not.

/**
 * Waits for the server to say what character this account owns, then acts on it.
 *
 * THE SELECTOR'S STATE MACHINE, in the one place the game gives us to put it. The brief's
 * own instruction is to make the states work before making them pretty, and this is that:
 * CHARACTER_SELECT is the interval between pressing MULTIPLAYER and the answer arriving.
 *
 * Polls rather than listens because the answer lands on the network thread into native
 * state - there is no script-side event to subscribe to. Ten attempts at a quarter second
 * is two and a half seconds, which is generous for a round trip on a tailnet and short
 * enough that a dead server does not leave somebody staring at a menu that never responds.
 *
 * Giving up says so. Failing silently here would be indistinguishable from the button not
 * working, which is the single most expensive bug shape this project has had.
 */
public class MpSelectorPoll extends DelayCallback {
    public let controller: wref<SingleplayerMenuGameController>;
    public let attempts: Int32;

    /**
     * Whether landing the roster should go straight into the world.
     *
     * TWO CALLERS, TWO ANSWERS, and conflating them was a real bug. CONNECT wants the
     * roster ON SCREEN so somebody can choose; PLAY wants the world. Both polls look
     * identical - wait for the server to say who this account is - so both used this class,
     * and this class always entered the world. Pressing CONNECT therefore loaded you
     * straight in as whoever happened to be active, which is exactly the thing a character
     * selection screen exists to prevent.
     */
    public let enterWhenKnown: Bool;

    public func Call() -> Void {
        let network = GameInstance.GetNetworkWorldSystem();

        if !IsDefined(network) || !IsDefined(this.controller) {
            return;
        }

        if network.IsCharacterStatusKnown() {
            // Panel first either way, so the answer is on screen before anything else
            // happens - otherwise the only feedback for a press is a load, or nothing.
            this.controller.MpUpdatePanel();

            if this.enterWhenKnown {
                this.controller.MpEnterWithCharacter();
            } else {
                // CONNECT: rebuild the menu so it now offers PLAY, a row per character
                // slot and DELETE, against a roster that has actually arrived.
                this.controller.MpRefreshMenu();
            }

            return;
        }

        this.attempts += 1;

        if this.attempts >= 10 {
            FTLogError(s"[CyberpunkMP] the server never said what character this account has");

            if !this.enterWhenKnown {
                /*
                 * CONNECT that could not connect SAYS SO, and goes nowhere.
                 *
                 * The old fallback loaded the world anyway, which was right when the button
                 * meant "play now" - a slow server cost a pause rather than a session. It is
                 * wrong for CONNECT: dropping somebody into a singleplayer world they did not
                 * ask for, because the multiplayer server is down, is worse than telling them
                 * the server is down. There is nothing to do in that world.
                 */
                this.controller.MpConnectFailed();
                return;
            }

            /*
             * NO SERVER CHARACTER, NO WORLD. Cam's rule, 2026-09-03: "make sure it uses
             * ONLY the characters THEY made through this, nothing else, only their server
             * owned characters."
             *
             * This used to load the last save and enter anyway. That was the right call
             * when one button meant "play now" - a slow server cost a pause rather than a
             * session - but it is the exact hole the rule closes: it puts somebody in the
             * world as whoever their singleplayer save happens to contain, with a name and
             * a face the server never issued, and the server then has a player it cannot
             * identify.
             *
             * Removing it is safe now in a way it was not before, and the menu is why. PLAY
             * is only DRAWN once IsCharacterStatusKnown() is true, so by the time anybody
             * can press it the roster has already arrived and this poll returns on its
             * first tick. Reaching this line means the connection died between the menu
             * being built and the button being pressed - which is not a case for entering
             * a singleplayer world, it is a case for saying so.
             */
            this.controller.MpConnectFailed();
            return;
        }

        let again = new MpSelectorPoll();
        again.controller = this.controller;
        again.attempts = this.attempts;
        again.enterWhenKnown = this.enterWhenKnown;

        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(again, 0.25, false);
    }
}

/**
 * Waits for the server's answer to a delete, then redraws the panel from it.
 *
 * Same shape as MpSelectorPoll and for the same reason: the reply lands on the network
 * thread into native state with no script event to hang off. Redrawing from the SERVER's
 * list rather than assuming the delete worked - it can be refused, and a panel that
 * cleared itself would claim a character was gone while it was still there.
 */
public class MpDeletePoll extends DelayCallback {
    public let controller: wref<SingleplayerMenuGameController>;
    public let attempts: Int32;

    public func Call() -> Void {
        if !IsDefined(this.controller) {
            return;
        }

        let network = GameInstance.GetNetworkWorldSystem();
        if !IsDefined(network) {
            return;
        }

        // Either outcome is an answer: the character is gone, or the server said why not.
        if !network.HasCharacter() || NotEquals(network.GetCharacterError(), "") {
            this.controller.MpUpdatePanel();
            return;
        }

        this.attempts += 1;

        if this.attempts >= 10 {
            FTLogError(s"[Selector] the server never answered the delete");
            return;
        }

        let again = new MpDeletePoll();
        again.controller = this.controller;
        again.attempts = this.attempts;

        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(again, 0.25, false);
    }
}

/**
 * Rebuild the menu now that the server has answered.
 *
 * CONNECT changes what the other entries should be - before it there is nothing to play as
 * and nothing to switch between, after it there is - and the list is only built when the
 * screen opens. Without this, connecting would fill the panel with a roster while the menu
 * beside it still offered nothing but CONNECT.
 *
 * CALLS THE GAME'S OWN REBUILD, and the distinction is not academic - it is the difference
 * between this working and reproducing the bug it was written for.
 *
 * PopulateMenuItemList does NOT clear the list. Read it in the shipped source
 * (singleplayerMenu.script:843) and it is nothing but a run of AddMenuItem calls, so
 * calling it a second time APPENDS a whole second menu - Continue, New Game, Load Game,
 * Settings, Credits and every one of ours, twice. That is exactly the duplicate-entries
 * symptom this change exists to fix, and I had written precisely that before checking.
 *
 * ShowActionsList is the engine's own answer (menuItemListGameController.script:79):
 * Clear(), then PopulateMenuItemList(), then Refresh(). Using it rather than composing
 * those three here means the rebuild stays correct if CDPR ever changes what a rebuild
 * involves.
 */
@addMethod(SingleplayerMenuGameController)
public func MpRefreshMenu() -> Void {
    this.ShowActionsList();
}

/**
 * CONNECT could not reach the server. Say so on the panel and go nowhere.
 *
 * Deliberately NOT the old fallback of loading the world anyway. That was right while the
 * button meant "play now" - a slow server cost a pause rather than a session. It is wrong
 * for CONNECT: dropping somebody into a singleplayer world because the multiplayer server
 * is down leaves them somewhere there is nothing to do, having asked for the opposite.
 */
@addMethod(SingleplayerMenuGameController)
public func MpConnectFailed() -> Void {
    if IsDefined(this.m_mpDetail) {
        this.m_mpDetail.SetText("Could not reach the server. Try again in a moment.");
    }
}

@addMethod(SingleplayerMenuGameController)
public func MpEnterWithCharacter() -> Void {
    let network = GameInstance.GetNetworkWorldSystem();
    if !IsDefined(network) {
        return;
    }

    if network.HasCharacter() {
        FTLog(s"[CyberpunkMP] playing as '\(network.GetCharacterName())' (level \(network.GetCharacterLevel()))");

        // Arm the in-world half. The world still has to be loaded to have somewhere to
        // stand; what changed is that the server already knows who is arriving.
        network.RequestJoin();

        // The world template, ALWAYS. Identity comes from the server, never from a save.
        //
        // This was LoadLastCheckpoint(false) once - save index 0, whatever is newest - and
        // then briefly "the newest save that is not the template", which sounded like an
        // identity and was not: it loaded whichever character happened to have the newest
        // file, including a probe run's throwaway Corpo. That coin flip is why this looked
        // like an appearance bug for weeks.
        //
        // See OwnSave.reds for the full reasoning, including why the planned fix - naming a
        // save per character - was dropped: it needs the mod to write saves, and the save
        // lock (343b912) forbids that on purpose.
        this.MpLoadMultiplayerWorld();
        return;
    }

    // No character on this account - the CREATE branch.
    //
    // Routed through the game's own New Game flow for the reason spelled out below: the
    // customization system is native-only and cannot be opened on demand, so New Game is
    // the only real character creation that exists.
    FTLog(s"[CyberpunkMP] this account has no character - starting creation");

    network.RequestJoin();
    network.MarkNewCharacter();

    // The menu's own New Game event, not a call on the requests handler - RequestNewGame
    // does not exist there, as the NEW CHARACTER path below already established. This is
    // the event the vanilla New Game item spawns, so it is the real flow with the real
    // lifepath and creator screens.
    this.m_menuEventDispatcher.SpawnEvent(n"OnNewGame");
}

/**
 * The character panel: who the server says you are, drawn on the main menu.
 *
 * Built in script rather than authored into an .inkwidget, the same way ChatController
 * builds its CHARACTER NAME label. That is the only route available here - the menu's own
 * assets are compiled, and adding to them is WolvenKit work rather than code.
 *
 * EVERY step is guarded and says what it could not find. A widget path that is wrong
 * produces nothing at all on screen, which is indistinguishable from the selector never
 * running, and that ambiguity has cost this project more time than any actual bug. If this
 * lands somewhere invisible the log still says it was built.
 *
 * Informational only. The interactive parts are menu ITEMS, which are the game's own
 * mechanism and work reliably; a hand-built clickable button would need its own input
 * handling and hover states to feel like it belonged.
 */
@addField(SingleplayerMenuGameController)
let m_mpPanel: wref<inkVerticalPanel>;

@addField(SingleplayerMenuGameController)
let m_mpDetail: wref<inkText>;

// First press of DELETE arms, second confirms. A character is hours of somebody's evening
// and the store keeps a retired copy, but neither is a reason to delete on one click.
@addField(SingleplayerMenuGameController)
let m_mpDeleteArmed: Bool;

@addMethod(SingleplayerMenuGameController)
public func MpBuildPanel() -> Void {
    if IsDefined(this.m_mpPanel) {
        return;
    }

    let root = this.GetRootCompoundWidget();
    if !IsDefined(root) {
        FTLogWarning(s"[Selector] no root widget to hang the character panel on");
        return;
    }

    let panel = new inkVerticalPanel();
    panel.SetName(n"mp_character_panel");
    panel.SetAnchor(inkEAnchor.TopRight);

    // Anchor point as well as anchor. SetAnchor alone puts the widget's own top-LEFT
    // corner at the screen's top-right, so a fit-to-content panel grows off the edge and
    // only its first characters stay visible - which is exactly what it did. (1,0) means
    // "line up MY top-right with that corner", so it grows inwards instead.
    panel.SetAnchorPoint(1.0, 0.0);
    // 120 put this on top of the expansion logo - the panel's first line rendered straight
    // through "PHANTOM LIBERTY". 260 clears it on 1080p and above.
    panel.SetMargin(new inkMargin(0.0, 260.0, 90.0, 0.0));
    panel.SetFitToContent(true);
    panel.SetHAlign(inkEHorizontalAlign.Right);
    panel.Reparent(root);

    let detail = new inkText();
    detail.SetName(n"mp_character_detail");
    detail.SetText("");
    detail.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
    detail.SetFontStyle(n"Regular");
    detail.SetFontSize(32);
    detail.SetHorizontalAlignment(textHorizontalAlignment.Right);
    detail.SetHAlign(inkEHorizontalAlign.Right);

    // The same yellow the game uses for prompts, so it reads as the game speaking.
    detail.SetTintColor(new HDRColor(2.0, 1.75, 0.25, 1.0));
    detail.Reparent(panel);

    this.m_mpPanel = panel;
    this.m_mpDetail = detail;

    FTLog(s"[Selector] status line built");
}

@addMethod(SingleplayerMenuGameController)
public func MpUpdatePanel() -> Void {
    if !IsDefined(this.m_mpDetail) {
        return;
    }

    let network = GameInstance.GetNetworkWorldSystem();
    if !IsDefined(network) {
        return;
    }

    // A refusal outranks everything - it is the answer to the button they just pressed.
    let error = network.GetCharacterError();
    if NotEquals(error, "") {
        this.m_mpDetail.SetText(error);
        return;
    }

    if !network.IsCharacterStatusKnown() {
        this.m_mpDetail.SetText("signing in...");
        return;
    }

    /*
     * NOTHING TO SAY IS THE NORMAL STATE, and it says nothing.
     *
     * This used to draw the whole roster - four slot lines, a used/total count and a hint -
     * top-right, where it rendered straight through the expansion logo. That list is now
     * the MENU: one full-width item per slot, which is the game's own surface and is
     * clickable, focusable and controller-navigable, none of which a widget built at
     * runtime gets for free.
     *
     * Keeping a second copy beside the menu would be two places to read the same fact, and
     * the one on the right could not be pressed. So this is a status line now: blank unless
     * there is a refusal to show or an action in flight.
     */
    if !network.HasCharacter() {
        // The one state the rows cannot explain by themselves. Four EMPTY SLOT lines say
        // what is missing but not what to do about it, and NEW CHARACTER is destructive
        // enough that it should be named rather than guessed at.
        this.m_mpDetail.SetText("No character yet - NEW CHARACTER makes one.");
        return;
    }

    this.m_mpDetail.SetText("");
}

/**
 * One menu row for one slot.
 *
 * Full-width menu items rather than a panel, because selection has to be PRESSABLE and
 * the game's item list is the only surface on this screen that reliably is. The caret
 * marks who you are about to play as; lifepath and level are what tell two of your own
 * characters apart at a glance, which is the entire reason the roster carries them.
 *
 * Slots are addressed by number, not by roster index: retiring the character in slot 1
 * of three leaves 0 and 2 occupied, and a list built from the roster alone would draw
 * two rows and silently renumber them.
 */
@addMethod(SingleplayerMenuGameController)
public func MpSlotLabel(slot: Int32) -> String {
    let network = GameInstance.GetNetworkWorldSystem();

    if !IsDefined(network) {
        return s"      \(slot + 1).   ---";
    }

    let i = 0u;

    while i < network.GetRosterCount() {
        if network.GetRosterSlot(i) == slot {
            let name = network.GetRosterName(i);
            let shown = NotEquals(name, "") ? name : "unnamed";
            let marker = network.IsRosterActive(i) ? ">  " : "     ";

            // NEW rather than LEVEL 0: a character that has never been in the world has
            // no level yet, and printing one implies a character that has been played.
            let detail = network.HasRosterSpawnedBefore(i) ? s"LEVEL \(network.GetRosterLevel(i))" : "NEW";

            // Lifepath is optional by design - a character made before the field existed
            // has none, and that is a normal row rather than a broken one.
            let lifepath = network.GetRosterLifepath(i);

            if NotEquals(lifepath, "") {
                return s"\(marker)\(slot + 1).   \(shown)   -   \(lifepath)   -   \(detail)";
            }

            return s"\(marker)\(slot + 1).   \(shown)   -   \(detail)";
        }

        i += 1u;
    }

    return s"     \(slot + 1).   EMPTY SLOT";
}

/**
 * Which event a slot row fires.
 *
 * Four fixed CNames rather than one built with StringToName, because the handler compares
 * against literals and a name built at runtime is a different value than the literal that
 * looks identical - the same trap Phone.reds carries a comment about.
 */
@addMethod(SingleplayerMenuGameController)
public func MpSlotEvent(slot: Int32) -> CName {
    if slot == 0 {
        return n"OnMultiplayerSlot1";
    }

    if slot == 1 {
        return n"OnMultiplayerSlot2";
    }

    if slot == 2 {
        return n"OnMultiplayerSlot3";
    }

    return n"OnMultiplayerSlot4";
}

/**
 * The slot a pressed row refers to, or -1 for any other menu item.
 */
@addMethod(SingleplayerMenuGameController)
public func MpSlotFromEvent(eventName: CName) -> Int32 {
    if Equals(eventName, n"OnMultiplayerSlot1") {
        return 0;
    }

    if Equals(eventName, n"OnMultiplayerSlot2") {
        return 1;
    }

    if Equals(eventName, n"OnMultiplayerSlot3") {
        return 2;
    }

    if Equals(eventName, n"OnMultiplayerSlot4") {
        return 3;
    }

    return -1;
}

@wrapMethod(SingleplayerMenuGameController)
private func PopulateMenuItemList() -> Void {
    let network = GameInstance.GetNetworkWorldSystem();

    /*
     * NOT LAUNCHED THROUGH NIGHT CITY ONLINE: the mod is not here.
     *
     * Cam's rule, 2026-09-03 - a plain Cyberpunk launch must not be able to connect, play,
     * or create a character. The C++ half already honoured that (Core::Application::Update
     * returns early every frame without --online), but redscript is compiled into the game
     * and runs either way, so the menu was still offering to do all three against a client
     * that could do none of them.
     *
     * wrappedMethod() and nothing else: the vanilla menu, exactly as CDPR built it, with no
     * trace of the mod on it.
     */
    if !IsDefined(network) || !network.IsModEnabled() {
        wrappedMethod();
        return;
    }

    /*
     * LAUNCHED THROUGH THE LAUNCHER: this is a multiplayer client, so the singleplayer
     * entries go.
     *
     * wrappedMethod() is deliberately NOT called. It is the only thing that adds Continue,
     * New Game and Load Game (singleplayerMenu.script:843), and on this server those are
     * three ways to end up playing somebody who is not your character - Continue and Load
     * Game open a local save directly, and New Game starts a story nobody here is in.
     *
     * Settings and Credits are re-added below by hand, because they are the game's and
     * removing them would be taking something away rather than replacing it. Their labels
     * and events are copied from the same shipped source, so they behave identically.
     *
     * NEW CHARACTER still runs the game's own New Game FLOW - it dispatches OnNewGame
     * directly - so removing the menu item costs nothing. The flow was never reached
     * through that button.
     */

    /*
     * CONNECT FIRST, THEN CHOOSE, THEN PLAY. Cam's flow, 2026-09-03.
     *
     * The old single MULTIPLAYER entry did all three at once: opened the connection, waited
     * for the roster, and loaded the world as whoever happened to be active. With one
     * character that is indistinguishable from correct. With four it means the selection
     * screen can never be reached, because the only entry that connects also leaves the menu.
     *
     * So the entry that connects STOPS at the roster, and playing is a second press against
     * a screen showing who you are about to be.
     *
     * NEW CHARACTER carries its warning IN THE LABEL. Making one replaces the character the
     * server holds - hours of somebody's evening - and "NEW CHARACTER" alone reads as ADDING
     * one, which is the misreading that costs people their character. By the time anything
     * in game could warn them, the replacement has happened. A confirmation dialog would be
     * better and needs an API this menu does not obviously have; a label that cannot be
     * misread works today and cannot fail to show.
     *
     * DELETE is a menu ITEM rather than a hand-built button, because menu items are the
     * game's own mechanism and are reliably clickable, focusable and controller-navigable -
     * none of which a widget built at runtime gets for free.
     */
    if IsDefined(network) {
        if network.IsCharacterStatusKnown() {
            // CONNECTED. The roster is here, so the menu is about WHO, not whether.
            this.MpBuildPanel();
            this.MpUpdatePanel();

            // PLAY is first and is the thing they came for. It reads as an answer to the
            // rows under it - "this is who you are, press this to be them" - which is only
            // true because getting here required the server to have answered.
            this.AddMenuItem("PLAY", n"OnMultiplayerContinue");

            /*
             * THE SELECTOR IS THE MENU. zeldfep's call, 2026-09-07: "just a full line menu
             * item after hitting connect".
             *
             * One row per SLOT, drawn between PLAY and the destructive entries. The empty
             * ones are drawn too, because otherwise there is no way to see that a slot is
             * free without trying to use it - and NEW CHARACTER replaces rather than adds,
             * so knowing how many slots are spoken for is the difference between making a
             * character and losing one.
             *
             * This replaces two things at once: the floating YOUR CHARACTERS panel, which
             * showed the same list somewhere you could not press, and SWITCH CHARACTER,
             * which cycled blind. SWITCH also had a bug worth recording - it was DRAWN when
             * GetCharacterSlots() > 1 but only ACTED when the roster held more than one
             * character, so an account with one character in four slots got a button that
             * did nothing at all and said nothing about why. Rows cannot have that bug:
             * each one names the thing it selects.
             */
            let slot = 0;
            let slots = network.GetCharacterSlots();

            // Four events exist, so four rows can be addressed. A server handing out more
            // draws the ones that can actually be pressed rather than dead rows.
            if slots > 4 {
                slots = 4;
            }

            while slot < slots {
                this.AddMenuItem(this.MpSlotLabel(slot), this.MpSlotEvent(slot));
                slot += 1;
            }

            this.AddMenuItem("NEW CHARACTER (REPLACES YOURS)", n"OnMultiplayerNewCharacter");

            // The trash can.
            //
            // Only offered while a character exists to delete - drawing it against an empty
            // account would be a button whose only possible outcome is a refusal.
            if network.HasCharacter() {
                this.AddMenuItem("[ TRASH ]  DELETE CHARACTER", n"OnMultiplayerDeleteCharacter");
            }
        } else {
            // NOT CONNECTED YET. Exactly one multiplayer entry, so there is no question
            // about which one starts things.
            this.AddMenuItem("CONNECT", n"OnMultiplayerCharacters");

            // Creation stays reachable without a connection, because it runs the game's own
            // New Game flow and arms the join on the way through - somebody with no
            // character can still make one while the server is being slow.
            this.AddMenuItem("NEW CHARACTER (REPLACES YOURS)", n"OnMultiplayerNewCharacter");
        }

        /*
         * Settings and Credits, put back by hand.
         *
         * wrappedMethod() is not called on this branch, and it is what normally adds these
         * along with Continue / New Game / Load Game. Dropping the first three is the point;
         * dropping these two would be taking away the game's own screens - and Settings in
         * particular is how somebody fixes their resolution or their controls, which they
         * need at least as much in multiplayer as out of it.
         *
         * Labels and events copied from the shipped source (singleplayer_menu.script:843),
         * so they resolve and behave exactly as the vanilla entries do.
         */
        this.AddMenuItem(GetLocalizedText("UI-Labels-Settings"), n"OnSwitchToSettings");
        this.AddMenuItem(GetLocalizedText("UI-Labels-Credits"), n"OnCreditsPicker");
    }

    // PopulateMenuItemList refreshes at its end, before our item existed. Without
    // refreshing again the entry is in the data but never drawn, which looks exactly
    // like the hook silently doing nothing.
    this.m_menuListController.Refresh();
}

// NOTE THE ref<>. The game's own source declares this as `data : PauseMenuListItemData`,
// and copying that signature across breaks the ENTIRE mod: redscript requires class types
// to be reached through ref or wref, and it aborts all compilation on one bad file. The
// game then starts with no scripts at all - no menu entry, no chat, no HUD - while looking
// like the mod simply did nothing. The .script dialect the game ships is not redscript.
@wrapMethod(SingleplayerMenuGameController)
protected func HandleMenuItemActivate(data: ref<PauseMenuListItemData>) -> Bool {
    // Straight back in - no save picker.
    //
    // The most recent save is loaded purely as a vehicle to get into the world; the
    // server replaces the position on arrival with wherever you actually were. That is
    // what makes this "continue from the server" rather than "continue singleplayer".
    if Equals(data.eventName, n"OnMultiplayerContinue") {
        FTLog(s"[CyberpunkMP] MULTIPLAYER selected from the main menu");

        let network = GameInstance.GetNetworkWorldSystem();
        if !IsDefined(network) {
            FTLogError(s"[CyberpunkMP] No NetworkWorldSystem in the menu - cannot arm the join");
            return true;
        }

        // THE CHARACTER SELECTOR IS ON (2026-08-26).
        //
        // Ask the server WHICH character this account owns before loading anything, then
        // route accordingly: play as it if there is one, run creation only if there is not.
        //
        // Why this had to change. The old path went straight to LoadLastCheckpoint, which
        // loads the player's last SINGLEPLAYER save purely as a vehicle into the world. For
        // someone who has never played singleplayer there is no save to load, so MULTIPLAYER
        // did nothing useful and the only entry that worked was MULTIPLAYER - NEW CHARACTER
        // - which is why players were creating a fresh character every session instead of
        // keeping the one they made. The server has known who they are the whole time; the
        // menu simply never asked.
        //
        // It was switched off in v0.3.92 for a good reason: it had never been exercised
        // live, and this is the one screen where being wrong means nobody can play at all.
        // That risk is now covered rather than avoided - MpSelectorPoll falls back to this
        // exact old behaviour if the server has not answered within 2.5 seconds, so a down
        // or slow server costs a short pause instead of a session.
        if !network.IsConnected() {
            network.Connect();
        }

        let poll = new MpSelectorPoll();
        poll.controller = this;
        poll.attempts = 0;
        poll.enterWhenKnown = true;

        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(poll, 0.25, false);
        return true;
    }

    // Real character creation, via the game's own New Game.
    //
    // This is the answer to something that looked unsolvable for most of a day: the
    // character creator cannot be opened mid-session, because the customization system is
    // native-only - verified against the 2.31 type hierarchy, which has no open event, no
    // mirror class and no creator controller. So it cannot be brought to the player.
    //
    // The player can be brought to IT. New Game runs the creator as part of its normal
    // flow, including body gender, which is the one thing no ripperdoc can change. Pick
    // Phantom Liberty's start on the way through and it lands post-Act-1 at level 15 -
    // the same state the shipped world templates were made from.
    //
    // The join is armed BEFORE the flow starts, on the game system that survives the load,
    // so the connection happens once there is a real world to arrive in. Same mechanism as
    // PLAY; only the route through the menus differs.
    // DELETE, in two presses.
    //
    // The store retires rather than destroys, and the server refuses outright while the
    // character is being played - but neither is a reason to delete on one click. The
    // first press arms and says so on the panel; the second sends it. Walking away from
    // the menu disarms, because the arm lives on the controller and the controller does
    // not survive leaving the screen.
    // "Show me my characters." Opens the connection so the server can say who this account
    // is, then polls until the roster lands and rebuilds the menu with the panel on it.
    //
    // A deliberate press rather than something the menu does by itself: connecting is not
    // free, and most visits to this screen are somebody loading a singleplayer save.
    if Equals(data.eventName, n"OnMultiplayerCharacters") {
        let network = GameInstance.GetNetworkWorldSystem();

        if !IsDefined(network) {
            return true;
        }

        if !network.IsConnected() {
            network.Connect();
        }

        let poll = new MpSelectorPoll();
        poll.controller = this;
        poll.attempts = 0;
        poll.enterWhenKnown = false;

        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(poll, 0.25, false);
        return true;
    }

    // A CHARACTER ROW WAS PRESSED. Select that slot and rebuild the menu against the
    // answer, so the caret moves onto the row they just pressed.
    //
    // Pressing the row you are already on is allowed and costs a round trip: it is not
    // worth a special case, and refusing it would need the menu to explain why.
    //
    // An empty slot answers rather than acts. The server's reply to "select slot 3" when
    // slot 3 is empty is a refusal the player did not ask for, so the menu never sends it.
    let pressedSlot = this.MpSlotFromEvent(data.eventName);

    if pressedSlot >= 0 {
        let network = GameInstance.GetNetworkWorldSystem();

        if !IsDefined(network) || !network.IsConnected() {
            FTLogError(s"[Selector] slot pressed with no connection");
            return true;
        }

        let occupied = false;
        let i = 0u;

        while i < network.GetRosterCount() {
            if network.GetRosterSlot(i) == pressedSlot {
                occupied = true;
            }

            i += 1u;
        }

        if !occupied {
            if IsDefined(this.m_mpDetail) {
                this.m_mpDetail.SetText("That slot is empty - NEW CHARACTER fills it.");
            }

            return true;
        }

        network.SelectCharacterSlot(pressedSlot);

        if IsDefined(this.m_mpDetail) {
            this.m_mpDetail.SetText("switching...");
        }

        // The answer comes back as a fresh roster, so poll for it rather than assuming the
        // switch took. SelectCharacterSlot only says the request was sent.
        let poll = new MpSelectorPoll();
        poll.controller = this;
        poll.attempts = 0;
        poll.enterWhenKnown = false;

        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(poll, 0.25, false);
        return true;
    }

    if Equals(data.eventName, n"OnMultiplayerDeleteCharacter") {
        let network = GameInstance.GetNetworkWorldSystem();

        if !IsDefined(network) || !network.IsConnected() {
            FTLogError(s"[Selector] delete pressed with no connection");
            return true;
        }

        if !this.m_mpDeleteArmed {
            this.m_mpDeleteArmed = true;

            if IsDefined(this.m_mpDetail) {
                this.m_mpDetail.SetText("Press DELETE again to confirm");
            }

            FTLog(s"[Selector] delete armed - waiting for a second press");
            return true;
        }

        this.m_mpDeleteArmed = false;

        FTLog(s"[Selector] delete confirmed - asking the server");
        network.DeleteCharacter();

        if IsDefined(this.m_mpDetail) {
            this.m_mpDetail.SetText("deleting...");
        }

        // The server answers with the list, which is what the panel redraws from. Polling
        // for it rather than assuming success: the delete can be refused, and a panel that
        // cleared itself optimistically would show NO CHARACTER for one that still exists.
        let poll = new MpDeletePoll();
        poll.controller = this;
        poll.attempts = 0;

        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(poll, 0.25, false);
        return true;
    }

    if Equals(data.eventName, n"OnMultiplayerNewCharacter") {
        FTLog(s"[CyberpunkMP] MULTIPLAYER - NEW CHARACTER selected from the main menu");


        let network = GameInstance.GetNetworkWorldSystem();
        if IsDefined(network) {
            // DELIBERATELY DOES NOT CONNECT HERE. RequestJoin only arms the join.
            //
            // Connecting at this point was tried on 2026-08-26 and reverted the same
            // session. Unlike MULTIPLAYER, which loads straight into the world, this branch
            // runs the game's ENTIRE character creator first - lifepath, appearance, the
            // Phantom Liberty start prompt. Being connected through all of that put a live
            // multiplayer session behind the creation UI: the player list drew on top of
            // the lifepath screens and the game froze on the Phantom Liberty prompt.
            //
            // The connection belongs after the creator, when a world exists to arrive in.
            network.RequestJoin();

            // Says out loud that what arrives next REPLACES the stored character.
            //
            // Without this the server has no way to tell the difference. It captures an
            // appearance only for a player who has none, so anybody with an existing
            // character went through the whole creator and was then spawned as the
            // character they had just replaced - the creation was silently discarded.
            //
            // The client is the only side that knows which menu entry was pressed, so the
            // client is what says so.
            network.MarkNewCharacter();
        } else {
            FTLogError(s"[CyberpunkMP] No NetworkWorldSystem in the menu - cannot arm the join");
        }

        // Handed to the game's own New Game entry rather than starting one ourselves.
        // RequestNewGame does not exist on the requests handler - checked - and this is the
        // event the menu's own New Game item spawns, so it is the real flow with the real
        // lifepath and creator screens.
        this.m_menuEventDispatcher.SpawnEvent(n"OnNewGame");

        return true;
    }

    return wrappedMethod(data);
}
