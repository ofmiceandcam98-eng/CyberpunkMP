module CyberpunkMP

import CyberpunkMP.World.*

/**
 * Everyone plays on the hardest difficulty, and nobody gets to change it.
 *
 * Cam, 2026-08-28: "make sure we remove the ability to change difficulties in game, we will
 * be forcing everyone to play on the hardest difficulty."
 *
 * WHAT THE SETTING ACTUALLY IS
 *
 * Read from the game's own UserSettings.json rather than guessed:
 *
 *   group "/gameplay/difficulty", option "GameDifficulty", type string_list
 *   values ["Story", "Easy", "Hard", "VeryHard"]   - index 3 is the hardest
 *
 * Note the list has no "Normal": Story, Easy, Hard, VeryHard. Hardcoding index 3 would
 * work today and break the day a patch adds one, so the index is looked up BY NAME through
 * GetIndexFor("VeryHard") and the constant below is only a fallback.
 *
 * WHY FORCING RATHER THAN HIDING THE OPTION
 *
 * The difficulty control is built natively - it does not exist anywhere in the game's
 * redscript, so there is no option list to filter and no menu class to wrap. What CAN be
 * reached is the setting itself. So the setting is pinned instead: set on connect, and
 * re-set on a slow poll, so lowering it in the menu lasts a couple of seconds and then
 * snaps back.
 *
 * That is honestly a pin rather than a removal, and it is worth being precise about the
 * difference: a determined player can see the option and watch it revert. What they cannot
 * do is play on it.
 *
 * ONLY WHILE CONNECTED. Somebody's singleplayer game is theirs - the poll stops the moment
 * they disconnect and their own setting is left exactly as they had it.
 */
public class MpDifficultyLock extends DelayCallback {
    public func Call() -> Void {
        let network = GameInstance.GetNetworkWorldSystem();

        if !IsDefined(network) || !network.IsConnected() {
            return; // disconnected - stop re-scheduling, leave their setting alone
        }

        MpForceHardestDifficulty(network, false);

        // Ten seconds. Frequent enough that nobody plays a fight on Easy, slow enough that
        // it is not a settings write every frame.
        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(this, 10.0, false);
    }
}

/**
 * Pins the difficulty to the hardest value the game offers.
 *
 * `announce` is for the first call, so the log says it happened once rather than every ten
 * seconds forever.
 */
public func MpForceHardestDifficulty(network: ref<NetworkWorldSystem>, announce: Bool) -> Void {
  let settings = GameInstance.GetSettingsSystem(GetGameInstance());

  if !IsDefined(settings) {
    if announce {
      network.ScriptLog("difficulty: no settings system - cannot pin the difficulty");
    }
    return;
  }

  let option = settings.GetVar(n"/gameplay/difficulty", n"GameDifficulty") as ConfigVarListString;

  if !IsDefined(option) {
    if announce {
      network.ScriptLog("difficulty: /gameplay/difficulty GameDifficulty not found - cannot pin it");
    }
    return;
  }

  // By name, not by number. The list is Story/Easy/Hard/VeryHard today and a patch that
  // adds one would silently move the hardest entry somewhere else.
  let hardest = option.GetIndexFor("VeryHard");

  if hardest < 0 {
    hardest = ArraySize(option.GetValues()) - 1;
  }

  if option.GetIndex() == hardest {
    if announce {
      network.ScriptLog(s"difficulty: already \(option.GetValue()) - pinned there for this session");
    }
    return;
  }

  let was = option.GetValue();
  option.SetIndex(hardest);

  network.ScriptLog(s"difficulty: forced \(was) -> \(option.GetValueFor(hardest)) - this server is hardest-only");
}
