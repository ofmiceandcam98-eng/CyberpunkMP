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

        // The wire carries radians; EulerAngles is in degrees.
        let angles = new EulerAngles(0.0, 0.0, yaw * 57.2957795);

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

@addMethod(GameInstance)
public static native func GetNetworkWorldSystem() -> ref<NetworkWorldSystem>