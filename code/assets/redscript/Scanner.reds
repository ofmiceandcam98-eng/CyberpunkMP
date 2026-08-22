module CyberpunkMP

import CyberpunkMP.World.*

/**
 * Scanning another player shows THEIR name.
 *
 * THE BUG THIS FIXES, because the symptom was misleading. Scanning a player returned a
 * random NPC name - and a DIFFERENT random name depending on who was doing the scanning,
 * which is what made it obvious the name was being invented locally rather than sent.
 *
 * The chain: Character.Muppet inherits from Character.TPP_Player, which chains to
 * Character.Panam. Her display name came with it, so every remote player scanned as PANAM,
 * affiliation and criminal record included. The fix at the time was to blank the three
 * display-name fields on our record - correct, and it stopped the Panam leak.
 *
 * But nothing then supplied a replacement. The game builds a scanner name from
 * GameObject.GetDisplayName(), and for a puppet with no name of its own it falls back to
 * its own generic-NPC naming - which is seeded per client. Hence random, and hence
 * different for every observer.
 *
 * WHY THIS HOOK AND NOT ANOTHER. GetDisplayName() is a native import and cannot be
 * wrapped. CompileScannerChunks() is `public const virtual` on GameObject
 * (core/entity/gameObject.script:2779) and CAN be, so the game builds its chunks as
 * normal and the name chunk is replaced afterwards for our puppets only. Everything the
 * scanner shows about real NPCs is untouched.
 *
 * The name itself has been held client-side all along - AppearanceSystem keeps it per
 * entity so nameplates can use it. It simply had no route into script, because
 * std::string does not marshal across RTTI. GetNetworkPlayerName is that route.
 */
@wrapMethod(GameObject)
public const func CompileScannerChunks() -> Bool {
    let result = wrappedMethod();

    let network = GameInstance.GetNetworkWorldSystem();
    if !IsDefined(network) {
        return result;
    }

    let appearance = network.GetAppearanceSystem();
    if !IsDefined(appearance) {
        return result;
    }

    // Empty for anything that is not a network player - real NPCs, devices, vehicles and
    // the local player all fall through here untouched.
    let name = appearance.GetNetworkPlayerName(this.GetEntityID());
    if Equals(name, "") {
        return result;
    }

    // Written after wrappedMethod deliberately. The game has already put its own name in
    // the blackboard by this point; replacing it is the whole job, and doing it first
    // would simply be overwritten.
    let chunk = new ScannerName();
    chunk.Set(name);

    GameInstance.GetBlackboardSystem(GetGameInstance())
        .Get(GetAllBlackboardDefs().UI_ScannerModules)
        .SetVariant(GetAllBlackboardDefs().UI_ScannerModules.ScannerName, chunk, true);

    return result;
}
