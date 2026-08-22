#pragma once

#include <cstdint>

// What a combatant is holding, owned by the server.
//
// Ammunition is the reason this exists. A client-owned magazine is an infinite magazine:
// nothing stops a modified client reporting "still full" after every shot, and no amount of
// validation elsewhere recovers from the server not knowing how many bullets somebody has.
// So the count lives here and the client is corrected when it disagrees.
//
// Deliberately NOT a model of Cyberpunk's weapon system. There is no fire rate, no damage
// curve, no attachment list, no mod slots - reimplementing those server-side would be months
// of work that still drifted from the game every patch. What the server needs is the
// smallest amount of state that makes cheating expensive: which weapon, how many rounds, and
// whether a reload is genuinely in progress.
struct WeaponComponent
{
    // TweakDB id of the equipped weapon. Zero means empty-handed, which is a real state -
    // fists are not a weapon record.
    uint64_t WeaponId{0};

    uint32_t MagazineAmmo{0};
    uint32_t ReserveAmmo{0};

    // Set on reload start, cleared on reload complete. A completion without a matching start
    // is how a client refills instantly, so the pair is tracked rather than trusted.
    bool Reloading{false};

    // Server time the reload began, so a completion that arrives implausibly early can be
    // refused. The SERVER's clock - a duration measured with the client's own timestamps is
    // not a limit on the client.
    uint64_t ReloadStartedMs{0};

    // Server time of the last accepted shot, for fire-rate validation.
    //
    // One floor for every weapon rather than per-weapon rates, for the same reason as above:
    // the server does not model weapons. This catches the difference between a fast weapon
    // and a script firing every frame, which is the attack worth catching.
    uint64_t LastShotMs{0};

    // Rises per owner. Same replay and duplicate rejection as combat events, and separate
    // from the combat sequence so a burst of fire and a burst of hits cannot invalidate
    // each other.
    uint32_t LastSequence{0};
    bool HasSequence{false};
};
