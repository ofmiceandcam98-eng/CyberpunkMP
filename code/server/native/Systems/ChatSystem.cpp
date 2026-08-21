#include "ChatSystem.h"

#include "GameServer.h"
#include "Components/PlayerComponent.h"
#include "Components/MovementComponent.h"
#include "Components/AppearanceComponent.h"
#include "CharacterRecord.h"
#include "Components/CharacterComponent.h"
#include "Game/Level.h"
#include "Game/WorldClock.h"
#include "Systems/NpcSystem.h"

#include "PlayerManager.h"

/**
 * A dummy that walks, so remote movement can be tested by one person.
 *
 * /dummy used to spawn a fake player that never moved. That was right for the bug it was
 * built for - a spawn crash - and useless for the one being chased now, because a puppet
 * standing still is exactly what a broken puppet looks like. Two people had to be online
 * to tell the difference, and on 19 Aug the second person went home mid-session.
 *
 * Owner is kept so the dummy can borrow that player's Tick. The client interpolates
 * against ticks in ITS OWN timebase - it buffers samples and plays them back behind its
 * local clock - so a tick invented by the server would either sit permanently in the
 * future and never play, or permanently in the past and be discarded. Copying the tick
 * from the player who summoned it puts every sample exactly where a real player's would
 * be.
 */
struct DummyWalkComponent
{
    glm::vec3 Origin{};
    flecs::entity_t Owner{0};
    float Angle{0.f};
    float Radius{5.f};

    // True for a dummy that stamps its movement with the SERVER's clock rather than the
    // summoner's - see the tick assignment in the walk system.
    bool ServerTick{false};
};

ChatSystem::ChatSystem(gsl::not_null<World*> apWorld)
    : m_pWorld(apWorld)
{
    GServer->RegisterHandler<&ChatSystem::HandleChatMessageRequest>(this);
    GServer->RegisterHandler<&ChatSystem::HandleRespawnRequest>(this);
    GServer->RegisterHandler<&ChatSystem::HandleSaveCharacterRequest>(this);

    // Walks every dummy in a slow circle. Registered here rather than beside the command
    // so it exists exactly once, whatever anyone types.
    m_pWorld->system<DummyWalkComponent, MovementComponent>("Dummy walk")
        .each(
            [](flecs::entity aEntity, DummyWalkComponent& aWalk, MovementComponent& aMovement)
            {
                const auto owner = aEntity.world().entity(aWalk.Owner);
                const auto* pOwner = owner.is_alive() ? owner.get<MovementComponent>() : nullptr;

                // The summoner has gone. Leaving it walking against a frozen tick would
                // put every sample in the past and look exactly like the freeze this is
                // meant to expose.
                if (!pOwner)
                    return;

                aWalk.Angle += aEntity.world().delta_time() * 0.7f;

                aMovement.Position = aWalk.Origin + glm::vec3{std::cos(aWalk.Angle) * aWalk.Radius,
                                                             std::sin(aWalk.Angle) * aWalk.Radius,
                                                             0.f};

                // Facing along the circle rather than at its centre, so a puppet that is
                // moving but not turning is distinguishable from one doing neither.
                aMovement.Rotation = {0.f, 0.f, aWalk.Angle + 1.5708f};
                aMovement.Velocity = 2.f;
                // Whose clock stamps this dummy's samples.
                //
                // Borrowing the summoner's tick is what makes an ordinary dummy walk: its
                // samples land just ahead of that client's render time, because they came
                // from that client's own clock. A REAL remote player stamps with their own
                // SynchronizedClock instead, and that is the one difference between the
                // two that has never been ruled out as the cause of the freeze.
                //
                // ServerTick reproduces the real case: the server's own clock, which is a
                // different timebase from the viewer's. If a dummy stamped this way
                // freezes while an ordinary one walks, the clock is the freeze - proven by
                // one person, without waiting for a second player to be free.
                aMovement.Tick = aWalk.ServerTick
                                     ? static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch()).count())
                                     : pOwner->Tick;
                ++aMovement.Sequence;

                // set<> would replace the component and lose Sequence's history; modified<>
                // fires the same OnSet observer that replicates a real player's movement,
                // so the dummy takes exactly the path being tested.
                aEntity.modified<MovementComponent>();
            });
}

void ChatSystem::HandleSaveCharacterRequest(const PacketEvent<client::SaveCharacterRequest>& aMessage)
{
    auto* pPlayerManager = m_pWorld->get<PlayerManager>();

    const auto entity = pPlayerManager->GetByConnectionId(aMessage.ConnectionId);
    if (!entity || !entity.has<PlayerComponent>())
        return;

    auto* pPlayer = entity.get_mut<PlayerComponent>();

    // Identity comes from Discord, never from the client. Without a verified account there
    // is nothing durable to key a character on, and storing one against a connection id
    // would lose it the moment they reconnect.
    if (pPlayer->DiscordId.empty())
    {
        Tell(*pPlayer, "Your Discord sign-in could not be verified, so a character cannot be saved.");
        return;
    }

    const auto& blob = aMessage.get_ccstate();

    // Same bound as the spawn path. This is stored and then relayed to every other client,
    // so an unbounded blob is a way to make one save allocate arbitrary memory everywhere.
    constexpr size_t kMaxCcstate = 256 * 1024;

    // Too SMALL is the case that actually happened. A real appearance is 7-9KB; a 23-byte
    // one was saved during testing and then used to spawn that player, because the
    // customization state exists briefly before it is populated. Rejecting only empty
    // blobs let the degenerate case straight through, and the server is the last place to
    // catch it before it becomes somebody's stored character.
    constexpr size_t kMinCcstate = 1024;

    if (blob.size() < kMinCcstate || blob.size() > kMaxCcstate)
    {
        spdlog::warn("Refused a character save from {} - {} bytes of appearance is not plausible",
                     pPlayer->Username, blob.size());
        Tell(*pPlayer, "That character could not be saved - the appearance data was not usable.");
        return;
    }

    auto& store = GServer->GetPlayerStore();
    const auto* pExisting = store.FindCharacter(pPlayer->DiscordId);

    // Start from the record as stored, and overwrite only what an appearance save is
    // allowed to change. SaveCharacter replaces the stored record wholesale with what it
    // is given (CreatedAt and CharacterId excepted), so every field NOT copied here
    // silently resets on every ripperdoc visit. Not hypothetical: building the record
    // from scratch and hand-carrying fields is how SpawnedBefore got wiped by face edits
    // - sending people back to the arrivals point on their next spawn - and it is how
    // the next field added to CharacterRecord would break too. Copying first inverts the
    // default: a new field survives unless a save deliberately changes it.
    CharacterRecord character = pExisting ? *pExisting : CharacterRecord{};
    character.Slot = 0;
    character.IsMale = aMessage.get_is_male();
    character.Appearance = Base64::Encode(std::vector<uint8_t>(blob.begin(), blob.end()));

    // Read BEFORE it is consumed. The old code cleared this field and then tested it, so
    // "did they deliberately choose a name" could never come back true from here.
    std::string chosenName = pPlayer->PendingCharacterName;
    pPlayer->PendingCharacterName.clear();

    // A name is chosen once per character, and only deliberately.
    //
    // Deliberately means /name or /character save <name> - never the message's own name
    // field, which old clients filled with the Discord name on EVERY ripperdoc save. The
    // server took any non-empty name as a rename, so editing your hair as 'Silverhand92'
    // walked you out named after your account, with NameChosen set so the prompt never
    // asked again. The guard meant to prevent that ("an existing character keeps its
    // name") tested name.empty(), which the always-filled field made unreachable.
    //
    // Once means the FIRST deliberate choice sticks. The reset is a new character:
    // /character new retires this one, and the fresh record starts with NameChosen false.
    // Dying is not a reset - FLATLINED revives the same person, so their name survives;
    // if permadeath ever retires the record instead, the unlock comes with it for free.
    const bool alreadyNamed = pExisting && pExisting->NameChosen;

    if (alreadyNamed && !chosenName.empty() && chosenName != pExisting->Name)
    {
        Tell(*pPlayer, fmt::format("This character is already named '{}' - a name is chosen once.",
                                   pExisting->Name));
        Tell(*pPlayer, "Start a new character with /character new to choose a new name.");
        chosenName.clear();
    }

    if (!chosenName.empty())
    {
        if (chosenName.size() > 32)
            chosenName.resize(32);

        character.Name = chosenName;
        character.NameChosen = true;
    }
    else if (!pExisting)
    {
        // First capture. The client's label if it sent one (first-capture clients send
        // none), else the account username - so a character always has something to be
        // called, and NameChosen stays false so the prompt asks properly.
        std::string label = aMessage.get_name().c_str();
        if (label.size() > 32)
            label.resize(32);

        character.Name = label.empty() ? pPlayer->Username : label;
    }
    // else: an existing character's Name and NameChosen came over with the copy,
    // untouched - editing your face is not an identity change.

    // Possessions, taken from the client and kept by the server.
    //
    // Only overwritten when the client actually sent some. An older client, or one saving
    // before it has read the player's inventory, sends none - and treating that as "they
    // own nothing" would wipe a character's belongings on their next ripperdoc visit. Same
    // reasoning as copying the record before editing it: absence is not a value.
    if (!aMessage.get_inventory().empty() || aMessage.get_money() > 0)
    {
        character.Inventory.clear();
        character.Inventory.reserve(aMessage.get_inventory().size());

        for (const auto& stack : aMessage.get_inventory())
        {
            // A zero id is not an item, and a zero quantity is not a holding. Both mean
            // something upstream failed to read properly, and storing them would hand the
            // player back junk on their next spawn.
            if (stack.get_id() == 0 || stack.get_quantity() == 0)
                continue;

            character.Inventory.push_back({stack.get_id(), stack.get_quantity()});
        }

        // The balance the SERVER last had for this character, before the client's claim
        // replaces it. Absent for a first capture, where there is nothing to disagree with.
        const int64_t storedMoney = pExisting ? pExisting->Money : 0;

        character.Money = aMessage.get_money();

        spdlog::info("{} stored {} item stack(s) and {} eddies", pPlayer->Username,
                     character.Inventory.size(), character.Money);

        // A save that changes the balance is recorded, and one that DISAGREES with the
        // stored balance is called out.
        //
        // This is the instrumentation for the 20000 -> 300 -> 20000 thrash. The server
        // performs real transfers against its own record, and then this path overwrites
        // the balance with whatever the client believed - so a capture already in flight
        // when a transfer lands silently undoes it. Nothing recorded that until now; the
        // symptom was only ever visible as a player saying their money was wrong.
        //
        // Recorded, not refused. Refusing the client's figure is the right answer and it
        // is what phase 5 is for, but doing it here - before there is a ledger showing how
        // often it actually happens, or a server-side balance to fall back to - would take
        // money off players whose client is simply ahead of the server. Measure first.
        if (pExisting && storedMoney != character.Money)
        {
            auto& audit = GServer->GetAuditLog();
            audit.RecordMoney(pPlayer->DiscordId, pPlayer->DiscordId,
                              aMessage.get_automatic() ? "save.automatic" : "save.manual",
                              storedMoney, character.Money);

            // Money appearing from nowhere is worth a human-readable warning as well. A
            // legitimate save reports a balance the player earned or spent since the last
            // one; a large unexplained jump is the shape cheating takes here.
            if (character.Money > storedMoney)
            {
                spdlog::info("{} reported {} eddies, {} more than the server had",
                             pPlayer->Username, character.Money, character.Money - storedMoney);
            }
        }
    }

    // Skills, street cred and level. Same rule as possessions: only replaced when the
    // client actually sent some, because an empty list means both "no progression" and
    // "nobody looked", and resetting somebody's skills to zero because a read failed is
    // not a recoverable mistake.
    if (!aMessage.get_proficiencies().empty())
    {
        /**
         * A new character starts at 15, whatever the template happens to be.
         *
         * The world template is a real save, and its level is an accident of which save was
         * usable - the current one is level 34 because that is where the unmodded,
         * post-Dogtown save happened to be. Letting that decide everyone's starting power
         * means the day we swap the template for world-state reasons, every new player
         * silently starts somewhere else.
         *
         * Level and street cred are both proficiencies, so one clamp covers both. Skills
         * come along for the ride, which is right: a level 15 character with level 20 skills
         * is not a level 15 character.
         *
         * FIRST CAPTURE ONLY. After that the player owns their progression and nothing here
         * touches it - clamping every capture would delete somebody's levelling the moment
         * they passed 15, which is the kind of silent theft that ends a server.
         */
        const bool firstCapture = !character.Initialised && !character.SpawnedBefore;
        constexpr int32_t kStartingLevel = 15;

        character.Proficiencies.clear();
        character.Proficiencies.reserve(aMessage.get_proficiencies().size());

        int clamped = 0;

        for (const auto& prof : aMessage.get_proficiencies())
        {
            auto level = prof.get_level();

            if (firstCapture && level > kStartingLevel)
            {
                level = kStartingLevel;
                ++clamped;
            }

            character.Proficiencies.push_back({prof.get_type(), level});
        }

        spdlog::info("{} stored {} proficiency level(s){}", pPlayer->Username,
                     character.Proficiencies.size(),
                     clamped ? fmt::format(" - new character, {} clamped to {}", clamped,
                                           kStartingLevel)
                             : "");
    }

    // Attributes and perks, same absence rule as everything else: only replaced when the
    // client actually sent some. Resetting somebody's build to zero because a read failed
    // is not a recoverable mistake.
    if (!aMessage.get_attributes().empty())
    {
        character.Attributes.clear();
        for (const auto& a : aMessage.get_attributes())
            character.Attributes.push_back({a.get_type(), a.get_value()});
    }

    if (!aMessage.get_perks().empty())
    {
        character.Perks.clear();
        for (const auto& k : aMessage.get_perks())
            character.Perks.push_back({k.get_type(), k.get_level()});
    }

    // The client's vehicle list is deliberately IGNORED.
    //
    // It was stored here until ownership became a server record. Now the phone's contents
    // are derived from what the player owns, so accepting their report would let a client
    // grant itself cars by claiming to have them unlocked - which is exactly the thing the
    // ownership system exists to prevent. The field stays on the wire for older clients
    // and is read by nothing.

    if (!character.Attributes.empty() || !character.Perks.empty())
    {
        spdlog::info("{} stored {} attribute(s) and {} perk(s)", pPlayer->Username,
                     character.Attributes.size(), character.Perks.size());
    }

    store.SaveCharacter(pPlayer->DiscordId, pPlayer->Username, character);

    spdlog::info("{} saved character '{}' from the creator ({} bytes)", pPlayer->Username,
                 character.Name, blob.size());

    // Confirmed only when they asked. An automatic save is not news.
    if (!aMessage.get_automatic())
    {
        Tell(*pPlayer, fmt::format("Character saved as '{}'. You will look like this every time you join.",
                                   character.Name));
    }

    // Nobody should have to know a command exists to be called something.
    //
    // The creator has no name field, so a character arrives with the account name on it -
    // which is the one thing roleplay cannot have, and it is now on the nameplate, in the
    // scanner and in front of every line they type. Asking here means the box appears the
    // moment they finish creating, which is exactly when they are thinking about who this
    // person is.
    //
    // Only when they have not chosen one. Somebody editing their face at a ripperdoc has
    // already answered this and being asked again every visit would be its own kind of
    // broken.
    //
    // And only on a DELIBERATE save. Saving became automatic (every ~90 seconds), and
    // this ask rode along - so an unnamed player had the name box re-open mid-typing,
    // over and over, for as long as they stayed unnamed: "it keeps refreshing the
    // /name" (live, 2026-08-21). The box appears when they finish the creator and on
    // spawn; the autosave loop stays silent.
    if (!character.NameChosen && !aMessage.get_automatic())
        AskForCharacterName(*pPlayer, character);
}

