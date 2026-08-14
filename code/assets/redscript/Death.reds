module CyberpunkMP

import CyberpunkMP.World.*

// Nobody truly dies on a multiplayer server.
//
// Cyberpunk's death flow assumes one player and a save file: it blocks input, shows the
// death menu, and waits for you to load. On a shared server that is wrong in three
// separate ways.
//
// It strands the player. Loading a save detaches and rebuilds their world, and the server
// is still holding the puppet from before - so everyone else keeps seeing a motionless
// copy of someone who has already reloaded. That is exactly the desync Cam hit: his
// friend died, loaded a save, and the session stopped working for both of them.
//
// It also loses the session. A save is singleplayer state from before any of this
// happened, so reloading throws away everything the server knows about where you were.
//
// And it is wrong for roleplay. Being downed should be a setback inside the story, not an
// exit from it.
//
// So death becomes: get back up, somewhere else. The server decides where - see
// /setspawn.

@wrapMethod(PlayerPuppet)
protected cb func OnDeath(evt: ref<gameDeathEvent>) -> Bool {
    let system = GameInstance.GetNetworkWorldSystem();

    // Singleplayer is left completely alone. Someone playing the game normally with the
    // mod installed should die exactly as the game intends.
    if !IsDefined(system) || !system.IsConnected() {
        return wrappedMethod(evt);
    }

    FTLog(s"[Death] downed on the server - reviving instead of dying");

    // Deliberately NOT calling wrappedMethod. That is what starts the death sequence:
    // it applies BlockAllMenu, tells telemetry you died, cancels autosaves, and brings up
    // the death menu. None of that should happen here.
    system.RevivePlayer();

    return true;
}
