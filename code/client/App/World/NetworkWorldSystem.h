#pragma once

#include "Core/Hooking/HookingAgent.hpp"
#include "RED4ext/Scripting/Natives/Generated/Vector4.hpp"
#include "RED4ext/Scripting/Natives/Generated/Quaternion.hpp"
#include "RED4ext/Scripting/Natives/Generated/game/IBlackboard.hpp"
#include "RED4ext/Scripting/Natives/Generated/game/bb/ScriptID_Int32.hpp"
#include "Network/Client.h"
#include "Red/TypeInfo/Macros/Definition.hpp"
#include "AppearanceSystem.h"
#include "ChatSystem.h"
#include "InterpolationSystem.h"
#include "VehicleSystem.h"

struct NetworkWorldSystem : RED4ext::IGameSystem, Core::HookingAgent, flecs::world
{
    RTTI_IMPL_TYPEINFO(NetworkWorldSystem);
    RTTI_IMPL_ALLOCATOR();

    static uint64_t GetTick();

    NetworkWorldSystem();

    bool Spawn(uint64_t aServerId, const Red::Vector4& aPosition, const Red::Quaternion& aRotation, const Red::DynArray<Red::TweakDBID>& aEquipment, const Vector<uint8_t> aCcstate, const std::string& acUsername = {}, const std::string& acRecord = {});
    void DeSpawn(uint64_t aServerId) const;

    Red::Handle<Red::Entity> GetEntity(Red::EntityID aId) const;
    flecs::entity GetEntityByServerId(uint64_t aServerId) const;
    Red::EntityID GetEntityIdByServerId(uint64_t aServerId) const;
    flecs::entity FindEntity(Red::EntityID aId) const;

    void SetRemotePlayerId(uint64_t aId) { m_remotePlayerId = aId; }
    std::optional<uint64_t> GetRemotePlayerId() const { return m_remotePlayerId; }

    Red::Handle<AppearanceSystem> GetAppearanceSystem() const { return m_appearanceSystem; }
    Red::Handle<ChatSystem> GetChatSystem() const { return m_chatSystem; }
    Red::Handle<InterpolationSystem> GetInterpolationSystem() const { return m_interpolationSystem; }
    Red::Handle<VehicleSystem> GetVehicleSystem() const { return m_vehicleSystem; }

    void Update(uint64_t aTick);

    void OnInitialize(const RED4ext::JobHandle& aJob) override;

    void Connect();
    void Disconnect();
    void OnConnected();
    void OnDisconnected(Client::EDisconnectReason);

    // Joining is a decision someone makes in the main menu, but it cannot be acted on
    // there: the menu has no world and no player to put anywhere. The decision has to
    // outlive the save load that follows it.
    //
    // This system is where it lives because it is the one thing in the chain that
    // survives. Game systems are created once per session and stay put while worlds
    // attach and detach around them - which is exactly why OnInitialize runs once and
    // OnWorldAttached runs for every world. Script state does not survive; this does.
    void RequestJoin();

    // Called from the main menu when MULTIPLAYER - NEW CHARACTER is chosen.
    //
    // The server captures an appearance on first spawn only for a player who has NO
    // character, which means NEW CHARACTER could never actually replace one. Somebody with
    // an existing character went through the whole creator, connected, and was spawned as
    // the character they had just replaced - hyliangenesis built a male V and stayed female
    // for a day, and their stored record never changed.
    //
    // The server cannot tell the difference on its own: from its side, a returning player
    // and a player who just rebuilt themselves look identical. Only the client knows which
    // menu entry was pressed, so the client is what says so.
    void MarkNewCharacter();

    // Capturing what the player is carrying, so the SERVER owns it rather than their save.
    //
    // Three calls rather than one, because redscript is what can read an inventory - the
    // item API is script-side and documented there - while only C++ can put it on the
    // wire. Passing an array across that boundary means marshalling a struct array through
    // RTTI; passing two numbers at a time does not. The cost is three calls instead of
    // one, which is nothing next to getting the marshalling subtly wrong.
    //
    // Begin clears whatever a previous capture left. A capture that half-completed and
    // then failed must not merge into the next one and hand somebody an inventory that is
    // partly their own and partly a stale copy.
    void BeginInventoryCapture();
    void AddInventoryItem(uint64_t aId, uint32_t aQuantity);
    void EndInventoryCapture(int64_t aMoney);

    // Number back to TweakDBID, because redscript cannot.
    //
    // TDBID exposes ToNumber and no inverse - the conversion is one-way in script. A
    // TweakDBID IS its number though, so C++ reconstructs it for nothing, and this keeps
    // the item API calls in redscript where the compiler checks them.
    Red::TweakDBID TdbidFromNumber(uint64_t aValue) const;