/**
 * Opens the name box on a player's client.
 *
 * Split out because there are two moments worth asking: straight after the creator, and
 * on spawn for anybody whose character predates the prompt existing. Both want identical
 * behaviour, and the second one is the only route that ever reaches an existing player.
 */
/**
 * Tells one player's game to mark a quest done.
 *
 * Takes the entity rather than the component because the caller has already resolved the
 * player and the connection is the only thing needed - and looking the player up twice is
 * how the two lookups end up disagreeing about who the target was.
 */
/**
 * What to call whoever holds a number.
 *
 * The name a player chose at creation, always, when they have one - that is who they are on
 * this server, and a phone showing somebody's Discord handle to their contacts leaks an
 * account name they never offered.
 *
 * The Discord name is the BACKUP and nothing else: only reached by a character with no
 * chosen name, which is a character mid-creation. The number itself is the last resort, so
 * a row never renders as a blank or as "Unknown" - a contact with no name at all reads like
 * a bug in the phone rather than a person who has not named themselves yet.
 */
static std::string DisplayNameFor(const std::string& acDiscordId, const CharacterRecord* apCharacter,
                                  const std::string& acFallback)
{
    if (apCharacter && !apCharacter->Name.empty())
        return apCharacter->Name;

    if (const auto* pRecord = GServer->GetPlayerStore().Find(acDiscordId))
    {
        if (!pRecord->Username.empty())
            return pRecord->Username;
    }

    return acFallback;
}

void ChatSystem::SendQuestSkip(flecs::entity aSubject, const std::string& acQuest)
{
    const auto* pPlayer = aSubject.get<PlayerComponent>();
    if (!pPlayer)
        return;

    server::NotifyQuestSkip notify;
    notify.set_quest(acQuest.c_str());

    GServer->Send(pPlayer->Connection, notify);
}

void ChatSystem::PushMoney(const PlayerComponent& acPlayer, int32_t aBalance, const std::string& acReason)
{
    server::NotifyMoney notify;
    notify.set_balance(aBalance);
    notify.set_reason(acReason.c_str());

    GServer->Send(acPlayer.Connection, notify);
}

/**
 * Executes a vehicle sale.
 *
 * Ordered so that nothing is half-done. Funds are checked before anything moves, money
 * moves before ownership, and ownership moves last - because a failed transfer after a
 * successful debit leaves somebody poorer with nothing to show, which is the one outcome
 * players will never accept.
 *
 * Balances are read from the SERVER's records, never from what either client claims. The
 * whole reason money became server-owned tonight is so that a purchase cannot be talked
 * into existence by the machine that benefits from it.
 */
void ChatSystem::CompleteSale(const PendingSale& acSale, const PlayerComponent& acBuyer)
{
    auto& store = GServer->GetPlayerStore();
    auto& vehicles = GServer->GetVehicles();

    const auto* pVehicle = vehicles.Find(acSale.VehicleId);

    if (!pVehicle)
    {
        Tell(acBuyer, "That vehicle no longer exists.");
        return;
    }

    // Re-checked at execution rather than trusted from when the offer was made. Between
    // offering and accepting the car may have changed hands by some other route, and the
    // seller must still own the thing they are selling at the moment it is sold.
    if (pVehicle->OwnerId != acSale.SellerId)
    {
        vehicles.Unlock(acSale.VehicleId, acSale.Token);
        Tell(acBuyer, "The seller no longer owns that vehicle.");
        return;
    }

    const auto* pBuyerCharacter = store.FindCharacter(acSale.BuyerId);
    const auto* pSellerCharacter = store.FindCharacter(acSale.SellerId);

    if (!pBuyerCharacter || !pSellerCharacter)
    {
        vehicles.Unlock(acSale.VehicleId, acSale.Token);
        Tell(acBuyer, "That sale cannot complete - one of you has no character record.");
        return;
    }

    if (pBuyerCharacter->Money < acSale.Price)
    {
        vehicles.Unlock(acSale.VehicleId, acSale.Token);
        Tell(acBuyer, fmt::format("You need {} eddies and have {}.", acSale.Price,
                                  pBuyerCharacter->Money));
        return;
    }

    // Money first, both sides, then ownership. Each store write is its own save, so a
    // crash between them leaves the buyer paid-and-carless rather than the reverse - the
    // recoverable direction, and the one an admin can fix with /givecar.
    CharacterRecord buyer = *pBuyerCharacter;
    CharacterRecord seller = *pSellerCharacter;

    buyer.Money -= acSale.Price;
    seller.Money += acSale.Price;

    store.SaveCharacter(acSale.BuyerId, acBuyer.Username, buyer);
    store.SaveCharacter(acSale.SellerId, seller.Name, seller);

    if (!vehicles.Transfer(acSale.VehicleId, acSale.BuyerId, acSale.Token))
    {
        // Put the money back. A transfer that fails here is a bug rather than a refusal -
        // the lock is ours and ownership was checked - but leaving somebody charged for a
        // car they did not receive is not something to risk on that reasoning.
        buyer.Money += acSale.Price;
        seller.Money -= acSale.Price;
        store.SaveCharacter(acSale.BuyerId, acBuyer.Username, buyer);
        store.SaveCharacter(acSale.SellerId, seller.Name, seller);

        Tell(acBuyer, "That sale could not complete. You have not been charged.");
        spdlog::error("Vehicle {} transfer failed after debit - refunded", acSale.VehicleId);
        return;
    }

    // Both games corrected, so neither client's next autosave reports the old figure and
    // undoes the transaction.
    PushMoney(acBuyer, static_cast<int32_t>(buyer.Money), "vehicle purchase");
    Tell(acBuyer, fmt::format("Bought {} (plate {}) for {} eddies. It is in your phone next time you connect.",
                              pVehicle->ModelName, pVehicle->Plate, acSale.Price));

    // The seller may not be online - a sale can complete while they are away, and their
    // record has already been updated either way. Only the live correction is skipped;
    // they will read the new balance from their own record when they next connect.
    //
    // PlayerManager has no lookup by Discord id, so the world is walked. Two players is a
    // walk of two, and inventing an index for a lookup that happens once per sale would be
    // one more thing to keep in step.
    m_pWorld->each(
        [this, &acSale, &seller, &acBuyer, pVehicle](flecs::entity, const PlayerComponent& acOther)
        {
            if (acOther.DiscordId != acSale.SellerId)
                return;

            PushMoney(acOther, static_cast<int32_t>(seller.Money), "vehicle sale");
            Tell(acOther, fmt::format("{} bought your {} for {} eddies.", acBuyer.Username,
                                      pVehicle->ModelName, acSale.Price));
        });

    spdlog::info("Vehicle {} sold: {} -> {} for {}", acSale.VehicleId, acSale.SellerId,
                 acSale.BuyerId, acSale.Price);
}

void ChatSystem::AskForCharacterName(const PlayerComponent& acPlayer, const CharacterRecord& acCharacter)
{
    server::RequestCharacterName ask;

    // Their account name is NOT offered as a starting value. It is what we are trying to
    // get away from, and pre-filling it makes pressing Enter - the path of least
    // resistance - reproduce exactly the problem the prompt exists to solve.
    ask.set_current("");

    GServer->Send(acPlayer.Connection, ask);

    spdlog::info("Asked {} to name their character", acPlayer.Username);
}

void ChatSystem::HandleRespawnRequest(const PacketEvent<client::RespawnRequest>& aMessage)
{
    auto* pPlayerManager = m_pWorld->get<PlayerManager>();

    const auto entity = pPlayerManager->GetByConnectionId(aMessage.ConnectionId);
    if (!entity || !entity.has<PlayerComponent>())
        return;

    auto* pPlayer = entity.get_mut<PlayerComponent>();

    glm::vec3 position;
    float yaw = 0.f;

    if (!GServer->GetRespawnPoint(position, yaw))
    {
        // No respawn point set yet. Reviving where they fell is a worse outcome than the
        // Afterlife but a far better one than lying dead waiting for a load screen, so
        // the client has already healed them and this simply leaves them put.
        Tell(*pPlayer, "You are back up. (No respawn point set - an admin can fix that with /setspawn.)");
        spdlog::warn("{} respawned in place - no respawn point configured", pPlayer->Username);
        return;
    }

    // A jailed player respawns in the cell, not at the Afterlife. Dying is not a way out
    // of a sentence, and EnforceJail would drag them back within the second regardless -
    // this just avoids the visible teleport across the map and straight back.
    if (const auto* pRecord = GServer->GetPlayerStore().Find(pPlayer->DiscordId))
    {
        if (pRecord->JailedUntil > 0)
        {
            position = {pRecord->JailX, pRecord->JailY, pRecord->JailZ};
            yaw = 0.f;
        }
    }

    server::NotifyTeleport teleport;

    common::Vector3 destination;
    destination.set_x(position.x);
    destination.set_y(position.y);
    destination.set_z(position.z);
    teleport.set_position(destination);
    teleport.set_rotation(yaw);

    GServer->Send(pPlayer->Connection, teleport);

    spdlog::info("Respawned {} at ({:.1f}, {:.1f}, {:.1f})", pPlayer->Username, position.x, position.y, position.z);
}

