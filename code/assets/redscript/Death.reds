module CyberpunkMP

import CyberpunkMP.World.*

// Nobody flatlines on a multiplayer server.
//
// The vanilla death screen is the single worst thing that can happen in a session. It is
// not just an interruption: its only real option is Load Last Checkpoint, and loading
// rebuilds the world while the server still holds the old puppet, so the player who died
// leaves everyone else watching a corpse. One person falling off a roof ends the evening
// for the group.
//
// There are three layers here, because two were not enough.
//
//   1. A FLOOR under the health pool, so health never reaches zero and the dead state is
//      never entered. This is the same mechanism the game's own NPCs use to be "defeated"
//      rather than killed. It handles ordinary damage.
//
//   2. RE-ARMING that floor after every revive. A custom limit is consumed when it is
//      reached, so the first death was caught and the second was not. That is the shape of
//      what Cam saw on v0.1.31 - one player fine, another flatlined - and it is why "it
//      worked" and "it did not work" were both true reports of the same build.
//
//   3. A BACKSTOP on the death menu itself, for the deaths a health floor cannot catch:
//      falling out of the world, drowning, a scripted quest kill. It hides the menu before
//      it draws, revives, and closes it.
//
// Layer 3 carries a watchdog. Hiding a menu that then refuses to close would trade a
// visible problem for an invisible one - a player trapped behind a menu they cannot see is
// worse off than a player looking at FLATLINED. If the menu is still up after a few
// seconds, it comes back.

// Health is a pool measured 0..100, so this is one percent, not one hit point. Zero is not
// usable: at exactly the bottom the race between this callback and the engine's own death
// handling is too tight to rely on.
public static func MpDeathFloor() -> Float = 1.0

// Puts the floor under the player's health and starts listening for it.
//
// Only while connected. Applying this in singleplayer would quietly make Cam immortal in
// his own save, and an unkillable player who never asked for one is a bug even though it
// looks like a small one.
public func MpArmDeathFloor(player: ref<PlayerPuppet>) -> Void {
    if !IsDefined(player) {
        return;
    }

    let system = GameInstance.GetNetworkWorldSystem();
    if !IsDefined(system) || !system.IsConnected() {
        return;
    }

    let pools = GameInstance.GetStatPoolsSystem(player.GetGame());
    let id = Cast<StatsObjectID>(player.GetEntityID());

    if !IsDefined(player.m_mpDeathGuard) {
        let guard = new MpDeathGuard();
        guard.player = player;
        player.m_mpDeathGuard = guard;

        pools.RequestRegisteringListener(id, gamedataStatPoolType.Health, guard);
    }

    pools.RequestSettingStatPoolValueCustomLimit(id, gamedataStatPoolType.Health, MpDeathFloor(), null);
}

// The message Cam asked for: our own, brief, and gone in a few seconds.
//
// Deliberately not a menu. The whole point is that being downed costs you a moment and a
// walk back, not a screen you have to dismiss.
public func MpShowFlatlinedMessage() -> Void {
    let message: SimpleScreenMessage;
    message.isShown = true;
    message.duration = 4.0;
    message.message = "YOU WERE FLATLINED";

    GameInstance.GetBlackboardSystem(GetGameInstance())
        .Get(GetAllBlackboardDefs().UI_Notifications)
        .SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(message), true);
}

public class MpDeathGuard extends ScriptStatPoolsListener {
    public let player: wref<PlayerPuppet>;

    protected cb func OnStatPoolCustomLimitReached(value: Float) -> Bool {
        let system = GameInstance.GetNetworkWorldSystem();

        // Singleplayer dies normally. Someone playing with the mod installed but not
        // connected should get the game's own behaviour, untouched.
        if !IsDefined(system) || !system.IsConnected() {
            return false;
        }

        if !IsDefined(this.player) {
            return false;
        }

        FTLog(s"[Death] downed - reviving instead of flatlining");

        MpShowFlatlinedMessage();
        system.RevivePlayer();

        // Re-arm. The limit that just fired is spent, and without this the next death is
        // an ordinary one.
        MpArmDeathFloor(this.player);

        return true;
    }
}

@addField(PlayerPuppet)
public let m_mpDeathGuard: ref<MpDeathGuard>;

@wrapMethod(PlayerPuppet)
protected cb func OnGameAttached() -> Bool {
    let result = wrappedMethod();

    // Covers loading a save while already connected. The usual order is the other way
    // round - the player attaches, then MULTIPLAYER connects - which is what
    // NetworkWorldSystem.OnConnected is for.
    MpArmDeathFloor(this);

    return result;
}

// ---------------------------------------------------------------------------
// Layer 3: the death menu never gets to draw.
// ---------------------------------------------------------------------------

@addField(DeathMenuGameController)
public let m_mpSuppressed: Bool;

@wrapMethod(DeathMenuGameController)
protected cb func OnInitialize() -> Bool {
    let system = GameInstance.GetNetworkWorldSystem();

    if IsDefined(system) && system.IsConnected() {
        FTLogError(s"[Death] the death menu opened on a server - suppressing it");

        // Hidden BEFORE anything else, so FLATLINED does not appear for even one frame.
        let root = this.GetRootCompoundWidget();
        if IsDefined(root) {
            root.SetVisible(false);
            root.SetOpacity(0.0);
        }

        this.m_mpSuppressed = true;

        MpShowFlatlinedMessage();
        system.RevivePlayer();

        // Give the menu system a moment to finish opening before asking it to close -
        // m_menuEventDispatcher is not wired up yet at this point in OnInitialize.
        let close = new MpCloseDeathMenuCallback();
        close.controller = this;
        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(close, 0.1, false);

        // The watchdog. If the close above does not take, put the menu back rather than
        // leave someone stuck behind an invisible one.
        let watchdog = new MpDeathMenuWatchdogCallback();
        watchdog.controller = this;
        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(watchdog, 3.0, false);

        // Deliberately does NOT chain. The vanilla controller is never built, so there is
        // no FLATLINED text and no Load Last Checkpoint button to build it from.
        return false;
    }

    // Not connected: the game's own death menu, with the chat widget the mod adds to it.
    let result = wrappedMethod();
    this.AsyncSpawnFromExternal(inkWidgetRef.Get(this.m_buttonHintsManagerRef),
                                r"mods\\cyberpunkmp\\multiplayer_ui.inkwidget", n"chat", this, n"OnChatSpawn");
    return result;
}

public class MpCloseDeathMenuCallback extends DelayCallback {
    public let controller: wref<DeathMenuGameController>;

    public func Call() -> Void {
        if !IsDefined(this.controller) {
            return;
        }

        if IsDefined(this.controller.m_menuEventDispatcher) {
            this.controller.m_menuEventDispatcher.SpawnEvent(n"OnBack");
        }
    }
}

public class MpDeathMenuWatchdogCallback extends DelayCallback {
    public let controller: wref<DeathMenuGameController>;

    public func Call() -> Void {
        // A controller that closed has already been torn down, so reaching here with a
        // live one means the close did not work.
        if !IsDefined(this.controller) || !this.controller.m_mpSuppressed {
            return;
        }

        FTLogError(s"[Death] the death menu would not close - showing it rather than trapping the player");

        let root = this.controller.GetRootCompoundWidget();
        if IsDefined(root) {
            root.SetVisible(true);
            root.SetOpacity(1.0);
        }
    }
}
