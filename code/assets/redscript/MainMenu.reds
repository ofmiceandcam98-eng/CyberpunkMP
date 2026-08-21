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

    public func Call() -> Void {
        let network = GameInstance.GetNetworkWorldSystem();

        if !IsDefined(network) || !IsDefined(this.controller) {
            return;
        }

        if network.IsCharacterStatusKnown() {
            this.controller.MpEnterWithCharacter();
            return;
        }

        this.attempts += 1;

        if this.attempts >= 10 {
            FTLogError(s"[CyberpunkMP] the server never said what character this account has - is it up?");
            return;
        }

        let again = new MpSelectorPoll();
        again.controller = this.controller;
        again.attempts = this.attempts;

        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(again, 0.25, false);
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
        this.GetSystemRequestsHandler().LoadLastCheckpoint(false);
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

@wrapMethod(SingleplayerMenuGameController)
private func PopulateMenuItemList() -> Void {
    wrappedMethod();

    // Two entries, and this time they genuinely differ.
    //
    // An earlier version had CONTINUE and LOAD GAME, which both loaded a save and differed only in
    // whether you picked it - a choice that changed nothing once the server started owning
    // appearance and position. That pair was rightly collapsed into one.
    //
    // These two are different actions. PLAY drops you into the world. NEW CHARACTER runs
    // the game's own New Game flow, which is the ONLY place body gender can be chosen -
    // ripperdocs change everything about how you look except that, and the customization
    // system is native-only so it cannot be opened on demand. Going through New Game is
    // therefore the only route to real character creation that exists.
    this.AddMenuItem("MULTIPLAYER", n"OnMultiplayerContinue");

    // The warning is IN THE LABEL.
    //
    // Making a character replaces the one the server holds, and that is hours of
    // somebody's evening. "NEW CHARACTER" on its own reads as ADDING one, which is exactly
    // the misreading that costs people their character - and by the time anything could
    // warn them from in game, the replacement has already happened.
    //
    // A confirmation dialog would be better and needs an API this menu does not obviously
    // have. A label that cannot be misread is available right now and cannot fail to show.
    this.AddMenuItem("MULTIPLAYER - NEW CHARACTER (REPLACES YOURS)", n"OnMultiplayerNewCharacter");

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

        // ASK THE SERVER FIRST, then decide where this player goes.
        //
        // This used to load a save immediately and connect once the world was up, which
        // meant the server's answer to "what character does this account own" arrived
        // AFTER the player was already standing in the world as it. There was no moment in
        // which a selector could exist.
        //
        // So connect here, from the menu, and wait. The socket survives the load into the
        // world - only an explicit Disconnect closes it - and the spawn is held back until
        // EnterWorld, so authenticating early does not put anybody anywhere.
        //
        // Connecting is skipped when we already are: re-entering the menu after a
        // disconnect-and-return should not stack a second connection, and each Connect
        // aborts the previous one.
        if !network.IsConnected() {
            FTLog(s"[CyberpunkMP] not connected yet - signing in before offering a character");
            network.Connect();
        }

        // The wait is a poll rather than a callback because the answer arrives on the
        // network thread into native state; there is no script event to hang off. A
        // quarter second is well inside a human's tolerance for a menu press and well
        // outside the round trip.
        let poll = new MpSelectorPoll();
        poll.controller = this;
        poll.attempts = 0;

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
    if Equals(data.eventName, n"OnMultiplayerNewCharacter") {
        FTLog(s"[CyberpunkMP] MULTIPLAYER - NEW CHARACTER selected from the main menu");


        let network = GameInstance.GetNetworkWorldSystem();
        if IsDefined(network) {
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