void ChatSystem::Broadcast(String acUsername, String acMessage, uint32_t aChannel)
{
    server::ChatMessage message;
    message.set_username(std::move(acUsername));
    message.set_message(std::move(acMessage));
    message.set_channel(aChannel);

    m_pWorld->each([&message](flecs::entity, const PlayerComponent& aPlayer)
    {
        GServer->Send(aPlayer.Connection, message);
    });
}

void ChatSystem::Tell(const PlayerComponent& acPlayer, const std::string& acMessage)
{
    server::ChatMessage message;
    message.set_username("SERVER");
    message.set_message(acMessage.c_str());
    message.set_channel(ChatChannel::kServer);

    GServer->Send(acPlayer.Connection, message);
}

void ChatSystem::BroadcastInRange(const std::string& acUsername, const std::string& acMessage,
                                  const glm::vec3& acOrigin, float aRange, flecs::entity aSender,
                                  uint32_t aChannel)
{
    server::ChatMessage message;
    message.set_username(acUsername.c_str());
    message.set_message(acMessage.c_str());
    message.set_channel(aChannel);

    m_pWorld->each(
        [&](flecs::entity aEntity, const PlayerComponent& aPlayer)
        {
            if (aEntity == aSender)
            {
                GServer->Send(aPlayer.Connection, message);
                return;
            }

            // No puppet means they are connected but not yet standing anywhere, so there
            // is no distance to measure. They hear nothing local until they spawn, which
            // is the same as not being in the room.
            const auto* pMovement = aPlayer.Puppet ? aPlayer.Puppet.get<MovementComponent>() : nullptr;
            if (!pMovement)
                return;

            if (glm::distance(pMovement->Position, acOrigin) <= aRange)
                GServer->Send(aPlayer.Connection, message);
        });
}

bool ChatSystem::ResolveChannel(const PlayerComponent& acSender, const std::string& acLine,
                                std::string& aText, float& aRange, bool& aEveryone, uint32_t& aChannel)
{
    // Default: ordinary talking, heard by anyone nearby. Typing nothing special is the
    // common case and must stay the shortest path.
    aText = acLine;
    aRange = ChatRange::kLocal;
    aEveryone = false;
    aChannel = ChatChannel::kLocal;

    const auto prefix = [&](const char* acWord) { return acLine.rfind(acWord, 0) == 0; };

    // Each of these is checked WITH a trailing space, so "/yellow hair" is not read as a
    // yell. The bare form is handled separately so it can explain itself.
    struct Channel { const char* Word; float Range; bool Everyone; EPermissionLevel Needs; const char* Marker; uint32_t Id; };

    static constexpr Channel kChannels[] = {
        {"/whisper", ChatRange::kWhisper, false, EPermissionLevel::kPlayer, "[whispers] ", ChatChannel::kWhisper},
        {"/yell",    ChatRange::kYell,    false, EPermissionLevel::kPlayer, "[yells] ",    ChatChannel::kYell},
        {"/advert",  0.f,                 true,  EPermissionLevel::kAdmin,  "[ADVERT] ",   ChatChannel::kAdvert},
    };

    for (const auto& channel : kChannels)
    {
        const std::string withSpace = std::string(channel.Word) + " ";

        if (!prefix(channel.Word))
            continue;

        if (!acSender.HasAtLeast(channel.Needs))
        {
            Tell(acSender, fmt::format("You need to be {} to use {}.",
                                       ToString(channel.Needs), channel.Word));
            return false;
        }

        if (!prefix(withSpace.c_str()) || acLine.size() <= withSpace.size())
        {
            Tell(acSender, fmt::format("Usage: {} <message>", channel.Word));
            return false;
        }

        // The marker travels inside the message rather than the username, so the chat UI
        // still groups consecutive lines by author instead of treating every yell as a
        // new speaker.
        aText = std::string(channel.Marker) + acLine.substr(withSpace.size());
        aRange = channel.Range;
        aEveryone = channel.Everyone;
        aChannel = channel.Id;
        return true;
    }

    return true;
}

namespace
{
// Splits "/ban SomeName being a nuisance" into { "/ban", "SomeName", "being a nuisance" }.
// The reason keeps its spaces - only the command and target are tokenised.
void SplitCommand(const std::string& acLine, std::string& aCommand, std::string& aTarget, std::string& aRest)
{
    const auto first = acLine.find(' ');
    if (first == std::string::npos)
    {
        aCommand = acLine;
        return;
    }

    aCommand = acLine.substr(0, first);

    const auto second = acLine.find(' ', first + 1);
    if (second == std::string::npos)
    {
        aTarget = acLine.substr(first + 1);
        return;
    }

    aTarget = acLine.substr(first + 1, second - first - 1);
    aRest = acLine.substr(second + 1);
}
}