    // Skills, street cred and level ride the same capture as possessions - they are saved
    // at the same moment and describe the same character, and a second message would be a
    // second thing to keep in step.
    void AddProficiency(uint32_t aType, int32_t aLevel);

    // A way for redscript to say something into a log we can actually read.
    //
    // FTLog goes somewhere none of the mod's logs collect - not CyberpunkMP.log, not the
    // RED4ext log, not redscript's own. Every script-side message written tonight went
    // into the void, so "the restore did not run" and "I cannot see the restore run" were
    // indistinguishable, and an evening went on the difference.
    void ScriptLog(const Red::CString& acText) const;

    // Applying what the server sent back. Mirrors the capture side: native holds the data
    // that came off the wire, script does the work, and only scalars cross between them.
    uint32_t GetRestoreCount() const;
    uint64_t GetRestoreId(uint32_t aIndex) const;
    uint32_t GetRestoreQuantity(uint32_t aIndex) const;
    // Int32 rather than Int64: redscript has no cast between them, and eddies do not
    // come close to two billion. The record and the wire keep the wider type.
    int32_t GetRestoreMoney() const;
    bool ConsumeJoinRequest();

    // Called from redscript when the local player is downed - see Death.reds.
    void RequestRespawn();

    // Stores the player's current appearance as their multiplayer character. A manual
    // override; PollAppearanceChanges is what saves in normal use.
    void SaveCharacterAppearance();

    // Watches for the player finishing a mirror or creator session, and saves it for them.
    void PollAppearanceChanges();

    // Writes every native function on the customization system to the log, once, so the
    // way to open the creator can be found rather than guessed at.
    void DumpCustomizationApi() const;

    // What they had while the customization state was still readable. Captured during the
    // session because once it closes the instance is null and there is nothing to read.
    Vector<uint8_t> m_pendingAppearance;
    bool m_pendingIsMale{true};
    bool m_wasCustomising{false};
    bool IsConnected() const;

protected:
    void OnWorldAttached(RED4ext::world::RuntimeScene* aScene) override;
    void OnAfterWorldDetach() override;
    void OnBeforeWorldDetach(RED4ext::world::RuntimeScene* aScene) override;

    void HandleCharacterLoad(const PacketEvent<server::NotifyCharacterLoad>& aMessage);
    void HandleEntityUnload(const PacketEvent<server::NotifyEntityUnload>& aMessage);
    void HandleSpawnCharacterResponse(const PacketEvent<server::SpawnCharacterResponse>& aMessage);

    // The server asking this client to make a character.
    void HandleOpenCharacterCreator(const PacketEvent<server::OpenCharacterCreator>& aMessage);
    void HandleRequestCharacterName(const PacketEvent<server::RequestCharacterName>& aMessage);

    // Set while a freshly created character is waiting to be sent to the server.
    bool m_newCharacterPending{false};

    // What the last capture read. Held until a character save sends it.
    Vector<client::ItemStack> m_capturedInventory;
    Vector<client::Proficiency> m_capturedProficiencies;

    // What the server sent on spawn, held until script has applied it.
    Vector<server::ItemStack> m_restoreInventory;
    int64_t m_restoreMoney{0};

    // Set when the server has sent possessions and script has not applied them yet.
    //
    // The spawn response arrives before the player's puppet is built, so calling straight
    // into script there finds no player and returns - silently, in the first version.
    // Update retries until there is somebody to give things to.
    bool m_restorePending{false};
    uint32_t m_restoreTicks{0};
    uint32_t m_restoreReadyAt{0};
    int64_t m_capturedMoney{0};
    bool m_hasCapturedPossessions{false};
    void HandleTeleport(const PacketEvent<server::NotifyTeleport>& aMessage);

    // The server's metronome: shared clock and sky. Applied on arrival, then re-asserted
    // on an interval, because the singleplayer brain drifts the clock and rolls its own
    // weather given the chance. See docs/WORLD-STATE.md.
    void HandleWorldState(const PacketEvent<server::NotifyWorldState>& aMessage);
    void ApplyWorldState();

    // An interaction the server decided happened (ask-don't-tell). Only the TARGET's
    // client acts on it - everyone else sees the outcome through ordinary sync.
    void HandleInteraction(const PacketEvent<server::NotifyInteraction>& aMessage);

    void UpdatePlayerLocation() const;

