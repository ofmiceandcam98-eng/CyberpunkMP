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
 * HOW IT IS ENFORCED
 *
 * The difficulty control is built natively - it exists nowhere in the game's redscript, so
 * there is no menu class to wrap. What CAN be reached is the setting, and
 * ConfigVarListString exposes SetValues - so the LIST is cut down to a single entry and
 * VeryHard becomes the only thing that can be seen or picked.
 *
 * The first version only pinned the value on a poll. Cam tested it: "it still offers you to
 * pick a difficulty, very hard should be the only one that shows and works". He was right -
 * a menu that lets you choose Easy and silently undoes it two seconds later looks broken
 * rather than deliberate. The pin stays as a backstop for a value stored before this ran.
 *
 * ONLY WHILE CONNECTED. Somebody's singleplayer game is theirs - the poll stops the moment
 * they disconnect and their own setting is left exactly as they had it.
 */
public class MpDifficultyLock extends DelayCallback {
    // The choices the player had before we cut them down, so they can be handed back.
    //
    // This matters more than it looks. SetValues writes to the player's OWN settings - it
    // is not a per-session overlay - so a restriction left in place would follow them into
    // their singleplayer game and leave them permanently unable to pick anything but
    // VeryHard there. Taking away a menu on our server is the intent; taking it away in
    // somebody's own game is an overstep.
    public let originalValues: array<String>;

    public func Call() -> Void {
        let network = GameInstance.GetNetworkWorldSystem();

        /*
         * GATED ON THE LAUNCHER, NOT ON THE CONNECTION. Changed 2026-09-03.
         *
         * IsConnected() was too late, and Cam found it: "when you make a character i can
         * still choose difficulties". The difficulty screen is part of the New Game flow,
         * which runs BEFORE the world exists and before anything connects - so the lock had
         * not started yet and the choice was still on offer at the one moment it matters
         * most, when the character is being made.
         *
         * --online is the honest test. It means the launcher started this game, so it is a
         * multiplayer session from the main menu onward, whether or not a socket is open at
         * this instant. It also survives a reconnect, where IsConnected briefly goes false
         * and would otherwise hand the choices back mid-session.
         */
        if !IsDefined(network) || !network.IsModEnabled() {
            // Not a launcher session. Give the choices back and stop - no re-schedule.
            MpRestoreDifficultyChoices(this.originalValues);
            return;
        }

        this.originalValues = MpRestrictDifficultyToHardest(network, this.originalValues);

        // Ten seconds. Frequent enough that nobody plays a fight on Easy, slow enough that
        // it is not a settings write every frame.
        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(this, 10.0, false);
    }
}

/**
 * Start the lock from the MAIN MENU, before anything can offer a choice.
 *
 * Cam, 2026-09-03: "when you make a character i can still choose difficulties". The
 * restriction used to begin in MultiplayerGameController - in the world, after connecting -
 * and the difficulty screen belongs to the New Game flow, which happens before either. So
 * the one screen that actually asks was the one screen running before the lock existed.
 *
 * Idempotent and cheap: MpRestrictDifficultyToHardest returns immediately once the list is
 * already down to one entry, so pressing the menu repeatedly costs nothing. Armed once per
 * controller rather than per press, because the lock reschedules itself and a second one
 * would double the polling forever.
 */
@addField(SingleplayerMenuGameController)
private let m_mpDifficultyArmed: Bool;

@wrapMethod(SingleplayerMenuGameController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();

  let network = GameInstance.GetNetworkWorldSystem();

  if IsDefined(network) && network.IsModEnabled() && !this.m_mpDifficultyArmed {
    this.m_mpDifficultyArmed = true;

    MpForceHardestDifficulty(network, true);

    let lock = new MpDifficultyLock();
    lock.originalValues = MpRestrictDifficultyToHardest(network, lock.originalValues);
    GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(lock, 10.0, false);
  }

  return result;
}

/**
 * Puts the full difficulty list back after a session ends.
 *
 * Called when the lock notices the connection has gone. If nothing was ever restricted the
 * array is empty and this does nothing.
 */
public func MpRestoreDifficultyChoices(original: array<String>) -> Void {
  if ArraySize(original) <= 1 {
    return;
  }

  let settings = GameInstance.GetSettingsSystem(GetGameInstance());
  if !IsDefined(settings) {
    return;
  }

  let option = settings.GetVar(n"/gameplay/difficulty", n"GameDifficulty") as ConfigVarListString;
  if !IsDefined(option) {
    return;
  }

  option.SetValues(original);

  // Left ON the hardest rather than snapped back to whatever they had. They played the
  // session on it; silently dropping them to Easy on disconnect would be its own surprise.
  let hardest = option.GetIndexFor("VeryHard");
  if hardest >= 0 {
    option.SetIndex(hardest);
  }

  FTLog(s"[Difficulty] session over - difficulty choices restored (\(ArraySize(original)) options)");
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

/**
 * Leaves VeryHard as the ONLY value the difficulty option offers.
 *
 * The first version of this just pinned the value on a ten-second poll, and Cam tested it
 * and said what it actually needed: "it still offers you to pick a difficulty, 'very hard'
 * should be the only one that shows and works". He was right - a menu that lets you choose
 * Easy and then silently undoes it two seconds later is worse than no choice at all,
 * because it looks broken rather than deliberate.
 *
 * ConfigVarListString exposes SetValues, so the list itself can be cut down to one entry
 * rather than policed after the fact. The pin above stays as a backstop for the value that
 * was already stored before this ran.
 *
 * Returns the list as it was BEFORE restricting, so the caller can hand it back on
 * disconnect - see MpRestoreDifficultyChoices. SetValues writes to the player's own
 * settings, so a restriction left in place would follow them into their singleplayer game.
 * `known` is that same list coming back in on later polls, so it is captured once.
 */
public func MpRestrictDifficultyToHardest(network: ref<NetworkWorldSystem>, known: array<String>) -> array<String> {
  let settings = GameInstance.GetSettingsSystem(GetGameInstance());
  if !IsDefined(settings) {
    return known;
  }

  let option = settings.GetVar(n"/gameplay/difficulty", n"GameDifficulty") as ConfigVarListString;
  if !IsDefined(option) {
    return known;
  }

  let values = option.GetValues();

  // Already restricted - nothing to do, and re-running SetValues every ten seconds would be
  // a settings write per poll for no reason. Hand back whatever was captured the first time.
  if ArraySize(values) <= 1 {
    return known;
  }

  let hardestName = "VeryHard";
  if !ArrayContains(values, hardestName) {
    hardestName = values[ArraySize(values) - 1];
  }

  let only: array<String>;
  ArrayPush(only, hardestName);

  option.SetValues(only);
  option.SetIndex(0);

  network.ScriptLog(s"difficulty: menu restricted to \(hardestName) only - was offering \(ArraySize(values)) choices");

  return values;
}