bool ChatSystem::HandleModerationCommand(flecs::entity aSender, const PlayerComponent& acSender,
                                         const std::string& acLine)
{
    if (acLine.empty() || acLine[0] != '/')
        return false;

    std::string command, target, rest;
    SplitCommand(acLine, command, target, rest);

    // Refusals say what rank is needed. "You can't do that" invites people to keep
    // trying; "requires moderator" tells them to go ask for the role.
    const auto deny = [&](EPermissionLevel aRequired)
    {
        Tell(acSender, fmt::format("You need to be {} to use {}.", ToString(aRequired), command));
        spdlog::info("Denied {} ({}) command {}", acSender.Username, ToString(acSender.Level), command);
        return true;
    };

    // Find a connected player by name. Case-insensitive, because nobody types display
    // names exactly, and a moderator fumbling capitalisation during an incident is bad.
    // Four ways to name the same person, because staff know them by different things.
    //
    // Matching only the Discord username meant an admin watching somebody roleplay as
    // "Silas Voss" had to go and look up that this was noremacxxi before they could do
    // anything about them - and a character id, the one identifier that is stable and
    // safe to paste anywhere, could not be used at all.
    //
    // Order matters. Character id first because it is exact and unambiguous by
    // construction; then Discord id, equally exact; then the two human names. A character
    // name is checked before an account name so that in-world identity wins - if somebody
    // names their character after another player's account, the character they are
    // standing in front of is the one an admin means.
    const auto equalsInsensitive = [](const std::string& acLeft, const std::string& acRight)
    {
        return acLeft.size() == acRight.size() &&
               std::equal(acLeft.begin(), acLeft.end(), acRight.begin(),
                          [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    };

    const auto findPlayer = [&](const std::string& acQuery) -> flecs::entity
    {
        flecs::entity byCharacterId{};
        flecs::entity byDiscordId{};
        flecs::entity byCharacterName{};
        flecs::entity byUsername{};

        m_pWorld->each(
            [&](flecs::entity aEntity, const PlayerComponent& aOther)
            {
                if (equalsInsensitive(aOther.Username, acQuery))
                {
                    if (!byUsername) byUsername = aEntity;
                }

                if (aOther.DiscordId == acQuery)
                {
                    if (!byDiscordId) byDiscordId = aEntity;
                }

                const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(aOther.DiscordId);
                if (!pCharacter)
                    return;

                if (!pCharacter->CharacterId.empty() &&
                    equalsInsensitive(pCharacter->CharacterId, acQuery))
                {
                    if (!byCharacterId) byCharacterId = aEntity;
                }

                if (!pCharacter->Name.empty() && equalsInsensitive(pCharacter->Name, acQuery))
                {
                    if (!byCharacterName) byCharacterName = aEntity;
                }
            });

        if (byCharacterId)   return byCharacterId;
        if (byDiscordId)     return byDiscordId;
        if (byCharacterName) return byCharacterName;

        return byUsername;
    };

    // ---------------------------------------------------------------- /kick ---
    if (command == "/kick")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
            return deny(EPermissionLevel::kModerator);

        if (target.empty())
        {
            Tell(acSender, "Usage: /kick <player> [reason]");
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        const auto* pVictim = victim.get<PlayerComponent>();

        // Rank protects rank. Without this, one moderator can kick another in a loop,
        // and anyone who talks a mod into a bad decision can decapitate the staff.
        if (pVictim->Level >= acSender.Level)
        {
            Tell(acSender, "You cannot kick someone at or above your own rank.");
            return true;
        }

        spdlog::info("{} kicked {} ({})", acSender.Username, pVictim->Username, rest);
        Broadcast("SERVER", fmt::format("{} was kicked by {}{}", pVictim->Username, acSender.Username,
                                        rest.empty() ? "" : (" - " + rest)).c_str());
        GServer->Kick(pVictim->Connection);
        return true;
    }

    // ----------------------------------------------------------------- /ban ---
    if (command == "/ban")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        if (target.empty())
        {
            Tell(acSender, "Usage: /ban <player> [reason]");
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        const auto* pVictim = victim.get<PlayerComponent>();

        if (pVictim->Level >= acSender.Level)
        {
            Tell(acSender, "You cannot ban someone at or above your own rank.");
            return true;
        }

        if (pVictim->DiscordId.empty())
        {
            // Nothing durable to ban. Kicking is the honest outcome rather than
            // pretending a ban was recorded.
            Tell(acSender, "That player has no verified Discord account - kicking instead.");
            GServer->Kick(pVictim->Connection);
            return true;
        }

        GServer->GetBanList().Add(pVictim->DiscordId, pVictim->Username, rest, acSender.Username);

        spdlog::info("{} BANNED {} ({}): {}", acSender.Username, pVictim->Username, pVictim->DiscordId, rest);
        Broadcast("SERVER", fmt::format("{} was banned by {}{}", pVictim->Username, acSender.Username,
                                        rest.empty() ? "" : (" - " + rest)).c_str());
        GServer->Kick(pVictim->Connection);
        return true;
    }

    // --------------------------------------------------------------- /unban ---
    if (command == "/unban")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        if (target.empty())
        {
            Tell(acSender, "Usage: /unban <discord id>  (see /bans)");
            return true;
        }

        const bool removed = GServer->GetBanList().Remove(target);
        Tell(acSender, removed ? fmt::format("Unbanned {}.", target)
                               : fmt::format("{} is not banned.", target));

        if (removed)
            spdlog::info("{} unbanned {}", acSender.Username, target);

        return true;
    }

    // ---------------------------------------------------------------- /bans ---
    if (command == "/bans")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
            return deny(EPermissionLevel::kModerator);

        const auto& entries = GServer->GetBanList().Entries();

        if (entries.empty())
        {
            Tell(acSender, "Nobody is banned.");
            return true;
        }

        for (const auto& entry : entries)
        {
            Tell(acSender, fmt::format("{} ({}) - banned by {}{}", entry.Username, entry.DiscordId,
                                       entry.BannedBy,
                                       entry.Reason.empty() ? "" : (" - " + entry.Reason)));
        }

        return true;
    }

    // ----------------------------------------------------------------- /tp ----
    //
    // Two directions, because both are wanted for different reasons.
    //
    //   /tp <player>     brings them to you - you chose where to stand
    //   /tp to <player>  sends you to them - you go and see what they are looking at
    //
    // Written as a word rather than a separate command so there is one thing to remember
    // and one place to look in /help.
    if (command == "/tp")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        const bool goToThem = (target == "to");

        // Re-split when "to" ate the first word: the player's name is what follows it.
        if (goToThem)
        {
            const auto nameStart = rest.find_first_not_of(' ');
            target = (nameStart == std::string::npos) ? std::string{} : rest.substr(nameStart);

            // A trailing word after the name would be a typo rather than a reason.
            if (const auto space = target.find(' '); space != std::string::npos)
                target = target.substr(0, space);
        }

        if (target.empty())
        {
            Tell(acSender, "Usage: /tp <player>   (brings them to you)");
            Tell(acSender, "       /tp to <player>   (sends you to them)");
            Tell(acSender, "  <player> can be a character name, a Discord name, a Discord id,");
            Tell(acSender, "  or a character id from /whois.");
            return true;
        }

        if (goToThem)
        {
            const auto them = findPlayer(target);
            if (!them)
            {
                Tell(acSender, fmt::format("No player called '{}' is online.", target));
                return true;
            }

            const auto* pThem = them.get<PlayerComponent>();
            const auto* pTheirs = pThem->Puppet ? pThem->Puppet.get<MovementComponent>() : nullptr;

            if (!pTheirs)
            {
                Tell(acSender, fmt::format("{} has not spawned in yet.", pThem->Username));
                return true;
            }

            // Remember where WE were, so /return brings us back. Same mechanism, applied
            // to the person running the command instead of the person being moved.
            if (const auto* pMine = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr)
            {
                auto* pSelf = aSender.get_mut<PlayerComponent>();
                pSelf->ReturnPosition = pMine->Position;
                pSelf->ReturnRotation = pMine->Rotation;
                pSelf->HasReturnPoint = true;
            }

            // Beside them rather than on top of them. Two puppets sharing a spot look
            // broken and shove each other apart.
            const float theirYaw = pTheirs->Rotation.z;
            const glm::vec3 theirForward{-std::sin(theirYaw), std::cos(theirYaw), 0.f};
            const glm::vec3 destination = pTheirs->Position + theirForward * kTeleportDistance;

            server::NotifyTeleport teleport;
            common::Vector3 position;
            position.set_x(destination.x);
            position.set_y(destination.y);
            position.set_z(destination.z);
            teleport.set_position(position);

            // Facing them, since you came to see what they are doing.
            teleport.set_rotation(theirYaw + 3.14159265f);

            GServer->Send(acSender.Connection, teleport);

            spdlog::info("{} teleported to {}", acSender.Username, pThem->Username);

            Tell(acSender, fmt::format("Went to {}. /return brings you back.", pThem->Username));
            Tell(*pThem, fmt::format("{} teleported to you.", acSender.Username));
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        const auto* pVictim = victim.get<PlayerComponent>();

        const auto* pMine = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr;
        if (!pMine)
        {
            Tell(acSender, "You need to be in the world to teleport anyone to you.");
            return true;
        }

        if (!pVictim->Puppet)
        {
            Tell(acSender, fmt::format("{} has not spawned in yet.", pVictim->Username));
            return true;
        }

        // Remember where they were BEFORE moving them, so /return can undo this.
        //
        // Recorded from the server's own copy of their position rather than asking the
        // client, and only overwritten when there is a real position to record - so
        // teleporting someone twice in a row still returns them to where they actually
        // started, not to the last place staff dragged them.
        if (const auto* pTheirs = pVictim->Puppet.get<MovementComponent>())
        {
            auto* pVictimMutable = victim.get_mut<PlayerComponent>();
            pVictimMutable->ReturnPosition = pTheirs->Position;
            pVictimMutable->ReturnRotation = pTheirs->Rotation;
            pVictimMutable->HasReturnPoint = true;
        }

        // Yaw is rotation about Z, and Cyberpunk's world is Y-forward at yaw 0, so the
        // facing vector is (-sin, cos). Getting this backwards would still place them the
        // right distance away, just behind - worth an eyeball on the first use.
        const float yaw = pMine->Rotation.z;
        const glm::vec3 forward{-std::sin(yaw), std::cos(yaw), 0.f};
        const glm::vec3 destination = pMine->Position + forward * kTeleportDistance;

        server::NotifyTeleport teleport;

        common::Vector3 position;
        position.set_x(destination.x);
        position.set_y(destination.y);
        position.set_z(destination.z);
        teleport.set_position(position);

        // Turned to face the person who summoned them, which is the whole point of
        // being brought somewhere.
        teleport.set_rotation(yaw + 3.14159265f);

        GServer->Send(pVictim->Connection, teleport);

        spdlog::info("{} teleported {} to ({:.1f}, {:.1f}, {:.1f})", acSender.Username, pVictim->Username,
                     destination.x, destination.y, destination.z);

        Tell(acSender, fmt::format("Brought {} to you.", pVictim->Username));
        Tell(*pVictim, fmt::format("You were teleported to {}.", acSender.Username));
        return true;
    }

    // --------------------------------------------------------------- /kill ----
    //
    // Sends someone to the respawn point immediately. Named /kill because that is what it
    // is for in roleplay terms, but nothing actually dies - players cannot, by design, and
    // a command that produced a FLATLINED screen would hand back the exact session-killer
    // the rest of this work exists to remove.
    if (command == "/kill")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
            return deny(EPermissionLevel::kModerator);

        if (target.empty())
        {
            Tell(acSender, "Usage: /kill <player> [reason]");
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        auto* pVictim = victim.get_mut<PlayerComponent>();

        if (victim != aSender && pVictim->Level >= acSender.Level)
        {
            Tell(acSender, "You cannot do that to someone at or above your own rank.");
            return true;
        }

        glm::vec3 position;
        float yaw = 0.f;

        if (!GServer->GetRespawnPoint(position, yaw))
        {
            Tell(acSender, "No respawn point set - stand where you want it and run /setspawn first.");
            return true;
        }

        // Jail wins, same as an ordinary death. Being killed should not be a way out.
        if (const auto* pRecord = GServer->GetPlayerStore().Find(pVictim->DiscordId))
        {
            if (pRecord->JailedUntil > 0)
                position = {pRecord->JailX, pRecord->JailY, pRecord->JailZ};
        }

        server::NotifyTeleport teleport;
        common::Vector3 destination;
        destination.set_x(position.x);
        destination.set_y(position.y);
        destination.set_z(position.z);
        teleport.set_position(destination);
        teleport.set_rotation(yaw);

        GServer->Send(pVictim->Connection, teleport);

        spdlog::info("{} killed {} ({})", acSender.Username, pVictim->Username, rest);

        Broadcast("SERVER", fmt::format("{} was killed by {}{}", pVictim->Username, acSender.Username,
                                        rest.empty() ? "" : (" - " + rest)).c_str());
        return true;
    }

    // ----------------------------------------------------------- /setspawn ----
    //
    // Where players reappear after dying. Recorded from where the admin is standing,
    // for the same reason /jail works that way: nobody should have to look up
    // coordinates, and "stand in the Afterlife and run this" is a instruction anyone can
    // follow. Persisted, so it survives a restart.
    if (command == "/setspawn")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        const auto* pMovement = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr;
        if (!pMovement)
        {
            Tell(acSender, "Spawn into the world first, then stand where you want people to respawn.");
            return true;
        }

        GServer->SetRespawnPoint(pMovement->Position, pMovement->Rotation.z);

        spdlog::info("{} set the respawn point to ({:.1f}, {:.1f}, {:.1f})", acSender.Username,
                     pMovement->Position.x, pMovement->Position.y, pMovement->Position.z);

        Tell(acSender, fmt::format("Respawn point set here ({:.0f}, {:.0f}, {:.0f}). Players will reappear here when they die.",
                                   pMovement->Position.x, pMovement->Position.y, pMovement->Position.z));
        return true;
    }

    // ------------------------------------------------------------- /name ----
    //
    // What your character is called, which is not your Discord name. Somebody being
    // "noremacxxi" and their character being someone else is the point of roleplay.
    //
    // Separate from appearance because the two are chosen at different moments: a face is
    // fiddled with at a mirror and saves itself, a name is a decision typed ONCE. One
    // name per character: it is part of who the character is, chosen when they are made
    // and kept until they are retired. /character new starts a fresh character, and a
    // fresh character chooses fresh. Dying is not the end of a character on this server -
    // FLATLINED revives the same person - so a name survives death on purpose.
    if (command == "/name")
    {
        const auto nameStart = acLine.find(' ');
        std::string wanted = (nameStart == std::string::npos) ? std::string{} : acLine.substr(nameStart + 1);

        while (!wanted.empty() && wanted.front() == ' ')
            wanted.erase(wanted.begin());

        if (wanted.empty())
        {
            const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(acSender.DiscordId);
            const bool named = pCharacter && pCharacter->NameChosen;

            Tell(acSender, fmt::format("You are called '{}'.",
                                       (pCharacter && !pCharacter->Name.empty()) ? pCharacter->Name
                                                                                 : acSender.Username));
            // The hint matches what typing a name would actually do.
            Tell(acSender, named ? "A name is chosen once per character - /character new starts one that can choose again."
                                 : "Choose it with /name <name>.");
            return true;
        }

        // A name beginning with '/' is a mistyped command, not an identity - one such
        // capture persisted a character literally named '/help' and locked it in.
        if (wanted.front() == '/')
        {
            Tell(acSender, "That looks like a command, not a name - try /name <name> without the slash.");
            return true;
        }

        if (wanted.size() > 32)
            wanted.resize(32);

        auto& store = GServer->GetPlayerStore();
        const auto* pCharacter = store.FindCharacter(acSender.DiscordId);

        if (!pCharacter)
        {
            Tell(acSender, "You have no character yet - look in a mirror first, then set a name.");
            return true;
        }

        // Once per character. Refused rather than applied - if renaming were free, a name
        // would be a chat status instead of an identity.
        if (pCharacter->NameChosen)
        {
            Tell(acSender, fmt::format("This character is already named '{}' - a name is chosen once.",
                                       pCharacter->Name));
            Tell(acSender, "Start a new character with /character new to choose a new name.");
            return true;
        }

        auto updated = *pCharacter;
        updated.Name = wanted;

        // Typing /name is the definition of choosing one, so the prompt stops asking.
        updated.NameChosen = true;

        store.SaveCharacter(acSender.DiscordId, acSender.Username, updated);

        spdlog::info("{} named their character '{}'", acSender.Username, wanted);

        Tell(acSender, fmt::format("You are now known as '{}'.", wanted));
        return true;
    }

    // ------------------------------------------------------------- /whois ----
    //
    // Who is this, by every name they have?
    //
    // Staff see three different identities for one person - the character in front of them,
    // the Discord account that owns it, and the id that outlives both - and until now there
    // was no way to get from any one to the others. That made the character id useless in
    // practice: nothing could tell you what it was.
    if (command == "/whois")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
            return deny(EPermissionLevel::kModerator);

        const auto nameStart = acLine.find(' ');
        std::string query = (nameStart == std::string::npos) ? std::string{} : acLine.substr(nameStart + 1);

        while (!query.empty() && query.front() == ' ')
            query.erase(query.begin());

        if (query.empty())
        {
            Tell(acSender, "Usage: /whois <player>   (character name, Discord name, or id)");
            return true;
        }

        const auto who = findPlayer(query);
        if (!who)
        {
            Tell(acSender, fmt::format("Nobody matching '{}' is online.", query));
            return true;
        }

        const auto* pWho = who.get<PlayerComponent>();
        const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(pWho->DiscordId);

        Tell(acSender, fmt::format("Character : {}",
                                   (pCharacter && !pCharacter->Name.empty()) ? pCharacter->Name
                                                                             : "not named yet"));
        Tell(acSender, fmt::format("Account   : {}", pWho->Username));

        if (pCharacter && !pCharacter->CharacterId.empty())
            Tell(acSender, fmt::format("Character id : {}", pCharacter->CharacterId));
        else
            Tell(acSender, "Character id : none yet - it is assigned when a character is saved.");

        // The Discord id is deliberately NOT shown. It identifies the human being rather
        // than the character, it is the key everything on the server is filed under, and
        // nothing an admin does in chat needs it - /tp and the rest already accept the
        // character id, which is safe to paste anywhere.
        return true;
    }

    // ---------------------------------------------------------- /sellcar ----
    //
    // Offer one of your vehicles to somebody, for money.
    //
    // Two-sided on purpose. A one-sided transfer - "give my car to that player" - cannot
    // charge for itself and cannot be refused, which makes it a gift rather than a sale
    // and makes it a way to force property onto somebody. The offer sits pending until the
    // other person answers.
    if (command == "/sellcar")
    {
        std::istringstream parts(acLine);
        std::string ignored, wanted, buyerName, priceText;
        parts >> ignored >> wanted >> buyerName >> priceText;

        if (wanted.empty() || buyerName.empty() || priceText.empty())
        {
            Tell(acSender, "Usage: /sellcar <id> <player> <price>");
            return true;
        }

        int64_t price = 0;
        try { price = std::stoll(priceText); } catch (...) { price = -1; }

        if (price < 0)
        {
            Tell(acSender, "That is not a price.");
            return true;
        }

        const auto buyer = findPlayer(buyerName);
        if (!buyer)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", buyerName));
            return true;
        }

        const auto* pBuyer = buyer.get<PlayerComponent>();

        if (pBuyer->DiscordId == acSender.DiscordId)
        {
            Tell(acSender, "You cannot sell a car to yourself.");
            return true;
        }

        // Searched among the SELLER's own vehicles only, so a refusal cannot be used to
        // learn what somebody else owns.
        const VehicleRecord* pMatch = nullptr;
        for (const auto& vehicle : GServer->GetVehicles().OwnedBy(acSender.DiscordId))
        {
            if (vehicle.Id.rfind(wanted, 0) == 0 || vehicle.Plate == wanted)
            {
                pMatch = &vehicle;
                break;
            }
        }

        if (!pMatch)
        {
            Tell(acSender, fmt::format("You do not own a vehicle matching '{}'.", wanted));
            return true;
        }

        if (!pMatch->LockedBy.empty())
        {
            Tell(acSender, "That vehicle is already part of a pending sale.");
            return true;
        }

        // The lock is taken NOW, before the buyer is told anything. Between offering and
        // accepting, the seller must not be able to offer the same car to somebody else -
        // otherwise two people can accept and the second transfer silently wins.
        const auto token = GenerateCharacterId();

        if (!GServer->GetVehicles().Lock(pMatch->Id, token))
        {
            Tell(acSender, "That vehicle could not be reserved for a sale.");
            return true;
        }

        m_pendingSales.push_back({token, pMatch->Id, acSender.DiscordId, pBuyer->DiscordId, price,
                                  std::time(nullptr)});

        Tell(acSender, fmt::format("Offered {} (plate {}) to {} for {} eddies. They have two minutes.",
                                   pMatch->ModelName, pMatch->Plate, pBuyer->Username, price));

        Tell(*pBuyer, fmt::format("{} is offering you a {} (plate {}) for {} eddies.",
                                  acSender.Username, pMatch->ModelName, pMatch->Plate, price));
        Tell(*pBuyer, "Type /buycar to accept, or /declinecar to refuse.");

        spdlog::info("{} offered vehicle {} to {} for {}", acSender.Username, pMatch->Id,
                     pBuyer->Username, price);
        return true;
    }

    // ----------------------------------------------------------- /buycar ----
    if (command == "/buycar")
    {
        auto pending = std::find_if(m_pendingSales.begin(), m_pendingSales.end(),
                                    [&acSender](const PendingSale& acSale)
                                    { return acSale.BuyerId == acSender.DiscordId; });

        if (pending == m_pendingSales.end())
        {
            Tell(acSender, "Nobody is offering you a vehicle.");
            return true;
        }

        CompleteSale(*pending, acSender);
        m_pendingSales.erase(pending);
        return true;
    }

    // ------------------------------------------------------- /declinecar ----
    //
    // Either side can call it off - the buyer refusing, or the seller changing their mind.
    if (command == "/declinecar")
    {
        auto pending = std::find_if(m_pendingSales.begin(), m_pendingSales.end(),
                                    [&acSender](const PendingSale& acSale)
                                    {
                                        return acSale.BuyerId == acSender.DiscordId ||
                                               acSale.SellerId == acSender.DiscordId;
                                    });

        if (pending == m_pendingSales.end())
        {
            Tell(acSender, "You have no pending vehicle sale.");
            return true;
        }

        GServer->GetVehicles().Unlock(pending->VehicleId, pending->Token);
        Tell(acSender, "Sale cancelled.");

        m_pendingSales.erase(pending);
        return true;
    }

    // ----------------------------------------------------------- /garage ----
    //
    // What this account owns. Only ever this account - the list comes from the caller's
    // own Discord id rather than anything they can name, so there is no request shape that
    // shows somebody else's cars.
    if (command == "/garage")
    {
        const auto owned = GServer->GetVehicles().OwnedBy(acSender.DiscordId);

        if (owned.empty())
        {
            Tell(acSender, "You do not own any vehicles yet.");
            return true;
        }

        Tell(acSender, fmt::format("You own {} vehicle(s):", owned.size()));

        for (const auto& vehicle : owned)
        {
            Tell(acSender, fmt::format("  [{}]  {}  plate {}{}", vehicle.Id.substr(0, 6),
                                       vehicle.ModelName.empty() ? "unknown model" : vehicle.ModelName,
                                       vehicle.Plate,
                                       vehicle.LockedBy.empty() ? "" : "  (sale pending)"));
        }

        Tell(acSender, "Call them from your phone as normal - this list is the paperwork.");
        return true;
    }

    // ------------------------------------------------------------- /call ----
    //
    // Deliberately gone. The game's own phone menu is the interface.
    //
    // A custom call command was built first and was the wrong shape: Cyberpunk already has
    // a vehicle summon - the phone, the animation, the arrival, the spawn positioning -
    // and players already know it. Replacing that with a chat command means writing a
    // worse version of something that ships with the game.
    //
    // So ownership decides what appears in the phone, and the phone does the summoning.
    // The server pushes EnablePlayerVehicle for every model this account owns at least one
    // of; anything they own nothing of is not in their menu at all.
    if (command == "/call")
    {
        Tell(acSender, "Use your phone - your vehicles are in the vehicle menu, like normal.");
        Tell(acSender, "/garage shows the paperwork: which specific car is which, and its plate.");
        return true;
    }

    // ---------------------------------------------------------- /givecar ----
    //
    // Admin: create an owned vehicle. Stands in for a dealership until there is one.
    if (command == "/givecar")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        const auto space = acLine.find(' ');
        std::string record = (space == std::string::npos) ? std::string{} : acLine.substr(space + 1);

        while (!record.empty() && record.front() == ' ')
            record.erase(record.begin());

        if (record.empty())
        {
            Tell(acSender, "Usage: /givecar <Vehicle.record>   e.g. Vehicle.v_standard2_archer_hella");
            return true;
        }

        const auto id = GServer->GetVehicles().Create(acSender.DiscordId, std::hash<std::string>{}(record),
                                                      record, 0);

        if (id.empty())
        {
            Tell(acSender, "Could not create that vehicle.");
            return true;
        }

        const auto* pCreated = GServer->GetVehicles().Find(id);
        Tell(acSender, fmt::format("Created {} - plate {}. It is yours, and stays yours.", record,
                                   pCreated ? pCreated->Plate : "?"));
        return true;
    }

    // -------------------------------------------------------------- /fact ----
    //
    // Open a door for everyone, and remember it.
    //
    // Which fact unlocks which building is not documented anywhere and is not guessable -
    // it is found by trying one, walking to the door, and seeing whether it opens. So this
    // sets a fact live on everybody AND writes it to config/worldfacts.json, which means
    // finding one is the same action as keeping it. Getting that wrong - making people
    // test in one place and record in another - is how a list of ninety buildings never
    // gets written down.
    // --------------------------------------------------------------- /quest ---
    //
    // Every subcommand is admin-and-up, gated ONCE here rather than per branch. A gate
    // repeated four times is a gate somebody forgets to repeat a fifth time.
    if (command == "/quest")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        const auto restStart = acLine.find(' ');
        std::string rest = (restStart == std::string::npos) ? std::string{} : acLine.substr(restStart + 1);

        while (!rest.empty() && rest.front() == ' ')
            rest.erase(rest.begin());

        const auto usage = [&]()
        {
            Tell(acSender, "Usage: /quest allow <player> <quest>   - let them see it");
            Tell(acSender, "       /quest deny  <player> <quest>   - take it back");
            Tell(acSender, "       /quest skip  <player> <quest>   - mark it done for them");
            Tell(acSender, "       /quest list  <player>           - what they are allowed");
        };

        if (rest.empty())
        {
            usage();
            return true;
        }

        // "<verb> <player> [quest]"
        std::string verb = rest;
        std::string remainder;

        if (const auto space = rest.find(' '); space != std::string::npos)
        {
            verb = rest.substr(0, space);
            remainder = rest.substr(space + 1);
        }

        std::string who = remainder;
        std::string quest;

        if (const auto space = remainder.find(' '); space != std::string::npos)
        {
            who = remainder.substr(0, space);
            quest = remainder.substr(space + 1);
        }

        if (who.empty())
        {
            usage();
            return true;
        }

        const auto subject = findPlayer(who);
        if (!subject)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", who));
            return true;
        }

        const auto* pSubjectPlayer = subject.get<PlayerComponent>();
        if (!pSubjectPlayer)
        {
            Tell(acSender, "That player has no record right now.");
            return true;
        }

        const auto subjectId = pSubjectPlayer->DiscordId;

        if (verb == "list")
        {
            const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(subjectId);

            if (!pCharacter || pCharacter->AllowedQuests.empty())
            {
                Tell(acSender, fmt::format("{} is allowed no quests.", who));
                return true;
            }

            Tell(acSender, fmt::format("{} is allowed {} quest(s):", who, pCharacter->AllowedQuests.size()));
            for (const auto& allowed : pCharacter->AllowedQuests)
                Tell(acSender, fmt::format("  {}", allowed));

            return true;
        }

        if (quest.empty())
        {
            usage();
            return true;
        }

        if (verb == "allow")
        {
            if (!GServer->GetPlayerStore().AllowQuest(subjectId, quest))
            {
                Tell(acSender, fmt::format("{} is already allowed '{}'.", who, quest));
                return true;
            }

            spdlog::info("{} allowed quest '{}' for {}", acSender.Username, quest, who);
            Tell(acSender, fmt::format("Allowed '{}' for {}. They see it on their next reconnect.",
                                       quest, who));
            return true;
        }

        if (verb == "deny")
        {
            if (!GServer->GetPlayerStore().DenyQuest(subjectId, quest))
            {
                Tell(acSender, fmt::format("{} was not allowed '{}' anyway.", who, quest));
                return true;
            }

            spdlog::info("{} denied quest '{}' for {}", acSender.Username, quest, who);
            Tell(acSender, fmt::format("Denied '{}' for {}.", quest, who));
            return true;
        }

        if (verb == "skip")
        {
            // Skipping is a CLIENT action - only the game can move a journal entry - so the
            // server records the instruction and the client carries it out. Told to the
            // subject's client directly rather than broadcast: a quest moving on is not
            // everybody's business.
            //
            // Deliberately noisy about the risk. Marking a quest done moves world state
            // forward, and some of that state is doors - which is the objection Cam raised
            // himself before asking for this. Better said every time than remembered once.
            SendQuestSkip(subject, quest);

            spdlog::info("{} skipped quest '{}' for {}", acSender.Username, quest, who);
            Tell(acSender, fmt::format("Told {}'s game to skip '{}'.", who, quest));
            Tell(acSender, "Note: skipping advances world state, which can unlock or lock doors.");
            return true;
        }

        usage();
        return true;
    }

    // -------------------------------------------------------------- /number ---
    if (command == "/number")
    {
        const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(acSender.DiscordId);

        if (!pCharacter)
        {
            Tell(acSender, "You have no character yet.");
            return true;
        }

        if (pCharacter->PhoneNumber.empty())
        {
            // Says so rather than inventing one on the spot. A number handed out by a read
            // command would not be saved, and the player would give out a number that stops
            // being theirs the moment they reconnect.
            Tell(acSender, "You have no number yet - it is assigned on your next save.");
            return true;
        }

        Tell(acSender, fmt::format("Your number is {}. Give it out and people can add you with "
                                   "/addcontact {}", pCharacter->PhoneNumber, pCharacter->PhoneNumber));
        return true;
    }

    // ---------------------------------------------------------- /addcontact ---
    if (command == "/addcontact")
    {
        if (target.empty())
        {
            Tell(acSender, "Usage: /addcontact 555-014-372   (ask them for their number)");
            return true;
        }

        if (!IsPhoneNumberShaped(target))
        {
            // Separated from "nobody has that number" deliberately. The two failures read
            // identically to a player and have completely different fixes.
            Tell(acSender, fmt::format("'{}' is not a number. They look like 555-014-372.", target));
            return true;
        }

        std::string ownerId;
        const auto* pOwner = GServer->GetPlayerStore().FindCharacterByPhoneNumber(target, &ownerId);

        if (!pOwner)
        {
            Tell(acSender, fmt::format("Nobody has the number {}.", target));
            return true;
        }

        if (ownerId == acSender.DiscordId)
        {
            Tell(acSender, "That is your own number.");
            return true;
        }

        if (!GServer->GetPlayerStore().AddContact(acSender.DiscordId, target))
        {
            Tell(acSender, fmt::format("{} is already in your contacts.", target));
            return true;
        }

        // Only the person who added them is told. Nobody else's phone gains an entry, and
        // the owner is not notified either - looking somebody up is not an event that should
        // announce itself to them.
        Tell(acSender, fmt::format("Added {} - {}.", DisplayNameFor(ownerId, pOwner, target), target));
        return true;
    }

    // ----------------------------------------------------------------- /pay ---
    //
    // Sends eddies to a phone number. The phone app will call this same path - the transfer
    // is deliberately not written inside a UI handler, because the rule that a sender loses
    // exactly what a recipient gains has to hold no matter which surface asked.
    if (command == "/pay")
    {
        const auto restStart = acLine.find(' ');
        std::string rest = (restStart == std::string::npos) ? std::string{} : acLine.substr(restStart + 1);

        while (!rest.empty() && rest.front() == ' ')
            rest.erase(rest.begin());

        std::string number = rest;
        std::string amountText;

        if (const auto space = rest.find(' '); space != std::string::npos)
        {
            number = rest.substr(0, space);
            amountText = rest.substr(space + 1);
        }

        if (number.empty() || amountText.empty())
        {
            Tell(acSender, "Usage: /pay 555-014-372 <amount>");
            return true;
        }

        if (!IsPhoneNumberShaped(number))
        {
            Tell(acSender, fmt::format("'{}' is not a number. They look like 555-014-372.", number));
            return true;
        }

        // Parsed strictly. A silent 0 from a failed parse would report a successful transfer
        // of nothing, which is worse than refusing outright.
        int64_t amount = 0;

        try
        {
            size_t consumed = 0;
            amount = std::stoll(amountText, &consumed);

            if (consumed != amountText.size())
                throw std::invalid_argument("trailing characters");
        }
        catch (...)
        {
            Tell(acSender, fmt::format("'{}' is not an amount.", amountText));
            return true;
        }

        if (amount <= 0)
        {
            // Zero is pointless; negative would be "pay me", which is theft with extra steps.
            Tell(acSender, "Amount must be more than zero.");
            return true;
        }

        auto& store = GServer->GetPlayerStore();

        std::string recipientId;
        const auto* pRecipient = store.FindCharacterByPhoneNumber(number, &recipientId);

        if (!pRecipient)
        {
            Tell(acSender, fmt::format("Nobody has the number {}.", number));
            return true;
        }

        if (recipientId == acSender.DiscordId)
        {
            Tell(acSender, "That is your own number.");
            return true;
        }

        const auto* pSenderCharacter = store.FindCharacter(acSender.DiscordId);

        if (!pSenderCharacter)
        {
            Tell(acSender, "You have no character record.");
            return true;
        }

        // Read from the SERVER's record, never from anything the client claimed. This is the
        // entire reason money became server-owned: a transfer must not be talked into
        // existence by the machine that benefits from it.
        if (pSenderCharacter->Money < amount)
        {
            Tell(acSender, fmt::format("You have {} eddies and tried to send {}.",
                                       pSenderCharacter->Money, amount));
            return true;
        }

        // Both records are written before either client is told anything. The saved balances
        // are the truth; the pushes below only bring the games into line with it, so a client
        // that never receives its push is out of date rather than wrong.
        CharacterRecord sender = *pSenderCharacter;
        CharacterRecord recipient = *pRecipient;

        // Read before the mutation, and out of the copies rather than the store pointers -
        // SaveCharacter below writes through the store, after which the pointers no longer
        // describe the state this transfer started from.
        const int64_t senderBefore = sender.Money;
        const int64_t recipientBefore = recipient.Money;

        sender.Money -= amount;
        recipient.Money += amount;

        store.SaveCharacter(acSender.DiscordId, acSender.Username, sender);
        store.SaveCharacter(recipientId, pRecipient->Name, recipient);

        // Both sides of the transfer, in the ledger, before either client is told anything.
        //
        // Recorded as two lines rather than one so that a balance can be reconstructed per
        // player by filtering on subject alone. If a later character save overwrites one of
        // these balances - the race this transfer already works around by pushing
        // immediately - the ledger is what shows it happened, which nothing could before.
        auto& audit = GServer->GetAuditLog();
        audit.RecordMoney(acSender.DiscordId, acSender.DiscordId, "transfer.sent",
                          senderBefore, sender.Money);
        audit.RecordMoney(acSender.DiscordId, recipientId, "transfer.received",
                          recipientBefore, recipient.Money);

        // The sender is always online - they just typed this - so their game is corrected
        // immediately, or their next autosave would report the old balance and undo the debit.
        PushMoney(acSender, static_cast<int32_t>(sender.Money), "sent");

        // The recipient may not be. An offline recipient needs no push: their record already
        // holds the money and restore applies it when they next spawn.
        const auto recipientEntity = findPlayer(recipientId);

        if (recipientEntity)
        {
            if (const auto* pRecipientPlayer = recipientEntity.get<PlayerComponent>())
            {
                PushMoney(*pRecipientPlayer, static_cast<int32_t>(recipient.Money), "received");
                Tell(*pRecipientPlayer,
                     fmt::format("{} sent you {} eddies.",
                                 DisplayNameFor(acSender.DiscordId, pSenderCharacter, acSender.Username),
                                 amount));
            }
        }

        spdlog::info("{} paid {} eddies to {} ({})", acSender.Username, amount, number, recipientId);

        Tell(acSender, fmt::format("Sent {} eddies to {}. You have {} left.", amount,
                                   DisplayNameFor(recipientId, pRecipient, number), sender.Money));
        return true;
    }

    // ------------------------------------------------------------ /contacts ---
    if (command == "/contacts")
    {
        const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(acSender.DiscordId);

        if (!pCharacter || pCharacter->Contacts.empty())
        {
            Tell(acSender, "No contacts yet. Ask someone for their number and /addcontact it.");
            return true;
        }

        Tell(acSender, fmt::format("{} contact(s):", pCharacter->Contacts.size()));

        for (const auto& number : pCharacter->Contacts)
        {
            const auto* pOwner = GServer->GetPlayerStore().FindCharacterByPhoneNumber(number);

            // A number whose owner has retired still shows, with the truth next to it.
            // Silently dropping it would look like the contact was never added.
            std::string ownerId;
            const auto* pResolved = GServer->GetPlayerStore().FindCharacterByPhoneNumber(number, &ownerId);

            Tell(acSender, fmt::format("  {}  {}", number,
                                       pResolved ? DisplayNameFor(ownerId, pResolved, number)
                                                 : "(no longer in service)"));
        }

        return true;
    }

    if (command == "/fact")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        const auto nameStart = acLine.find(' ');
        std::string rest = (nameStart == std::string::npos) ? std::string{} : acLine.substr(nameStart + 1);

        while (!rest.empty() && rest.front() == ' ')
            rest.erase(rest.begin());

        if (rest.empty() || rest == "list")
        {
            const auto& facts = GServer->GetWorldFacts().All();

            if (facts.empty())
            {
                Tell(acSender, "No world facts set. Usage: /fact <name> [value]   (value defaults to 1)");
                Tell(acSender, "         /fact remove <name>");
                return true;
            }

            Tell(acSender, fmt::format("{} world fact(s):", facts.size()));
            for (const auto& fact : facts)
                Tell(acSender, fmt::format("  {} = {}", fact.Name, fact.Value));

            return true;
        }

        if (rest.rfind("remove ", 0) == 0)
        {
            const auto name = rest.substr(7);

            if (GServer->GetWorldFacts().Remove(name))
                Tell(acSender, fmt::format("Removed '{}'. It stays set on anyone already here until they reconnect.", name));
            else
                Tell(acSender, fmt::format("No fact called '{}'.", name));

            return true;
        }

        // "<name> <value>", or just "<name>" for the usual case of turning something on.
        std::string name = rest;
        int32_t value = 1;

        if (const auto space = rest.find(' '); space != std::string::npos)
        {
            name = rest.substr(0, space);
            try { value = std::stoi(rest.substr(space + 1)); } catch (...) { value = 1; }
        }

        GServer->GetWorldFacts().Set(name, value);

        spdlog::info("{} set world fact '{}' = {}", acSender.Username, name, value);
        Tell(acSender, fmt::format("Set '{}' = {} and saved it. Reconnect to apply it to everyone already here.", name, value));

        return true;
    }

    // ---------------------------------------------------------- /setstart ----
    //
    // Where a brand-new character arrives. Separate from /setspawn on purpose - see
    // GameServer::SetStartPoint.
    if (command == "/setstart")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        const auto* pMovement = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr;
        if (!pMovement)
        {
            Tell(acSender, "Spawn into the world first, then stand where new players should arrive.");
            return true;
        }

        GServer->SetStartPoint(pMovement->Position, pMovement->Rotation.z);

        spdlog::info("{} set the start point to ({:.1f}, {:.1f}, {:.1f})", acSender.Username,
                     pMovement->Position.x, pMovement->Position.y, pMovement->Position.z);

        Tell(acSender, fmt::format("Start point set here ({:.0f}, {:.0f}, {:.0f}). New characters will arrive here.",
                                   pMovement->Position.x, pMovement->Position.y, pMovement->Position.z));
        return true;
    }

    // --------------------------------------------------------------- /time ----
    //
    // Sets the shared world clock. The day number is kept - only the hour moves - so
    // repeatedly testing sunset does not also fast-forward the city's calendar.
    if (command == "/time")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        auto* pClock = m_pWorld->get_mut<WorldClock>();
        if (!pClock)
            return true;

        // Mirror the real world: the server machine's wall clock becomes the game
        // clock at 1:1 - night in Night City when it is night outside.
        if (target == "real")
        {
            pClock->SetRealTime(true);
            spdlog::info("{} switched the world clock to real time", acSender.Username);
            Tell(acSender, "World clock now mirrors real time for everyone. /time HH:MM takes it back.");
            return true;
        }

        int hours = -1, minutes = 0;
        if (std::sscanf(target.c_str(), "%d:%d", &hours, &minutes) < 1 || hours < 0 || hours > 23 ||
            minutes < 0 || minutes > 59)
        {
            Tell(acSender, "Usage: /time HH:MM (24h), or /time real to mirror the real world");
            return true;
        }

        const uint64_t day = pClock->GetGameTimeSeconds() / 86400;
        pClock->SetTime(day * 86400 + static_cast<uint64_t>(hours) * 3600 + static_cast<uint64_t>(minutes) * 60);

        spdlog::info("{} set the world clock to {:02}:{:02}", acSender.Username, hours, minutes);
        Tell(acSender, fmt::format("World clock set to {:02}:{:02} for everyone.", hours, minutes));
        return true;
    }

    // ------------------------------------------------------------ /weather ----
    //
    // Sets the shared sky. Accepts the game's 24_hour_weather_* record names, with or
    // without the prefix - "/weather rain" and "/weather 24_hour_weather_rain" are the
    // same request. The id is the game's TweakDBID (crc32 + length), computable here
    // without the game because that is the whole point of the format.
    if (command == "/weather")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        if (target.empty())
        {
            Tell(acSender, "Usage: /weather <state> - e.g. sunny, rain, toxic_rain, fog, pollution, "
                           "light_clouds, cloudy, heavy_clouds, sandstorm - or 'reset'");
            return true;
        }

        auto* pClock = m_pWorld->get_mut<WorldClock>();
        if (!pClock)
            return true;

        // 0 is the documented "leave the sky alone" sentinel - the client releases the
        // weather back to its natural cycle.
        if (target == "reset" || target == "natural")
        {
            pClock->SetWeather(0, 10.f);
            Tell(acSender, "Weather released back to the natural cycle.");
            return true;
        }

        std::string state = target;
        if (state.rfind("24h_weather_", 0) != 0)
            state = "24h_weather_" + state;

        // The wire carries the CName hash (FNV1a64) - the same convention the client's
        // weather setter consumes. NOT a TweakDBID: weather states are named worldWeather
        // states, not tweak records.
        uint64_t weatherId = 0xCBF29CE484222325ULL;
        for (const unsigned char c : state)
        {
            weatherId ^= c;
            weatherId *= 0x100000001B3ULL;
        }

        pClock->SetWeather(weatherId, 10.f);

        spdlog::info("{} set the weather to {} ({:x})", acSender.Username, state, weatherId);
        Tell(acSender, fmt::format("Weather set to {} for everyone.", state));
        return true;
    }

    // ---------------------------------------------------------------- /npc ----
    //
    // Declares a character into the shared world where the admin is standing. The
    // record is any Character.* TweakDB record the game ships; the rest of the line
    // names them. Server-declared, so every client - now and after every restart -
    // renders the same person on the same spot.
    if (command == "/npc")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        auto* pNpcs = m_pWorld->get_mut<NpcSystem>();
        if (!pNpcs)
            return true;

        if (target.empty())
        {
            Tell(acSender, "Usage: /npc <Character.record> [name] - or /npc clear");
            Tell(acSender, fmt::format("{} NPC(s) currently declared.", pNpcs->Count()));
            return true;
        }

        if (target == "clear")
        {
            const auto removed = pNpcs->Clear();
            spdlog::info("{} cleared {} NPC(s)", acSender.Username, removed);
            Tell(acSender, fmt::format("Removed {} NPC(s) for everyone.", removed));
            return true;
        }

        const auto* pMovement = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr;
        if (!pMovement)
        {
            Tell(acSender, "Spawn into the world first, then stand where the NPC should be.");
            return true;
        }

        std::string record = target;
        if (record.rfind("Character.", 0) != 0)
            record = "Character." + record;

        const std::string name = rest.empty() ? "NPC" : rest;

        pNpcs->Spawn(record, name, pMovement->Position, pMovement->Rotation.z);
        pNpcs->Save();

        spdlog::info("{} declared NPC '{}' ({})", acSender.Username, name, record);
        Tell(acSender, fmt::format("'{}' now exists here for everyone, forever. /npc clear removes all.", name));
        return true;
    }

    // --------------------------------------------------------------- /jail ----
    //
    // The cell is wherever the staff member is standing. No configuration, no coordinates
    // to look up - walk into the room you want to use as a cell, bring them, and jail
    // them. Any building in Night City becomes a holding cell.
    if (command == "/jail")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
            return deny(EPermissionLevel::kModerator);

        if (target.empty())
        {
            Tell(acSender, "Usage: /jail <player> <minutes> [reason]");
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        auto* pVictim = victim.get_mut<PlayerComponent>();

        // Jailing YOURSELF is always allowed.
        //
        // Rank protects rank, but it protected the owner from themselves - the highest
        // rank on the server could not jail anybody at all, including their own
        // character, which made this impossible to try without a second person. Locking
        // yourself up harms nobody, and being able to test a punishment before using it
        // on a player is worth more than the consistency.
        if (victim != aSender && pVictim->Level >= acSender.Level)
        {
            Tell(acSender, "You cannot jail someone at or above your own rank.");
            return true;
        }

        if (pVictim->DiscordId.empty())
        {
            // Nothing durable to hold them by. A sentence that cannot survive a reconnect
            // is not one, so say so rather than pretending.
            Tell(acSender, "That player has no verified Discord account, so a sentence could not be kept.");
            return true;
        }

        // "/jail bob 15 breaking character" - minutes first, the rest is the reason.
        int minutes = 0;
        std::string reason;
        {
            const auto space = rest.find(' ');
            const auto minutesText = space == std::string::npos ? rest : rest.substr(0, space);

            try { minutes = std::stoi(minutesText); } catch (...) { minutes = 0; }

            if (space != std::string::npos)
                reason = rest.substr(space + 1);
        }

        if (minutes <= 0)
        {
            Tell(acSender, "Usage: /jail <player> <minutes> [reason]");
            return true;
        }

        // Capped. An accidental extra digit should not sentence somebody to a week.
        minutes = std::min(minutes, 1440);

        const auto* pCell = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr;
        if (!pCell)
        {
            Tell(acSender, "Stand where you want the cell to be, then jail them.");
            return true;
        }

        // Where they were arrested, so /unjail and the end of the sentence can put them
        // back. Same mechanism /tp uses.
        if (const auto* pTheirs = pVictim->Puppet ? pVictim->Puppet.get<MovementComponent>() : nullptr)
        {
            pVictim->ReturnPosition = pTheirs->Position;
            pVictim->ReturnRotation = pTheirs->Rotation;
            pVictim->HasReturnPoint = true;
        }

        const auto until = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() + (minutes * 60);

        GServer->GetPlayerStore().SetJail(pVictim->DiscordId, pVictim->Username, until,
                                          pCell->Position, acSender.Username, reason);

        // Straight into the cell. EnforceJail would drag them there within the second
        // anyway; doing it now means the sentence starts where it should.
        server::NotifyTeleport teleport;
        common::Vector3 position;
        position.set_x(pCell->Position.x);
        position.set_y(pCell->Position.y);
        position.set_z(pCell->Position.z);
        teleport.set_position(position);
        teleport.set_rotation(pCell->Rotation.z);
        GServer->Send(pVictim->Connection, teleport);

        spdlog::info("{} jailed {} for {} minutes ({})", acSender.Username, pVictim->Username, minutes, reason);

        Broadcast("SERVER", fmt::format("{} was jailed for {} minute(s) by {}{}", pVictim->Username, minutes,
                                        acSender.Username, reason.empty() ? "" : (" - " + reason)).c_str());

        Tell(*pVictim, fmt::format("You are jailed for {} minute(s). Leaving the cell will put you back in it.",
                                   minutes));
        return true;
    }

    // ------------------------------------------------------------- /unjail ----
    if (command == "/unjail")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
            return deny(EPermissionLevel::kModerator);

        if (target.empty())
        {
            Tell(acSender, "Usage: /unjail <player>");
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        auto* pVictim = victim.get_mut<PlayerComponent>();
        const auto* pRecord = GServer->GetPlayerStore().Find(pVictim->DiscordId);

        if (!pRecord || pRecord->JailedUntil == 0)
        {
            Tell(acSender, fmt::format("{} is not jailed.", pVictim->Username));
            return true;
        }

        GServer->GetPlayerStore().ClearJail(pVictim->DiscordId);

        // Released back where they were arrested, if that is still known.
        if (pVictim->HasReturnPoint)
        {
            server::NotifyTeleport teleport;
            common::Vector3 position;
            position.set_x(pVictim->ReturnPosition.x);
            position.set_y(pVictim->ReturnPosition.y);
            position.set_z(pVictim->ReturnPosition.z);
            teleport.set_position(position);
            teleport.set_rotation(pVictim->ReturnRotation.z);
            GServer->Send(pVictim->Connection, teleport);

            pVictim->HasReturnPoint = false;
        }

        spdlog::info("{} released {} from jail", acSender.Username, pVictim->Username);

        Broadcast("SERVER", fmt::format("{} was released by {}", pVictim->Username, acSender.Username).c_str());
        Tell(*pVictim, "You have been released.");
        return true;
    }

    // ------------------------------------------------------------- /return ----
    //
    // Puts someone back where /tp took them from. The counterpart to a summon: staff
    // pulling a player out of whatever they were doing should be able to undo it.
    if (command == "/return")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        if (target.empty())
        {
            Tell(acSender, "Usage: /return <player>");
            return true;
        }

        const auto victim = findPlayer(target);
        if (!victim)
        {
            Tell(acSender, fmt::format("No player called '{}' is online.", target));
            return true;
        }

        auto* pVictim = victim.get_mut<PlayerComponent>();

        if (!pVictim->HasReturnPoint)
        {
            Tell(acSender, fmt::format("{} has not been teleported, so there is nowhere to send them back to.",
                                       pVictim->Username));
            return true;
        }

        // A few metres short of where they were, facing the way they were facing.
        //
        // Landing on the exact spot is asking for trouble: whatever they were standing in
        // or next to may have moved, and materialising inside it drops people through the
        // world or wedges them in geometry. Arriving a short walk behind their own
        // footprints reads as being put back without pretending the world stood still.
        const float yaw = pVictim->ReturnRotation.z;
        const glm::vec3 forward{-std::sin(yaw), std::cos(yaw), 0.f};
        const glm::vec3 destination = pVictim->ReturnPosition - forward * kReturnBackoff;

        server::NotifyTeleport teleport;

        common::Vector3 position;
        position.set_x(destination.x);
        position.set_y(destination.y);
        position.set_z(destination.z);
        teleport.set_position(position);
        teleport.set_rotation(yaw);

        GServer->Send(pVictim->Connection, teleport);

        // Cleared so a second /return does not silently send them to a stale spot they
        // may have long since walked away from.
        pVictim->HasReturnPoint = false;

        spdlog::info("{} returned {} to ({:.1f}, {:.1f}, {:.1f})", acSender.Username, pVictim->Username,
                     destination.x, destination.y, destination.z);

        Tell(acSender, fmt::format("Sent {} back.", pVictim->Username));
        Tell(*pVictim, "You were sent back to where you were.");
        return true;
    }

    // ---------------------------------------------------------- /character ----
    //
    // The multiplayer character system, before it has a UI.
    //
    // Everything here is deliberately usable by one person with no second player and no
    // launcher work: the point is to find out whether the storage half is right before
    // building a creator on top of it. The creator replaces /character save, not the rest.
    if (command == "/character" || command == "/char")
    {
        auto& store = GServer->GetPlayerStore();

        if (target.empty() || target == "show")
        {
            const auto* pCharacter = store.FindCharacter(acSender.DiscordId);

            if (!pCharacter)
            {
                Tell(acSender, "You have no multiplayer character yet.");
                Tell(acSender, "  /character save <name>  - save how you look now as your character");
                return true;
            }

            Tell(acSender, fmt::format("Character: {}", pCharacter->Name.empty() ? "unnamed" : pCharacter->Name));
            Tell(acSender, fmt::format("  appearance {} bytes stored, {}",
                                       Base64::Decode(pCharacter->Appearance).size(),
                                       pCharacter->Initialised ? "initialised" : "not initialised yet"));
            Tell(acSender, "  /character new <name>  - retire this one and start again");
            return true;
        }

        // Asks the client for the appearance it has RIGHT NOW.
        //
        // Deliberately not read from the puppet's AppearanceComponent: that was captured
        // at spawn, so it would save whatever they looked like when they joined and
        // silently discard everything they just did in the creator. The client is the only
        // side that knows the current state, so the client is asked.
        //
        // The name is remembered here and applied when the appearance comes back, because
        // the reply carries no idea of what the player typed.
        if (target == "save")
        {
            if (!rest.empty())
            {
                auto* pMutable = aSender.get_mut<PlayerComponent>();
                // Same guard as /name: a slash-prefixed reply is a mistyped command.
                if (!rest.empty() && rest.front() == '/')
                {
                    Tell(acSender, "That looks like a command, not a name - names cannot start with '/'.");
                    return true;
                }

                pMutable->PendingCharacterName = rest.substr(0, 32);
            }

            server::OpenCharacterCreator capture;
            capture.set_capture_only(true);
            GServer->Send(acSender.Connection, capture);

            return true;
        }

        // How to change your appearance.
        //
        // Driving the game's creator directly is not possible from scripts - its system is
        // native-only, checked against the 2.31 type hierarchy rather than assumed. The
        // mirror is the game's own answer to the same problem and already works in a live
        // world, so it is what players are pointed at.
        //
        // Nothing to run afterwards: the client notices when the mirror closes and saves
        // it. Making players type a command to keep their own face was an implementation
        // limitation showing through the design.
        if (target == "create" || target == "edit")
        {
            Tell(acSender, "Visit any ripperdoc and change how you look - they do appearance, not just cyberware.");
            Tell(acSender, "It saves by itself when you close it. Use /name to choose what you are called.");
            return true;
        }

        if (target == "new")
        {
            if (store.RetireCharacter(acSender.DiscordId))
                Tell(acSender, "Your old character has been retired - it is kept, not deleted.");
            else
                Tell(acSender, "You had no character yet, so there was nothing to retire.");

            Tell(acSender, "Change how you look at any ripperdoc - it saves by itself.");
            return true;
        }

        Tell(acSender, "Usage: /character [show | create | new | save <name>]");
        return true;
    }

    // --------------------------------------------------------------- /help ----
    //
    // Nobody discovers a chat channel by accident, and an unlisted feature may as well
    // not exist. Only the commands the asker can actually use are listed - offering
    // someone /ban and then refusing it is worse than not mentioning it.
    if (command == "/help")
    {
        Tell(acSender, "Chat:");
        Tell(acSender, fmt::format("  just type          - local, heard within {:.0f}m", ChatRange::kLocal));
        Tell(acSender, fmt::format("  /yell <message>    - heard within {:.0f}m", ChatRange::kYell));
        Tell(acSender, fmt::format("  /whisper <message> - heard within {:.0f}m", ChatRange::kWhisper));

        if (acSender.HasAtLeast(EPermissionLevel::kAdmin))
            Tell(acSender, "  /advert <message>  - the whole server");

        Tell(acSender, "Other: /who");

        if (acSender.HasAtLeast(EPermissionLevel::kModerator))
        {
            Tell(acSender, "Staff: /kick <player> [reason], /bans");
            Tell(acSender, "       /jail <player> <minutes> [reason] - cell is where you stand");
            Tell(acSender, "       /unjail <player>");
            Tell(acSender, "       /kill <player> [reason] - sends them to the respawn point");
        }

        if (acSender.HasAtLeast(EPermissionLevel::kAdmin))
        {
            Tell(acSender, "Admin: /ban <player> [reason], /unban <discord id>");
            Tell(acSender, "       /tp <player>    - brings them to you");
            Tell(acSender, "       /tp to <player> - sends you to them");
            Tell(acSender, "       /return <player>");
            Tell(acSender, "       /setspawn - where players wake up after being downed");
            Tell(acSender, "       /setstart - where brand-new characters arrive");
            Tell(acSender, "       /time HH:MM - set the shared world clock (/time real = mirror reality)");
            Tell(acSender, "       /weather <state> - set the shared sky (sunny, rain, fog...)");
            Tell(acSender, "       /npc <record> [name] - declare a persistent NPC here (/npc clear)");
        }

        return true;
    }

    // ---------------------------------------------------------------- /who ----
    if (command == "/who")
    {
        // Answered to the asker alone. One person checking who is online should not
        // print the roster into everyone else's chat.
        m_pWorld->each(
            [&](flecs::entity, const PlayerComponent& aOther)
            {
                Tell(acSender, fmt::format("{} [{}]", aOther.Username, ToString(aOther.Level)));
            });
        return true;
    }

    return false;
}

