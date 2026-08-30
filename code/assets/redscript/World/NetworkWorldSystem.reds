module CyberpunkMP.World

import Codeware.*
import CyberpunkMP.*

/**
 * The second half of a seat promotion, run after the unmount has had time to land.
 *
 * A separate callback rather than a delay on the mount itself: MountingRequest has no
 * delay field - only UnmountingRequest does - so the wait has to live out here.
 */
public class MpTakeDriverSeatCallback extends DelayCallback {
    public let player: wref<GameObject>;
    public let vehicleId: EntityID;

    public func Call() -> Void {
        if !IsDefined(this.player) {
            return;
        }

        let info: MountingInfo;
        info.childId = this.player.GetEntityID();
        info.parentId = this.vehicleId;
        info.slotId.id = VehicleComponent.GetDriverSlotName();

        let request = new MountingRequest();
        request.lowLevelMountingInfo = info;
        request.mountData = new MountEventData();

        // The player is being moved into a seat of a car they are standing in, not
        // teleported to one - keeping the position avoids the lurch that mounting from
        // scratch produces.
        request.preservePositionAfterMounting = true;

        GameInstance.GetMountingFacility(GetGameInstance()).Mount(request);

        FTLog(s"[NetworkWorldSystem] took the driver seat of \(EntityID.GetHash(this.vehicleId))");
    }
}

