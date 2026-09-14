module CyberpunkMP

/**
 * The vehicle radio works on a server - it could neither be opened nor switched off.
 *
 * Reported twice (zeldfep and Cam, 2026-09-10): in a multiplayer car the radio will not
 * open, will not change station, and will not turn off.
 *
 * THE CAUSE is not the car and not the mount - both were checked. MP cars are spawned from
 * real vehicle records (VehicleSystem.reds SpawnVehicle), so they pass the radio's
 * CarObject/BikeObject check, and the player is mounted by a real MountingRequest into the
 * driver slot, so the PSM vehicle state is set.
 *
 * It is one blackboard value, UIGameData.Popup_Radio_Enabled, which every radio path reads:
 *
 *   - a tap on R    VehicleComponent.OnAction           (vehicleComponent.script:5408)
 *   - holding R     VehicleInsideWheelDecisions leaves the wheel at once when it is false
 *                                                         (quickSlotsTransitions.script:700)
 *   - the popup     PopupManager.TrySpawnVehicleRadioPopup (popupManager.script:577)
 *
 * and exactly ONE thing writes it: HotkeyConsumableWidgetController.UpdateBlackboard
 * (dpad_hint.script:236), from IsRadioEnabled() - which is false whenever the quest fact
 * unlock_car_hud_dpad is 0. The main story sets that fact early, when V gets a car. Our
 * characters never ran that story: it is absent from both template saves' fact lists and
 * from Cam's own saves. So on a server the radio is quest-locked in every car, which is
 * both halves of the report at once.
 *
 * WHY NOT JUST SET THE FACT, the way ep1_side_content opens Dogtown. The same fact is also
 * what keeps vanilla vehicle SUMMONING shut (vehicleSystem.script:70
 * IsSummoningVehiclesRestricted). A summoned vanilla car exists only in that one player's
 * game - nobody else would see it - so the fact is accidentally doing useful work there.
 * This lifts the quest lock for the radio alone and leaves summoning where it is.
 *
 * WHAT STILL BLOCKS THE RADIO, deliberately untouched: driver combat, a police vehicle, the
 * VehicleBlockRadioInput tag, and vehicle scenes. Only the quest lock is waived.
 *
 * Only while connected (MpQuestsSilenced), so a character taken back to singleplayer has
 * the game's own rule. The controller re-evaluates this on every vehicle entry
 * (OnPlayerEnteredVehicle -> RefreshInPoliceVehicle -> UpdateBlackboard), so connecting
 * after the HUD attached still takes effect the next time somebody gets in a car.
 */
@wrapMethod(HotkeyConsumableWidgetController)
private func IsRadioEnabled() -> Bool {
    if wrappedMethod() {
        return true;
    }

    if !MpQuestsSilenced() {
        return false;
    }

    // The vanilla condition with the quest term removed and nothing else changed.
    return !this.m_IsInDriverCombat
        && !this.m_IsPoliceVehicle
        && !this.m_isRadioBlocked
        && !this.m_isInVehicleScene;
}
