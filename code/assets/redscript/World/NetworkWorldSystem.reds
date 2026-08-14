module CyberpunkMP.World

import Codeware.*
import CyberpunkMP.*

public native class NetworkWorldSystem extends IGameSystem {
    public native func Connect() -> Void;
    public native func Disconnect() -> Void;
    public native func RequestJoin() -> Void;
    public native func ConsumeJoinRequest() -> Bool;
    public native func GetEntityIdByServerId(serverId: Uint64) -> EntityID;
    public native func GetAppearanceSystem() -> ref<AppearanceSystem>;
    public native func GetChatSystem() -> ref<ChatSystem>;
    public native func GetInterpolationSystem() -> ref<InterpolationSystem>;
    public native func GetVehicleSystem() -> ref<VehicleSystem>;

    public func OnConnected() -> Void {
        // let evt: ref<ConnectedToServer>;
        // evt.m_connected = true;
        // GameInstance.GetUISystem(GetGameInstance()).QueueEvent(evt);
        
        let blackboardSystem: ref<BlackboardSystem> = GameInstance.GetBlackboardSystem(GetGameInstance());
        let blackboard: ref<IBlackboard> = blackboardSystem.Get(GetAllBlackboardDefs().UIGameData);
        blackboard.SetBool(GetAllBlackboardDefs().UIGameData.UIMultiplayerConnectedToServer, true, true);

        // The usual order is player-attaches-then-connects, so this is where the health
        // floor that stops anyone flatlining actually gets armed - see Death.reds. Arming
        // it in PlayerPuppet.OnGameAttached alone only covered loading a save while
        // already connected, which is the rarer half.
        MpArmDeathFloor(GetPlayer(GetGameInstance()) as PlayerPuppet);
    }

    public func OnDisconnected(reason: Uint32) -> Void {
        // let evt: ref<ConnectedToServer>;
        // evt.m_connected = true;
        // GameInstance.GetUISystem(GetGameInstance()).QueueEvent(evt);
        
        let blackboardSystem: ref<BlackboardSystem> = GameInstance.GetBlackboardSystem(GetGameInstance());
        let blackboard: ref<IBlackboard> = blackboardSystem.Get(GetAllBlackboardDefs().UIGameData);
        blackboard.SetBool(GetAllBlackboardDefs().UIGameData.UIMultiplayerConnectedToServer, false, true);
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
        // Teleporting someone who is driving drags the vehicle along with them, so
        // summoning a player for a conversation parked their car on top of the admin who
        // called them. Getting out first also leaves the car where its owner left it,
        // which is what everyone else in the world can already see.
        if VehicleComponent.IsMountedToVehicle(GetGameInstance(), player) {
            let info = GameInstance.GetMountingFacility(GetGameInstance()).GetMountingInfoSingleWithObjects(player);

            let request = new UnmountingRequest();
            request.lowLevelMountingInfo = info;
            request.mountData = new MountEventData();

            GameInstance.GetMountingFacility(GetGameInstance()).Unmount(request);

            // Unmounting is not instant. Teleporting in the same frame races the dismount
            // animation and lands the player back in the seat at the destination, which
            // is the bug wearing a different hat.
            let delayed = new MpDelayedTeleportCallback();
            delayed.system = this;
            delayed.position = position;
            delayed.yaw = yaw;
            GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(delayed, 0.35, false);
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