void ChatSystem::HandleChatMessageRequest(const PacketEvent<client::ChatMessageRequest>& aMessage)
{
    auto* pPlayerManager = m_pWorld->get<PlayerManager>();

    const auto entity = pPlayerManager->GetByConnectionId(aMessage.ConnectionId);
    if (!entity || !entity.has<PlayerComponent>())
    {
        spdlog::error("Received chat message from connection with no associated player!");
        return;
    }
    auto* pPlayer = entity.get<PlayerComponent>();

    // Length is checked before the message is logged, let alone broadcast.
    //
    // Chat is relayed verbatim to everyone in range and written to the log. With no cap, a
    // single client could send a megabyte and have the server copy it to every player and
    // to disk - a denial of service that costs the sender one packet, and needs no exploit
    // beyond a text field with no maximum.
    //
    // 512 is far more than anyone types. Truncating rather than dropping keeps an
    // over-long message readable instead of making it vanish with no explanation.
    constexpr size_t kMaxChatLength = 512;

    std::string line = aMessage.get_message().c_str();

    if (line.size() > kMaxChatLength)
    {
        spdlog::warn("Truncated a {}-byte chat message from {}", line.size(), pPlayer->Username);
        line.resize(kMaxChatLength);
    }

    // Control characters are stripped. They do nothing useful in a chat box and newlines
    // in particular let one message forge several lines in the log, which is how a chat
    // message starts pretending to be a server notice.
    line.erase(std::remove_if(line.begin(), line.end(),
                              [](unsigned char c) { return c < 0x20 && c != '\t'; }),
               line.end());

    if (line.empty())
        return;

    spdlog::info("[chat] [{}]: {}", pPlayer->Username, line);

    // Moderation commands. Every one of these checks the permission level the SERVER
    // derived from Discord at connect time - never anything the client said about itself.
    if (HandleModerationCommand(entity, *pPlayer, line))
        return;

    // Debug command: spawn a fake remote player next to the sender.
    //
    // The client-side crash we're chasing only happens when a REMOTE player spawns,
    // which normally needs a second person connected. Level::Add() broadcasts a
    // NotifyCharacterLoad for any entity with a MovementComponent, so a fabricated
    // one drives exactly the same client path - letting a single player reproduce
    // the crash on demand with a debugger attached.
    // Remove every dummy. Deliberately not "the last one" - they are a test tool, they
    // accumulate while you are chasing something, and the only thing anyone ever wants is
    // for all of them to be gone.
    // Same dummy, stamped with the server's clock instead of yours. See DummyWalkComponent.
    if (line == "/dummy servertick")
    {
        // Admin only, and answered privately.
        //
        // These are debugging tools that happened to be reachable by anybody, announcing
        // themselves to the whole server. A player mid-roleplay does not need to read
        // "Spawned a dummy stamped with the SERVER clock" - that sentence is addressed to
        // whoever is chasing a bug, and to nobody else.
        //
        // Checked against pPlayer rather than acSender: this handler works from the
        // PlayerComponent it looked up, and does not have the sender reference the command
        // dispatcher further down uses.
        if (!pPlayer->HasAtLeast(EPermissionLevel::kAdmin))
        {
            Tell(*pPlayer, "That is an admin command.");
            return;
        }

        auto* pOwnPuppet = pPlayer->Puppet ? pPlayer->Puppet.get<MovementComponent>() : nullptr;
        if (!pOwnPuppet)
        {
            Tell(*pPlayer, "Spawn into the world first.");
            return;
        }

        auto position = pOwnPuppet->Position;
        position.x += 5.f;

        const auto* pOwnAppearance = pPlayer->Puppet.get<AppearanceComponent>();

        auto dummy = m_pWorld->entity()
            .set<MovementComponent>({position, pOwnPuppet->Rotation, 0.f, pOwnPuppet->Tick})
            .set<CharacterComponent>({true})
            .set<AppearanceComponent>(pOwnAppearance ? *pOwnAppearance : AppearanceComponent{{}, {}})
            .set<DummyWalkComponent>({position, pPlayer->Puppet, 0.f, 5.f, true});

        m_pWorld->get_mut<Level>()->Add(dummy);

        spdlog::info("[dummy] spawned a SERVER-CLOCK dummy for {} - if this one freezes, the clock is the freeze",
                     pPlayer->Username);
        Tell(*pPlayer, "Spawned a dummy stamped with the SERVER clock. If it stands still while /dummy walks, the clock is the bug.");
        return;
    }

    if (line == "/dummy clear" || line == "/dummy remove")
    {
        // Admin only, and answered privately.
        //
        // These are debugging tools that happened to be reachable by anybody, announcing
        // themselves to the whole server. A player mid-roleplay does not need to read
        // "Spawned a dummy stamped with the SERVER clock" - that sentence is addressed to
        // whoever is chasing a bug, and to nobody else.
        //
        // Checked against pPlayer rather than acSender: this handler works from the
        // PlayerComponent it looked up, and does not have the sender reference the command
        // dispatcher further down uses.
        if (!pPlayer->HasAtLeast(EPermissionLevel::kAdmin))
        {
            Tell(*pPlayer, "That is an admin command.");
            return;
        }

        int removed = 0;

        // Collected first, deleted after. Destroying entities inside the iteration
        // invalidates it, which is the sort of thing that works until the day somebody
        // spawns three.
        Vector<flecs::entity> doomed;
        m_pWorld->each([&doomed](flecs::entity aEntity, const DummyWalkComponent&) { doomed.push_back(aEntity); });

        for (auto entity : doomed)
        {
            m_pWorld->get_mut<Level>()->Remove(entity);
            ++removed;
        }

        spdlog::info("[dummy] {} removed by {}", removed, pPlayer->Username);
        Tell(*pPlayer, fmt::format("Removed {} dumm{}.", removed, removed == 1 ? "y" : "ies"));
        return;
    }

    if (line == "/dummy")
    {
        // Admin only, and answered privately.
        //
        // These are debugging tools that happened to be reachable by anybody, announcing
        // themselves to the whole server. A player mid-roleplay does not need to read
        // "Spawned a dummy stamped with the SERVER clock" - that sentence is addressed to
        // whoever is chasing a bug, and to nobody else.
        //
        // Checked against pPlayer rather than acSender: this handler works from the
        // PlayerComponent it looked up, and does not have the sender reference the command
        // dispatcher further down uses.
        if (!pPlayer->HasAtLeast(EPermissionLevel::kAdmin))
        {
            Tell(*pPlayer, "That is an admin command.");
            return;
        }

        auto* pOwnPuppet = pPlayer->Puppet ? pPlayer->Puppet.get<MovementComponent>() : nullptr;
        if (!pOwnPuppet)
        {
            spdlog::warn("[dummy] sender has no puppet yet - spawn into the world first");
            Tell(*pPlayer, "Spawn into the world first, then try /dummy again.");
            return;
        }

        // EXPERIMENT - distance is the one variable being changed.
        //
        // Puppets that arrive at connect time (at stale positions, far from the player)
        // complete the entire pipeline. A puppet spawned 2m away dies inside the game's
        // async assembly ~40ms later, before any of our code touches it again. Neither is
        // ever actually visible, so we cannot yet tell whether the trigger is PROXIMITY
        // (the game streams in and builds the puppet's mesh) or TIMING (mid-session spawns
        // differ from connect-time ones for some other reason).
        //
        // Five metres, in front of you, where you can actually watch it.
        //
        // This was 200m for an experiment about the spawn crash: the question then was
        // whether a puppet the game never builds a mesh for still crashes the client, and
        // being invisible was the point. That crash was fixed on 12 Aug and the distance
        // outlived its reason - leaving a test command whose subject nobody could see.
        auto position = pOwnPuppet->Position;
        position.x += 5.f;

        // Deliberately NOT child_of(entity). Level::Add takes the entity's parent as its
        // owner and skips that player when broadcasting the spawn - you are not told about
        // your own puppet. Parenting the dummy to the sender therefore excluded the only
        // connected client from the very notification this command exists to trigger: the
        // server reported success, and the client was never asked to spawn anything.
        //
        // With no parent, owner is invalid, matches no player, and everyone is notified -
        // which is what a real remote player looks like to the person seeing it.
        // Dressed like a real player, not a bare mannequin.
        //
        // The empty version walks perfectly, which proves the interpolation pipeline is
        // sound and makes the difference between it and a real player the whole question.
        // Appearance is the largest one: a real remote arrives with ~6.5KB of ccstate and
        // eight equipment items, all applied by ApplyAppearance before the first movement
        // is drawn. This borrows the summoner's own, so the dummy goes through exactly
        // that path.
        //
        // If a dressed dummy freezes and a naked one walks, the fault is in what applying
        // an appearance does to the puppet - and that is reproducible by one person in
        // thirty seconds, instead of needing two people online.
        const auto* pOwnAppearance = pPlayer->Puppet.get<AppearanceComponent>();

        auto dummy = m_pWorld->entity()
            .set<MovementComponent>({position, pOwnPuppet->Rotation, 0.f, pOwnPuppet->Tick})
            .set<CharacterComponent>({true})
            .set<AppearanceComponent>(pOwnAppearance ? *pOwnAppearance : AppearanceComponent{{}, {}})
            .set<DummyWalkComponent>({position, pPlayer->Puppet, 0.f, 5.f});

        spdlog::info("[dummy] dressed with {} bytes of ccstate and {} equipment item(s)",
                     pOwnAppearance ? pOwnAppearance->ccstate.size() : 0,
                     pOwnAppearance ? pOwnAppearance->equipment.size() : 0);

        spdlog::info("[dummy] spawning fake remote player {:x} at ({:.1f}, {:.1f}, {:.1f}) for {}",
                     static_cast<uint64_t>(dummy), position.x, position.y, position.z, pPlayer->Username);

        m_pWorld->get_mut<Level>()->Add(dummy);

        Tell(*pPlayer, "Spawned a dummy 5m away. It walks in a circle - if it stands still, remote movement is broken.");
        return;
    }

    // ------------------------------------------------------------------ channels ---
    //
    // Everything below is ordinary talking. Which channel it goes out on decides how far
    // it carries, and that is the whole point: a roleplay server where every line reaches
    // everyone is a group chat with a game attached.

    std::string text;
    float range = ChatRange::kLocal;
    bool everyone = false;
    uint32_t channel = ChatChannel::kLocal;

    if (!ResolveChannel(*pPlayer, line, text, range, everyone, channel))
        return; // Refused or misused - ResolveChannel already said why.

    // Chat shows the CHARACTER's name.
    //
    // The scanner already did - Level::Serialize sends it - so a player could be scanned
    // as one person and then speak as another, which is worse than either alone. Somebody
    // being "noremacxxi" while their character is somebody else is the point of roleplay,
    // and the account name leaking into the one place people read constantly undoes it.
    //
    // Falls back to the account name only until a character exists.
    std::string speaker = pPlayer->Username;

    if (const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(pPlayer->DiscordId))
    {
        if (!pCharacter->Name.empty())
            speaker = pCharacter->Name;
    }

    if (everyone)
    {
        Broadcast(speaker.c_str(), text.c_str(), channel);
        return;
    }

    // Ranged chat needs somewhere to speak from. Someone connected but not yet spawned
    // has no position, so there is no honest way to decide who is close enough.
    const auto* pMovement = pPlayer->Puppet ? pPlayer->Puppet.get<MovementComponent>() : nullptr;
    if (!pMovement)
    {
        Tell(*pPlayer, "You need to be in the world before anyone can hear you.");
        return;
    }

    BroadcastInRange(speaker, text, pMovement->Position, range, entity, channel);
}
