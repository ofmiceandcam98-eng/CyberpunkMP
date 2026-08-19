module CyberpunkMP.World

import Codeware.*
import CyberpunkMP.*

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

    // TDBID exposes ToNumber and no inverse - the conversion is one-way in script, so the
    // rebuild happens in C++ where a TweakDBID is just its number.
    public native func TdbidFromNumber(value: Uint64) -> TweakDBID;
    public native func ConsumeJoinRequest() -> Bool;
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