public native class NetworkWorldSystem extends IGameSystem {
    public native func Connect() -> Void;
    public native func Disconnect() -> Void;
    public native func RequestJoin() -> Void;

    // Declared here as well as in C++, because native means "the body lives in C++", not
    // "redscript will work it out". Without this line the call fails with
    // UNRESOLVED_METHOD and takes every other script in the mod down with it.
    public native func MarkNewCharacter() -> Void;

    // Handing the player's possessions to the server. Declared on both sides, like every
    // native here - a missing declaration fails with UNRESOLVED_METHOD and takes every
    // other script in the mod down with it.
    public native func BeginInventoryCapture() -> Void;
    public native func AddInventoryItem(id: Uint64, quantity: Uint32) -> Void;
    public native func EndInventoryCapture(money: Int64) -> Void;

    // The chosen lifepath, as the game's own gamedataLifePath value put through EnumInt.
    // The server turns it into a starting kit; nothing on this side needs to know which.
    public native func SetLifepath(value: Uint32) -> Void;

    // TDBID exposes ToNumber and no inverse - the conversion is one-way in script, so the
    // rebuild happens in C++ where a TweakDBID is just its number.
    public native func TdbidFromNumber(value: Uint64) -> TweakDBID;
    public native func AddProficiency(profType: Uint32, level: Int32) -> Void;
    public native func AddAttribute(statType: Uint32, value: Int32) -> Void;
    public native func AddPerk(perkType: Uint32, level: Int32) -> Void;
    public native func GetFactCount() -> Uint32;
    public native func GetFactName(index: Uint32) -> String;
    public native func GetFactValue(index: Uint32) -> Int32;
    public native func AddVehicle(name: String) -> Void;
    public native func GetTargetMoney() -> Int32;
    public native func GetRestoreVehicleCount() -> Uint32;
    public native func GetRestoreVehicle(index: Uint32) -> String;
    public native func GetRestorePerkCount() -> Uint32;
    public native func GetRestorePerkType(index: Uint32) -> Uint32;
    public native func GetRestorePerkLevel(index: Uint32) -> Int32;
    public native func GetRestoreAttributeCount() -> Uint32;
    public native func GetRestoreAttributeType(index: Uint32) -> Uint32;
    public native func GetRestoreAttributeValue(index: Uint32) -> Int32;
    public native func GetRestoreCount() -> Uint32;
    public native func GetRestoreId(index: Uint32) -> Uint64;
    public native func GetRestoreQuantity(index: Uint32) -> Uint32;
    public native func GetRestoreMoney() -> Int32;

    // "Put the restored items ON" - true only for a starter-kit grant.
    public native func ShouldEquipRestored() -> Bool;
    public native func ClearEquipRestored() -> Void;

    // FTLog goes somewhere we cannot read. This lands in CyberpunkMP.log.
    public native func ScriptLog(text: String) -> Void;

    // Walk UP the widget tree from `widget` and log every ancestor.
    //
    // Native because redscript cannot: inkWidget in 2.31 exposes Reparent and no
    // GetParentWidget, so script can only ever walk DOWN. That gap is why the chat-box bug
    // has been guessed at rather than measured - every probe so far could only confirm that
    // the widget itself is correct, which it is.
    public native func LogWidgetAncestry(widget: ref<inkWidget>, label: String) -> Void;
    public native func ConsumeJoinRequest() -> Bool;

    // What the SERVER says this account owns. Never derived from the save on disk - that
    // save is the world template, not anybody's identity.
    //
    // Status-known is separate from has-character on purpose: before the authentication
    // reply lands both are false, and a selector that could not tell those apart would
    // offer CREATE to somebody who already has a character.
    public native func IsCharacterStatusKnown() -> Bool;
    public native func HasCharacter() -> Bool;
    public native func GetCharacterName() -> String;
    public native func GetCharacterLevel() -> Int32;
    public native func HasCharacterSpawnedBefore() -> Bool;

    // Sends the spawn that was held back while the player sat on the selector. A no-op
    // when none is owed, so the old in-world connect path cannot announce twice.
    public native func EnterWorld() -> Void;

    // Retires this account's character. The confirmation happens on the client, in front
    // of the person losing it, before this is called.
    public native func DeleteCharacter() -> Void;

    // Why the last request was refused, empty when nothing was. Shown on the panel rather
    // than swallowed - a button that appears to do nothing is the worst outcome here.
    public native func GetCharacterError() -> String;

    // Why the last CONNECTION was refused - the server's own sentence, plus the code
    // (EDenialCode in GameServer.h) and, on manifest denials, the version the server
    // wanted. Empty/zero when the last disconnect was nobody refusing anything. This is
    // what OnDisconnected turns into the on-screen popup; before it existed a denied
    // join was a bare disconnect with the reason in a log file.
    public native func GetDenialMessage() -> String;
    public native func GetDenialCode() -> Uint32;
    public native func GetRequiredManifest() -> String;

    // ---------------------------------------------------------------------------
    // Voice devices and capture, from Windows' own audio endpoints.
    //
    // Index-based rather than returning a list, the same shape the restore inventory
    // uses: scalars cross the native boundary far more comfortably than arrays of
    // structs. Refresh takes the snapshot; the rest read it.
    //
    // There is no per-manufacturer anything here or below. A Scarlett, an Apollo and a
    // £20 headset are all just endpoints, which is the entire reason for going through
    // WASAPI instead of maintaining a list of interfaces.
    // ---------------------------------------------------------------------------
    public native func VoiceRefreshDevices() -> Void;

    public native func VoiceInputCount() -> Uint32;
    public native func VoiceOutputCount() -> Uint32;

    public native func VoiceInputName(index: Uint32) -> String;
    public native func VoiceInputId(index: Uint32) -> String;
    public native func VoiceInputIsDefault(index: Uint32) -> Bool;

    // Names that look like Stereo Mix and friends. Windows reports them as ordinary
    // microphones, so this is a warning for the UI to show - never a reason to hide one.
    public native func VoiceInputIsLoopback(index: Uint32) -> Bool;

    public native func VoiceOutputName(index: Uint32) -> String;
    public native func VoiceOutputId(index: Uint32) -> String;

    // An empty id follows the Windows default, which is what somebody means when they
    // have not chosen deliberately.
    public native func VoiceStartCapture(deviceId: String) -> Bool;
    public native func VoiceStopCapture() -> Void;
    public native func VoiceIsCapturing() -> Bool;

    // Loudest sample since the last call, 0..1. READING CLEARS IT - a meter shows the
    // interval it is drawing, not the loudest thing that has ever happened.
    public native func VoiceInputLevel() -> Float;

    public native func VoiceLastError() -> String;

    // ---------------------------------------------------------------------------
    // Voice chat proper - microphone to other people's speakers.
    //
    // Distinct from the capture calls above, which exist for the settings meter and open a
    // microphone WITHOUT transmitting. Starting voice takes the microphone over, so the two
    // never run at once.
    // ---------------------------------------------------------------------------

    // Empty ids follow the Windows defaults. False means voice did not start; the game
    // carries on regardless, because losing a session over a headset is not acceptable.
    public native func VoiceStart(inputDevice: String, outputDevice: String) -> Bool;
    public native func VoiceStop() -> Void;
    public native func VoiceIsRunning() -> Bool;

    // Frames exist only while transmitting - there is no "stopped talking" message, so a
    // lost packet can never leave somebody's microphone open.
    public native func VoiceSetTransmitting(on: Bool) -> Void;
    public native func VoiceIsTransmitting() -> Bool;

    // 0 whisper, 1 local, 2 yell. The SERVER turns these into metres.
    public native func VoiceSetRange(range: Uint32) -> Void;
    public native func VoiceGetRange() -> Uint32;

    public native func VoiceSetMicVolume(percent: Uint32) -> Void;
    public native func VoiceSetPlaybackVolume(percent: Uint32) -> Void;

    // How many people are audible right now, for the speaking indicator.
    public native func VoiceActiveSpeakerCount() -> Uint32;

    // The Nth speaker from the snapshot the last VoiceActiveSpeakerCount() call took -
    // call Count first, every time, or this reads a stale/empty list.
    public native func VoiceActiveSpeakerId(index: Uint32) -> Uint64;

    // Whether --hackable-puppets was passed. See Hackable.reds for what it turns on and
    // why it is off by default.
    public native func HackablePuppetsEnabled() -> Bool;

    // Combat, reported from the game's own damage pipeline. The client detects; the SERVER
    // decides. Neither of these applies damage to anybody - see Combat.reds.
    public native func GetServerIdByEntity(entityId: EntityID) -> Uint64;

    public native func SendCombatEvent(targetId: Uint64, sourceType: Uint32, attackType: Uint32, sourceId: Uint64, damageType: Uint32, hitZone: Uint32, damage: Float, position: Vector4, direction: Vector4, critical: Bool, headshot: Bool) -> Void;

    // Quickhack status effects. Outbound when we cause one on a remote player; inbound
    // when one lands on us - see Combat.reds.
    public native func SendStatusEffect(targetId: Uint64, effectId: Uint64, stacks: Uint32, sourceId: Uint64) -> Void;
    public native func ConsumeIncomingStatusEffect() -> Uint64;

    // The health the server says we should be on, or -1 when nothing new. Absolute, never
    // a delta - see MpApplyServerHealth.
    public native func ConsumeIncomingHealth() -> Float;
    public native func IsDowned() -> Bool;

    // Quickhack upload visibility - see Combat.reds. State is read separately because
    // redscript cannot receive a pair from one native call without a struct.
    public native func SendQuickhackUpload(targetId: Uint64, state: Uint32, quickhackId: Uint64, duration: Float) -> Void;
    public native func SendWeaponEvent(kind: Uint32, weaponId: Uint64, magazine: Uint32, reserve: Uint32) -> Void;
    public native func SendQuickhackRequest(targetId: Uint64, quickhackId: Uint64, ramCost: Float) -> Void;
    public native func ConsumeIncomingUploadTarget() -> Uint64;
    public native func GetIncomingUploadState() -> Uint32;
    public native func GetEntityIdByServerId(serverId: Uint64) -> EntityID;
    public native func GetAppearanceSystem() -> ref<AppearanceSystem>;
    public native func GetChatSystem() -> ref<ChatSystem>;
    public native func GetInterpolationSystem() -> ref<InterpolationSystem>;
    public native func GetVehicleSystem() -> ref<VehicleSystem>;

    // Called from native immediately before a character save is sent.
    //
    // The reading has to happen in script (the item API lives there) and the sending has
    // to happen in native (the socket lives there), so native asks script to fill the
    // buffer and then sends it. Doing it at save time rather than on a timer means what
    // is stored is what the player had at the moment the server was told about them,
    // rather than whatever they had up to a minute ago.
    // Called from native when the server's spawn response carries possessions.
    //
    // Reading them out one at a time rather than receiving an array, for the same reason
    // the capture pushes them one at a time: the boundary carries scalars, which cannot be
    // marshalled wrongly.
    /**
     * Applies a balance the server has decided on - a purchase, a sale, an adjustment.
     *
     * The difference, never the total, for the same reason the restore path does it that
     * way: money is an item the player already holds some of, and giving them the target
     * amount on top of what they have doubles it.
     *
     * Called from native when a NotifyMoney arrives.
     */
    public func ApplyServerMoney() -> Void {
        let player = GetPlayer(GetGameInstance());
        let transaction = GameInstance.GetTransactionSystem(GetGameInstance());

        if !IsDefined(player) || !IsDefined(transaction) {
            this.ScriptLog("money: cannot apply - no player or transaction system");
            return;
        }

        let target = this.GetTargetMoney();
        let held = transaction.GetItemQuantity(player, MarketSystem.Money());
        let owed = target - held;

        if owed > 0 {
            transaction.GiveItem(player, MarketSystem.Money(), owed);
        } else {
            if owed < 0 {
                transaction.RemoveItem(player, MarketSystem.Money(), -owed);
            }
        }

        this.ScriptLog(s"money: \(held) -> \(target) on the server's instruction");
    }

    public func RestorePossessions() -> Void {
        MpInventory.Restore(this);
        this.ApplyWorldFacts();
    }

    /**
     * Sets the quest facts the server sent - which doors are open here.
     *
     * A fact is how Night City decides whether a door opens: the device asks the quest
     * system for one value. Setting them is the server saying "this place is open", and
     * it does not touch quest state, mark anything complete, or pretend a story happened.
     *
     * Applied with the possessions rather than at connect, for the same reason - the world
     * has to exist before anything can be told about it.
     */
    public func ApplyWorldFacts() -> Void {
        let count = this.GetFactCount();

        if count == 0u {
            return;
        }

        let quests = GameInstance.GetQuestsSystem(GetGameInstance());

        if !IsDefined(quests) {
            this.ScriptLog("facts: no quest system - nothing unlocked");
            return;
        }

        let index: Uint32 = 0u;

        while index < count {
            quests.SetFactStr(this.GetFactName(index), this.GetFactValue(index));
            index += 1u;
        }

        this.ScriptLog(s"facts: applied \(count) world fact(s)");
    }

    public func CaptureInventory() -> Void {
        MpInventory.Capture(this);
    }

    public func OnConnected() -> Void {
        // let evt: ref<ConnectedToServer>;
        // evt.m_connected = true;
        // GameInstance.GetUISystem(GetGameInstance()).QueueEvent(evt);
        
        let blackboardSystem: ref<BlackboardSystem> = GameInstance.GetBlackboardSystem(GetGameInstance());
        let blackboard: ref<IBlackboard> = blackboardSystem.Get(GetAllBlackboardDefs().UIGameData);
        blackboard.SetBool(GetAllBlackboardDefs().UIGameData.UIMultiplayerConnectedToServer, true, true);

        // Start draining quickhacks aimed at us. Effects arrive on the network thread into
        // a native queue; this is the script side visiting it. See Combat.reds.
        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(new MpStatusEffectPoll(), 0.25, false);
        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(new MpWeaponPoll(), 0.25, false);

        // The usual order is player-attaches-then-connects, so this is where the
        // no-flatline machinery actually takes effect - see Death.reds. Arming it in
        // PlayerPuppet.OnGameAttached alone only covered loading a save while already
        // connected, which is the rarer half.
        let player = GetPlayer(GetGameInstance()) as PlayerPuppet;
        MpApplyImmortality(player);
        MpArmDeathFloor(player);
    }

    public func OnDisconnected(reason: Uint32) -> Void {
        // let evt: ref<ConnectedToServer>;
        // evt.m_connected = true;
        // GameInstance.GetUISystem(GetGameInstance()).QueueEvent(evt);

        let blackboardSystem: ref<BlackboardSystem> = GameInstance.GetBlackboardSystem(GetGameInstance());
        let blackboard: ref<IBlackboard> = blackboardSystem.Get(GetAllBlackboardDefs().UIGameData);
        blackboard.SetBool(GetAllBlackboardDefs().UIGameData.UIMultiplayerConnectedToServer, false, true);

        // Say WHY, on screen, when the server said why. Every denied join used to be a
        // bare disconnect with the reason sitting in a log file - "the mod loads but does
        // not connect" was a recurring live mystery that this single popup retires. The
        // message is the server's own sentence (or the transport refusal translated), so
        // it already says what to do about it.
        let denial = this.GetDenialMessage();
        if NotEquals(denial, "") {
            let required = this.GetRequiredManifest();
            if NotEquals(required, "") {
                denial = s"\(denial) (server wants \(required))";
            }

            let msg: SimpleScreenMessage;
            msg.isShown = true;
            msg.duration = 10.0;
            msg.message = denial;
            blackboard = blackboardSystem.Get(GetAllBlackboardDefs().UI_Notifications);
            blackboard.SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(msg), true);

            this.ScriptLog(s"connection denied (code \(this.GetDenialCode())): \(denial)");
        }

        // Give death back. Someone who leaves the server and carries on playing their own
        // save should be able to die in it.
        MpClearImmortality(GetPlayer(GetGameInstance()) as PlayerPuppet);
    }

    // record comes from native, which reads it from the launch flags - see Settings.h.
    // The muppet records are mannequins: a body and animations, and none of what the game
    // needs to treat something as a target. Which record gives a puppet that is both
    // stable AND shootable is trial and error, and every attempt costs two people being
    // online, so it is switchable between launches rather than between releases.
    public func CreatePuppet(position: Vector4, rotation: Quaternion, isMale: Bool, record: String) -> EntityID {
        let npcSpec = new DynamicEntitySpec();

        // An empty or misspelt record would produce an invalid id and spawn nothing at
        // all, which looks exactly like the other player never joining. Fall back to the
        // mannequin, which is at least known to work.
        let id = TDBID.Create(record);
        if !TDBID.IsValid(id) {
            FTLogError(s"[NetworkWorldSystem] no such record '\(record)' - falling back to the muppet");
            id = isMale ? t"Character.MaMuppet" : t"Character.WaMuppet";
        }

        npcSpec.recordID = id;
        npcSpec.alwaysSpawned = true;
        npcSpec.position = position;
        npcSpec.orientation = rotation;
        npcSpec.persistState = false;
        npcSpec.persistSpawn = false;
        npcSpec.tags = [n"CyberpunkMP.Puppet"];


        return GameInstance.GetDynamicEntitySystem().CreateEntity(npcSpec);
    }

    /**
     * Slide into the driver's seat, on the server's instruction.
     *
     * The driver getting out of a car with a passenger still in it already hands that
     * passenger the vehicle's SIMULATION - NextOccupant picks them by the server's seat
     * priority and TransferAuthority moves the authority across. What never happened is
     * the part a human can see: they stayed sitting in the passenger seat of a car they
     * were now responsible for, unable to drive it.
     *
     * This is that missing half. The server decides who is promoted - clients must not each
     * pick their own new driver - and sends NotifyVehicleControlAssigned to the one player
     * it chose.
     *
     * Two steps, because the mounting facility negotiates rather than commands and
     * unmounting is not instant: leave the current seat, then take the driver's on a short
     * delay. Mounting in the same frame as the unmount races it, which is the same lesson
     * the teleport path below already learned.
     *
     * The seat name comes from the game rather than a literal: VehicleComponent knows which
     * slot is the driver's, and hard-coding 'seat_front_left' here would be a second place
     * to be wrong if that ever differs by vehicle.
     */
    public func TakeDriverSeat(vehicleId: EntityID) -> Void {
        let player = GetPlayer(GetGameInstance());
        if !IsDefined(player) {
            FTLogError(s"[NetworkWorldSystem] driver seat assigned with no local player");
            return;
        }

        let vehicle = GameInstance.FindEntityByID(GetGameInstance(), vehicleId) as VehicleObject;
        if !IsDefined(vehicle) {
            FTLogError(s"[NetworkWorldSystem] driver seat assigned for a vehicle we do not have");
            return;
        }

        let facility = GameInstance.GetMountingFacility(GetGameInstance());
        let current = facility.GetMountingInfoSingleWithObjects(player);

        // Already driving - nothing to do. Reached when the promotion arrives for somebody
        // who was the only occupant and never left, and re-seating them would be a visible
        // stutter for no reason.
        if Equals(current.slotId.id, VehicleComponent.GetDriverSlotName()) {
            return;
        }

        let leave = new UnmountingRequest();
        leave.lowLevelMountingInfo = current;
        leave.mountData = new MountEventData();

        facility.Unmount(leave);

        let promote = new MpTakeDriverSeatCallback();
        promote.player = player;
        promote.vehicleId = vehicleId;

        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(promote, 0.35, false);
    }

    // Moves the local player, on the server's instruction.
    //
    // Called from native when a NotifyTeleport arrives. It has to happen here rather than
    // server-side because the client owns its own position - the server editing its copy
    // would simply be overwritten by the next position update the client sends.
    //
    // The teleportation facility is the game's own fast-travel machinery, so streaming,
    // collision and the camera are all handled properly. Setting the position directly
    // drops people through the world.
    public func TeleportLocalPlayer(position: Vector4, yaw: Float) -> Void {
        let player = GetPlayer(GetGameInstance());
        if !IsDefined(player) {
            FTLogError(s"[NetworkWorldSystem] teleport arrived with no local player");
            return;
        }

        // Bring the person, not the car.
        //
        // Teleporting someone who is driving used to drag the vehicle with them AND break
        // their position for everyone else. The second half is the part that was not
        // obvious: while the client believes it is driving, UpdatePlayerLocation sends the
        // VEHICLE's transform as the player's position. Move the player out from under
        // that and every other client is told they are still wherever the car is - so the
        // teleported player appears not to have moved, or to be somewhere they are not.
        //
        // So this is three separate things, and the first attempt only did the middle one:
        //
        //   1. tell the network we have left, which stops the car's transform being sent
        //      as ours and tells the server to detach the puppet for everyone else
        //   2. make the game actually take us out of the seat
        //   3. only then move
        if VehicleComponent.IsMountedToVehicle(GetGameInstance(), player) {
            FTLog(s"[NetworkWorldSystem] teleport while mounted - forcing an exit first");

            // 1. The network's idea of where we are, corrected before anything moves.
            let vehicles = this.GetVehicleSystem();
            if IsDefined(vehicles) {
                vehicles.OnVehicleExit();
            }

            // 2. Out of the seat, by both routes.
            //
            // The mounting facility is the clean way and it is not always enough - it
            // negotiates rather than commands, and a vehicle mid-animation can simply not
            // comply. The ExitVehicle AI event is what the mod already uses to get remote
            // puppets out of cars, and it is the blunt one. Doing both means the polite
            // path is tried and the forceful path is there when it is refused.
            let info = GameInstance.GetMountingFacility(GetGameInstance()).GetMountingInfoSingleWithObjects(player);

            let request = new UnmountingRequest();
            request.lowLevelMountingInfo = info;
            request.mountData = new MountEventData();

            GameInstance.GetMountingFacility(GetGameInstance()).Unmount(request);

            let exitEvent = new AIEvent();
            exitEvent.name = n"ExitVehicle";
            player.QueueEvent(exitEvent);

            // 3. Unmounting is not instant. Teleporting in the same frame races the
            // dismount and lands the player back in the seat at the destination, which is
            // the same bug wearing a different hat.
            let delayed = new MpDelayedTeleportCallback();
            delayed.system = this;
            delayed.position = position;
            delayed.yaw = yaw;
            GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(delayed, 0.5, false);
            return;
        }

        this.DoTeleport(position, yaw);
    }

    public func DoTeleport(position: Vector4, yaw: Float) -> Void {
        let player = GetPlayer(GetGameInstance());
        if !IsDefined(player) {
            return;
        }

        // The wire carries radians; EulerAngles is in degrees.
        //
        // Built field by field. redscript warns that passing arguments to a native
        // struct's constructor is undefined behaviour when it has no script definition,
        // and this one positions players - not somewhere to leave a shrug.
        let angles = new EulerAngles();
        angles.Roll = 0.0;
        angles.Pitch = 0.0;
        angles.Yaw = yaw * 57.2957795;

        FTLog(s"[NetworkWorldSystem] teleporting to \(position.X), \(position.Y), \(position.Z)");
        GameInstance.GetTeleportationFacility(GetGameInstance()).Teleport(player, position, angles);

        // Belt and braces. If the forced exit above did not take, the client would keep
        // reporting the car's position as the player's from the other side of the map -
        // and the symptom of that is not "still in a car", it is "this player is
        // desynchronised for everybody", which is far harder to recognise for what it is.
        let vehicles = this.GetVehicleSystem();
        if IsDefined(vehicles) && VehicleComponent.IsMountedToVehicle(GetGameInstance(), player) {
            FTLogWarning(s"[NetworkWorldSystem] still mounted after teleporting - clearing the vehicle link anyway");
            vehicles.OnVehicleExit();
        }
    }

    // Puts a downed player back on their feet at the server's respawn point.
    //
    // Health first, then the move. Teleporting a player the game still considers dead
    // leaves them face-down at the destination, which reads as the mod being broken
    // rather than as respawning.
    public func RevivePlayer() -> Void {
        let player = GetPlayer(GetGameInstance());
        if !IsDefined(player) {
            return;
        }

        let game = player.GetGame();

        // Back to full. RequestSettingStatPoolValue with the last argument true forces
        // the value rather than treating it as a modifier, which is what actually clears
        // the dead state.
        GameInstance.GetStatPoolsSystem(game)
            .RequestSettingStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()),
                                         gamedataStatPoolType.Health, 100.0, player, true);

        // The death flow applies this to stop you opening menus while dead. It is not
        // removed by healing, so anyone revived without this keeps a locked-out pause
        // menu for the rest of the session.
        StatusEffectHelper.RemoveStatusEffect(player, t"GameplayRestriction.BlockAllMenu");

        // Applied by the death flow when the player is downed. Left on, it keeps them
        // limping and unable to fight after they are back on their feet.
        StatusEffectHelper.RemoveStatusEffect(player, t"BaseStatusEffect.Defeated");

        // Re-arm the health floor. A custom stat-pool limit is spent once it is reached,
        // so without this only the FIRST death of a session is caught and every one after
        // it flatlines normally. That asymmetry is what made v0.1.31 look like it worked
        // for one player and not another.
        MpArmDeathFloor(player as PlayerPuppet);

        // A few seconds where nothing can put them straight back down. Full health alone
        // does not stop a revive-at-the-same-hot-spot loop; see Death.reds for why this
        // needs to be actual invulnerability, not a bigger number.
        MpGrantReviveGrace(player as PlayerPuppet);

        // Asking the server where to go rather than deciding here. It owns the respawn
        // point, and it is the same teleport path /tp and /return already use - so this
        // adds no new way for a client to move itself anywhere it likes.
        this.RequestRespawn();
    }

    public native func RequestRespawn() -> Void;
    public native func IsConnected() -> Bool;

    // Sends the appearance the player currently has to the server, which stores it against
    // their Discord id. Driven by /character save - see CharacterCreator.reds for why
    // saving is explicit rather than detected.
    public native func SaveCharacterAppearance() -> Void;

    // Opening the game's creator on demand is NOT possible from here, and that is a
    // settled question rather than an untried idea.
    //
    // gameuiCharacterCustomizationSystem is `importonly` and empty; its interface exposes
    // exactly two methods to scripts - HasCharacterCustomizationComponent and GetState.
    // Initialize, StartCustomization, EndCustomization, SetCustomizationState and IsActive
    // are all native-only. There is no mirror class and no creator game controller in the
    // type dump either. Verified against the 2.31 hierarchy, not guessed.
    //
    // So changing your appearance uses the game's own mirror - the one in V's apartment,
    // which already works in a live world - and /character save then captures the result.
    // Driving the creator directly needs native work on the C++ side, which already holds
    // a real CharacterCustomizationSystem pointer for serialisation.
    public func OpenCharacterCreator() -> Void {
        let game = GetGameInstance();
        let player = GetPlayer(game);

        let system = GameInstance.GetCharacterCustomizationSystem(game);

        if IsDefined(system) && IsDefined(player) && system.HasCharacterCustomizationComponent(player) {
            FTLog(s"[Character] the player can be customised - use a mirror, then /character save");
        } else {
            FTLogWarning(s"[Character] this player has no customization component");
        }
    }

    // Called from C++ when the server wants a name for this character.
    //
    // Raised as a UI event rather than handled here, because the thing that has to happen
    // is a text box appearing and this system owns no widgets. ChatController does, and it
    // already owns a text input that works - so the prompt reuses it instead of building a
    // second one that would need its own focus handling, its own input context and its own
    // way of being wrong.
    public func RequestCharacterName(current: String) -> Void {
        FTLog(s"[Character] the server asked for a character name (currently '\(current)')");

        let evt = new CharacterNameRequest();
        evt.m_current = current;

        GameInstance.GetUISystem(GetGameInstance()).QueueEvent(evt);
    }

    public func DeletePuppet(entityId: EntityID) {
        GameInstance.GetDynamicEntitySystem().DeleteEntity(entityId);
    }
}

public class MpDelayedTeleportCallback extends DelayCallback {
    public let system: wref<NetworkWorldSystem>;
    public let position: Vector4;
    public let yaw: Float;

    public func Call() -> Void {
        if IsDefined(this.system) {
            this.system.DoTeleport(this.position, this.yaw);
        }
    }
}

@addMethod(GameInstance)
public static native func GetNetworkWorldSystem() -> ref<NetworkWorldSystem>