    // Reads the local player's PlayerStateMachine blackboard: what V is DOING
    // (sprinting, crouching, jumping, aiming), straight from the same state machine
    // the game's own animation runs on. Zeros on any failure - the movement stream
    // must never stall because a blackboard was not ready yet.
    void ReadPlayerState(const Red::Handle<Red::GameObject>& acPlayer,
                         uint32_t& aLocomotion, uint32_t& aUpperBody) const;

private:
    // PSM blackboard access, acquired once per player entity and invalidated when the
    // entity changes (new save, respawn). Mutable: UpdatePlayerLocation is const and
    // only reports - this cache is bookkeeping, not state of the world.
    bool AcquirePsmBlackboard(const Red::Handle<Red::GameObject>& acPlayer) const;
    mutable Red::Handle<Red::game::IBlackboard> m_psmBlackboard;
    mutable Red::EntityID m_psmOwner{};
    mutable Red::gamebbScriptID_Int32 m_psmLocomotionId{};
    mutable Red::gamebbScriptID_Int32 m_psmUpperBodyId{};
    mutable Red::CBaseFunction* m_pPsmGetInt{nullptr};
    bool m_ready{false};
    bool m_joinRequested{false};

    // For measuring our own speed from how far we actually moved - see
    // UpdatePlayerLocation. Mutable because that function is const and only reports.
    mutable glm::vec3 m_lastPosition{};
    mutable std::chrono::steady_clock::time_point m_lastPositionAt{};
    // A spawn that arrived before the world was ready. These used to be silently
    // dropped with no retry - whoever was already online when you loaded in simply
    // never existed for you. Queued here and replayed the moment the world attaches.
    struct PendingSpawn
    {
        uint64_t ServerId;
        Red::Vector4 Position;
        Red::Quaternion Rotation;
        Red::DynArray<Red::TweakDBID> Equipment;
        Vector<uint8_t> Ccstate;
        std::string Username;
        std::string Record; // server-declared NPCs name their exact record
    };
    std::vector<PendingSpawn> m_pendingSpawns;

    mutable bool m_hasLastPosition{false};

    // Last world state the server sent, advanced locally between re-broadcasts so a
    // re-assert never rewinds the sky. Empty until the first NotifyWorldState.
    struct WorldState
    {
        double GameTimeSeconds{0.0};
        float TimeScale{0.f};
        uint64_t WeatherId{0};
        float TransitionSeconds{0.f};
        std::chrono::steady_clock::time_point ReceivedAt{};
    };
    std::optional<WorldState> m_worldState;
    std::chrono::steady_clock::time_point m_lastWorldStateApply{};
    // Whether WE forced the sky. Forcing is sticky engine-side, so weather 0 from the
    // server must actively RELEASE it once - not just stop re-asserting.
    bool m_weatherForced{false};

    Red::CBaseFunction* m_pCreatePuppet;
    Red::CBaseFunction* m_pDeletePuppet;
    std::optional<uint64_t> m_remotePlayerId;
    uint64_t m_lastTick;
    flecs::system m_updatePlayerLocation;
    flecs::system m_updateSpawningEntities;
    flecs::system m_updateAppearance;
    Red::Handle<InterpolationSystem> m_interpolationSystem;
    Red::Handle<AppearanceSystem> m_appearanceSystem;
    Red::Handle<ChatSystem> m_chatSystem;
    Red::Handle<VehicleSystem> m_vehicleSystem;
};

RTTI_DEFINE_CLASS(NetworkWorldSystem, { 
    RTTI_ALIAS("CyberpunkMP.World.NetworkWorldSystem");
    RTTI_METHOD(Connect);
    RTTI_METHOD(Disconnect);
    RTTI_METHOD(RequestJoin);
    RTTI_METHOD(MarkNewCharacter);
    RTTI_METHOD(BeginInventoryCapture);
    RTTI_METHOD(AddInventoryItem);
    RTTI_METHOD(EndInventoryCapture);
    RTTI_METHOD(TdbidFromNumber);
    RTTI_METHOD(AddProficiency);
    RTTI_METHOD(ScriptLog);
    RTTI_METHOD(GetRestoreCount);
    RTTI_METHOD(GetRestoreId);
    RTTI_METHOD(GetRestoreQuantity);
    RTTI_METHOD(GetRestoreMoney);
    RTTI_METHOD(ConsumeJoinRequest);
    RTTI_METHOD(RequestRespawn);
    RTTI_METHOD(SaveCharacterAppearance);
    RTTI_METHOD(IsConnected);
    RTTI_METHOD(GetEntityIdByServerId);
    RTTI_METHOD(GetAppearanceSystem);
    RTTI_METHOD(GetInterpolationSystem);
    RTTI_METHOD(GetChatSystem);
    RTTI_METHOD(GetVehicleSystem);
});
