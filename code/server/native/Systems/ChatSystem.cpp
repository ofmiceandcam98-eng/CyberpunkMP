#include "ChatSystem.h"

// Explicit rather than transitive. This file uses std::map for /vehseats and the string
// conversions for the trade commands, and MSVC supplies both through headers that GCC
// does not - which is a difference that only shows up on the Linux build the server
// actually runs on, long after it compiles cleanly here.
#include <algorithm>   // std::transform, for folding a /help topic to lower case
#include <cctype>      // std::tolower - reached transitively under MSVC, not under GCC
#include <map>
#include <string>
#include <vector>
#include <utility>

#include "GameServer.h"
#include "Components/PlayerComponent.h"
#include "Components/MovementComponent.h"
#include "Components/AppearanceComponent.h"
#include "Components/AttachmentComponent.h"   // who is sitting where, for /vehseats
#include "Components/HealthComponent.h"       // the medical state lives on it
#include "EconomyMutator.h"          // the one place money and inventory change
#include "Medical.h"                          // bleedout, treatment times, life states
#include "VehicleSeats.h"                     // and what to call the seat they are in
#include "CharacterRecord.h"
#include "StarterKit.h"
#include "Components/CharacterComponent.h"
#include "Game/Level.h"
#include "Game/WorldClock.h"
#include "Systems/NpcSystem.h"

#include "PlayerManager.h"
#include <functional>
#include <stdexcept>

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
    GServer->RegisterHandler<&ChatSystem::HandleDeleteCharacterRequest>(this);
    GServer->RegisterHandler<&ChatSystem::HandleSelectCharacterRequest>(this);
    GServer->RegisterHandler<&ChatSystem::HandleCallRequest>(this);
    GServer->RegisterHandler<&ChatSystem::HandleCallControlRequest>(this);

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

void ChatSystem::SendCharacterList(const PlayerComponent& acPlayer, const std::string& acError)
{
    server::NotifyCharacterList message;

    if (!acPlayer.DiscordId.empty())
    {
        // EVERY character on the account, not just the one in play.
        //
        // This used to send the active character wrapped in a list of one, which made the
        // list shape a lie: the wire has always carried a list, deliberately, and the server
        // was the thing collapsing it. A selector cannot draw four slots from a roster that
        // only ever contains the slot you are already in.
        const auto& store = GServer->GetPlayerStore();
        const auto* pRecord = store.Find(acPlayer.DiscordId);

        if (pRecord && !pRecord->Characters.empty())
        {
            Vector<server::CharacterSummary> characters;

            for (const auto& character : pRecord->Characters)
            {
                server::CharacterSummary summary;
                summary.set_id(character.CharacterId.c_str());
                summary.set_name(character.Name.c_str());
                summary.set_level(character.Level);
                summary.set_spawned_before(character.SpawnedBefore);

                // The slot is for DRAWING, never for identity. Slots are not contiguous -
                // retiring the character in slot 1 of three leaves 0 and 2 occupied - so the
                // client draws holes where the gaps are rather than renumbering. Anything
                // keyed on a slot number that moves is a bug waiting for a deletion.
                summary.set_slot(character.Slot);
                summary.set_is_active(character.Slot == pRecord->ActiveSlot);

                characters.push_back(summary);
            }

            message.set_characters(characters);
        }
    }

    if (!acError.empty())
        message.set_error(acError.c_str());

    GServer->Send(acPlayer.Connection, message);
}

/**
 * "Play as the character in this slot."
 *
 * Refuses an empty slot rather than falling back to the first one. "You asked for a
 * character that is not there, so here is a different one" is exactly how somebody ends up
 * playing - and then saving over - a character they did not choose.
 *
 * Answers nothing directly. Selection is a request; the real answer is the spawn that
 * follows, or a refusal carried back on the character list. A caller that waits for a
 * verdict here would wait forever.
 */
void ChatSystem::HandleSelectCharacterRequest(const PacketEvent<client::SelectCharacterRequest>& aMessage)
{
    auto* pPlayerManager = m_pWorld->get<PlayerManager>();

    const auto entity = pPlayerManager->GetByConnectionId(aMessage.ConnectionId);
    if (!entity || !entity.has<PlayerComponent>())
        return;

    const auto* pPlayer = entity.get<PlayerComponent>();

    if (pPlayer->DiscordId.empty())
    {
        SendCharacterList(*pPlayer, "Your Discord sign-in could not be verified.");
        return;
    }

    // Switching characters while standing in the world as one of them would leave a body
    // behind belonging to a character nobody is playing any more, and the autosave would
    // then write the new character's state over the old one's record.
    if (pPlayer->Puppet && pPlayer->Puppet.is_alive())
    {
        SendCharacterList(*pPlayer, "Leave the world before switching characters.");
        return;
    }

    auto& store = GServer->GetPlayerStore();

    /**
     * The outgoing character's call ends here, before the switch.
     *
     * A call belongs to the CHARACTER that made it. Carrying one across a switch would put
     * the previous character's conversation on the next character's phone, which is the
     * single clearest way to leak one character's digital life into another's - and the
     * person on the other end would be talking to somebody who has left without ever being
     * told.
     *
     * Read before SelectSlot, because afterwards the active character is the NEW one and
     * the id that needs hanging up is no longer reachable from this account.
     *
     * In practice the puppet check above already refuses a switch made while in the world,
     * and a call needs both parties in it - so this is belt and braces. It stays because
     * the puppet rule is a separate decision that could be relaxed later, and if it is,
     * this is the line that stops a call surviving the change.
     */
    if (const auto* pOutgoing = store.FindCharacter(pPlayer->DiscordId))
    {
        if (!pOutgoing->CharacterId.empty())
            EndCallFor(pOutgoing->CharacterId, CallState::Ended);
    }

    const std::string refusal = store.SelectSlot(pPlayer->DiscordId, aMessage.get_slot());

    if (!refusal.empty())
    {
        spdlog::info("{} could not select slot {}: {}", pPlayer->Username, aMessage.get_slot(),
                     refusal);

        SendCharacterList(*pPlayer, refusal == "empty_slot"
                                        ? "There is no character in that slot."
                                        : "That character could not be selected.");
        return;
    }

    spdlog::info("{} selected slot {}", pPlayer->Username, aMessage.get_slot());

    // The roster goes back so the panel can redraw with the new active row marked. The
    // player still has to press play; this only decides who they will be when they do.
    SendCharacterList(*pPlayer, {});
}

void ChatSystem::HandleDeleteCharacterRequest(const PacketEvent<client::DeleteCharacterRequest>& aMessage)
{
    auto* pPlayerManager = m_pWorld->get<PlayerManager>();

    const auto entity = pPlayerManager->GetByConnectionId(aMessage.ConnectionId);
    if (!entity || !entity.has<PlayerComponent>())
        return;

    const auto* pPlayer = entity.get<PlayerComponent>();

    // Whose character this is comes from the CONNECTION. The request carries no id, so
    // "delete mine" cannot be spelled "delete theirs" - the one request where getting that
    // wrong cannot be undone by the person it happened to.
    if (pPlayer->DiscordId.empty())
    {
        SendCharacterList(*pPlayer, "Your Discord sign-in could not be verified.");
        return;
    }

    // Not while they are standing in the world as it.
    //
    // A puppet means they are spawned. Retiring the record underneath a live character
    // leaves the server simulating somebody whose record has gone, and the next autosave
    // would write the deleted character straight back - a delete that silently undoes
    // itself is worse than one that refuses.
    if (pPlayer->Puppet && pPlayer->Puppet.is_alive())
    {
        SendCharacterList(*pPlayer, "Leave the world before deleting your character.");
        Tell(*pPlayer, "You cannot delete your character while you are playing as it.");
        return;
    }

    auto& store = GServer->GetPlayerStore();

    // Which slot the player pointed at. The slot is resolved to a character HERE, on the
    // server, and everything about that character is re-checked - a client naming a slot it
    // does not own, or one that is empty, gets a refusal rather than a deletion.
    //
    // Falls back to the active slot so an older client, which sends no slot at all, keeps
    // behaving exactly as it did.
    const int slot = aMessage.has_slot() ? aMessage.get_slot() : -1;

    const auto* pCharacters = store.GetCharacters(pPlayer->DiscordId);
    const CharacterRecord* pTarget = nullptr;

    if (pCharacters)
    {
        for (const auto& character : *pCharacters)
        {
            const bool wanted = (slot < 0) ? (&character == store.FindCharacter(pPlayer->DiscordId))
                                           : (character.Slot == slot);
            if (wanted)
            {
                pTarget = &character;
                break;
            }
        }
    }

    if (!pTarget)
    {
        SendCharacterList(*pPlayer, "There is no character in that slot.");
        return;
    }

    // Named in the log BEFORE it goes, because after the retire this record is somewhere
    // else and "which character did they just delete" becomes a question rather than a fact.
    spdlog::info("{} is deleting '{}' ({}) from slot {}", pPlayer->Username,
                 pTarget->Name.empty() ? "unnamed" : pTarget->Name, pTarget->CharacterId,
                 pTarget->Slot);

    // Retired, not destroyed. The store keeps it in RetiredCharacters precisely so that
    // "I clicked the wrong thing" has an answer that is not "it is gone" - and nothing
    // about a trash can in a menu makes that less true.
    //
    // Owned vehicles and other character-owned records are deliberately NOT cascaded here.
    // Working out what should be deleted, transferred or orphaned is its own decision, and
    // a delete that quietly took someone's cars with it would be discovered far too late.
    const bool retired = store.RetireCharacter(pPlayer->DiscordId, pTarget->Slot);

    if (!retired)
    {
        SendCharacterList(*pPlayer, "That character could not be deleted.");
        return;
    }

    GServer->GetAuditLog().Record("character.delete", pPlayer->DiscordId, pPlayer->DiscordId);

    spdlog::info("{} deleted their character", pPlayer->Username);

    SendCharacterList(*pPlayer);
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

    // BODY TYPE IS CHOSEN ONCE AND CANNOT CHANGE.
    //
    // Cyberpunk fixes body type at character creation - no ripperdoc, mirror or menu
    // changes it - so a save that flips an established character's body is never a
    // player's decision. It is a capture of somebody, or something, else: the state read
    // before the creator committed, the world template's default, another character's
    // save. Whatever the mechanism, the stored appearance is the good copy and the
    // incoming one is not, so the WHOLE save is refused rather than half-applied.
    //
    // Live, 2026-09-04: noremacxxi picked male and every client rendered a female corpo V
    // in prologue clothes, because the server recorded is_male=false from one such capture
    // and then faithfully broadcast it to everyone. The server already had the field it
    // needed to know better - it simply trusted it. It no longer does.
    //
    // Deliberately narrow, so it can never block making a character: it fires only for a
    // character that has an appearance stored AND has actually been played. A character
    // mid-creation sets its body freely, which is the one time it is allowed to.
    if (CharacterRecord::WouldFlipEstablishedBody(pExisting, aMessage.get_is_male()))
    {
        spdlog::error("[Character] REFUSED a save from {} - it would flip an established "
                      "character's body type ({} -> {}). Body type is fixed at creation, so "
                      "this capture is of the wrong character; the stored appearance is kept.",
                      pPlayer->Username, pExisting->IsMale ? "male" : "female",
                      aMessage.get_is_male() ? "male" : "female");

        Tell(*pPlayer, "That save was refused - it did not match your character's body type, "
                       "so your saved look was kept. If you meant to start over, use "
                       "/character new confirm.");
        return;
    }

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

        /*
         * IMPOSSIBLE values are refused. PLAUSIBLE ones are still trusted - deliberately.
         *
         * The note below is the standing decision and it stands: the client's figure is
         * accepted and recorded rather than refused, because there is no server-side balance
         * to fall back to yet, and refusing a client that is merely AHEAD of the server would
         * take money off innocent players. Measure first. That is phase 5's job, not this
         * commit's.
         *
         * But "ahead of the server" describes a delta of a few thousand eddies. It does not
         * describe a negative balance, and it does not describe a number larger than the
         * game can produce. Those are not a client racing the server; they are a client that
         * is wrong or lying, and writing them costs something either way:
         *
         *   - a NEGATIVE balance makes AvailableMoney negative, so every "can you afford
         *     this" test fails forever and the character is permanently unable to trade or
         *     pay. It is also a value no legitimate path produces - spending is already
         *     floored at zero everywhere it happens.
         *   - an ABSURD balance is the trivial form of the exploit this whole file worries
         *     about, and it is the one shape that needs no timing and no race to pull off.
         *
         * The ceiling is deliberately far above any real player rather than tuned to be
         * tight. The point is to refuse what cannot happen, not to police what might - a
         * limit somebody could reach by playing would be exactly the "taking money off
         * players" failure the standing decision warns against.
         *
         * Refusing means KEEPING THE STORED BALANCE, not zeroing it. A bad claim must not be
         * able to destroy a character's money either.
         */
        constexpr int64_t kMaxPlausibleMoney = 1'000'000'000;

        const int64_t claimed = aMessage.get_money();

        if (claimed < 0 || claimed > kMaxPlausibleMoney)
        {
            character.Money = storedMoney;

            spdlog::error("[MONEY] REFUSED an impossible balance from {}: claimed {}, keeping {}. "
                          "Negative or above {} cannot be produced by play.",
                          pPlayer->Username, claimed, storedMoney, kMaxPlausibleMoney);

            GServer->GetAuditLog().Record("money.refused", pPlayer->DiscordId, pPlayer->DiscordId,
                                          {{"claimed", claimed}, {"kept", storedMoney}});
        }
        else
        {
            character.Money = claimed;
        }

        // [MONEY] boundary 3 of 4: what the server received, against what it already had.
        //
        // Logged unconditionally, not only on disagreement. The existing warning below fires
        // when the figures differ, which is the interesting case once you know money moves at
        // all - and right now we do not know that. A line that prints when the numbers AGREE
        // is what distinguishes "money never changes" from "money changes and is overwritten",
        // and those need opposite fixes.
        spdlog::info("[MONEY] 3 received: client says {}, server had {}, delta {}",
                     character.Money, storedMoney, character.Money - storedMoney);

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

    // The lifepath starter kit, granted once, at creation, by the server.
    //
    // Placed after everything the client sent has been copied in, because for a brand new
    // character it deliberately OVERRIDES that. What the client reports at this moment is
    // whatever save it happened to load - V's full loadout, cyberware and eddies - and a
    // new character is not supposed to inherit any of it. They get their lifepath's own
    // clothes, one ordinary sidearm, a hundred rounds and 20,000 eddies. Nothing else.
    //
    // Gated on StarterKitGranted rather than on Initialised or SpawnedBefore because those
    // answer different questions. The kit is granted once per CHARACTER: the obvious wrong
    // place for it is the join path, which would hand out a fresh outfit and another
    // 20,000 eddies every time somebody reconnected.
    if (!character.StarterKitGranted)
    {
        const auto lifepath = StarterKit::FromWire(aMessage.get_lifepath());
        const auto kit = StarterKit::For(lifepath);

        if (kit.empty())
        {
            // Only the three the game offers are supported. An unrecognised value grants
            // nothing rather than picking a kit - the wrong lifepath's gear is a silent
            // bug, whereas arriving with nothing is one somebody reports in a minute.
            spdlog::warn("[StarterKit] {} finished the creator with an unsupported lifepath "
                         "({}) - granting no kit, character keeps nothing",
                         pPlayer->Username, aMessage.get_lifepath());
        }
        else
        {
            const int64_t moneyBefore = character.Money;

            /*
             * Built on a CANDIDATE, granted only if all of it succeeds.
             *
             * The old sequence wrote straight into `character`: clear the inventory, push
             * every item, set the money, then set StarterKitGranted = true. Nothing there
             * could fail loudly, but nothing stopped a partial grant either - a malformed
             * kit entry (id 0, quantity 0) would be skipped by the primitives while the
             * flag still went true, and the character would be permanently marked as having
             * received a kit they only got part of. StarterKitGranted is a one-way gate, so
             * that is not recoverable by retrying.
             *
             * Now: fill a copy, and only adopt it if every item and the money landed. On
             * failure the character keeps what it had and the flag stays FALSE, so the grant
             * can be attempted again once the kit definition is fixed.
             *
             * Through Economy for the same reason everything else is: one place that knows
             * what a valid item and a valid balance are.
             */
            CharacterRecord granted = character;
            granted.Inventory.clear();

            bool kitApplied = true;
            std::string kitFailure;

            for (const auto& item : kit)
            {
                const auto added = Economy::AddItem(granted, item.Id, item.Quantity);

                if (added != Economy::Result::Success)
                {
                    kitApplied = false;
                    kitFailure = fmt::format("item {:#x} x{}: {}", item.Id, item.Quantity,
                                             Economy::Describe(added));
                    break;
                }
            }

            if (kitApplied)
            {
                // From zero rather than added to whatever was there - a starter kit sets the
                // opening balance, it does not top somebody up.
                granted.Money = 0;

                const auto paid = Economy::Credit(granted, StarterKit::kStartingMoney);

                if (paid != Economy::Result::Success)
                {
                    kitApplied = false;
                    kitFailure = fmt::format("starting money: {}", Economy::Describe(paid));
                }
            }

            if (!kitApplied)
            {
                spdlog::error("[StarterKit] {} - kit NOT granted ({}). The character keeps "
                              "what it had and StarterKitGranted stays false, so this can be "
                              "retried once the kit is fixed.",
                              pPlayer->Username, kitFailure);
            }
            else
            {
                character = granted;
                character.Lifepath = StarterKit::ToString(lifepath);
                character.StarterKitGranted = true;

            spdlog::info("[StarterKit] {} - character '{}', lifepath {}, {} eddies",
                         pPlayer->Username, character.Name, character.Lifepath,
                         character.Money);

            for (const auto& item : kit)
                spdlog::info("[StarterKit]   {} x{} (0x{:016X})", item.Name, item.Quantity, item.Id);

            spdlog::info("[StarterKit]   starterKitGranted=true");

            // Hand it to the client NOW, not on their next join.
            //
            // The spawn response has already gone out by this point - it carried whatever
            // the retired character owned, which the client correctly discarded. So without
            // this the kit sits in the record, the player spends their first session naked
            // and broke, and ninety seconds later the possessions autosave captures that
            // empty inventory and writes it back over the kit. That is exactly what
            // happened to Cam on 27 August: granted at 15:01:27, overwritten at 15:02:57.
            //
            // The client still has its restore pending at this moment - it waits for the
            // world to settle before applying anything - so this lands in time to be part
            // of that same restore rather than a second, competing one.
            // Built as a vector and set in one go - the generator here is netpack, not
            // protobuf, so there is no add_inventory() to append with. Same shape as
            // Level.cpp's spawn response, deliberately.
            Vector<server::ItemStack> stacks;
            stacks.reserve(character.Inventory.size());

            for (const auto& item : character.Inventory)
            {
                server::ItemStack entry;
                entry.set_id(item.Id);
                entry.set_quantity(item.Quantity);
                stacks.push_back(entry);
            }

            server::NotifyPossessions possessions;
            possessions.set_inventory(stacks);
            possessions.set_money(character.Money);
            possessions.set_reason("starter kit");

            GServer->Send(pPlayer->Connection, possessions);

            // Recorded separately from the ordinary save audit above, which compared the
            // client's numbers. This is the server handing money out, and it should read
            // that way in the log rather than looking like a player's balance changing.
            auto& audit = GServer->GetAuditLog();
            audit.RecordMoney(pPlayer->DiscordId, pPlayer->DiscordId, "starterkit.grant",
                              moneyBefore, character.Money);
            }   // kitApplied
        }
    }

    store.SaveCharacter(pPlayer->DiscordId, pPlayer->Username, character);

    // Correct everyone who is already looking at this player.
    //
    // Appearance used to reach other clients at spawn and never again, so a save that
    // landed afterwards changed the stored record and nothing on screen. Two visible
    // consequences: a ripperdoc visit was invisible until everyone watching rejoined, and
    // a player whose spawn went out before their customization state was populated stayed
    // FACELESS for the rest of the session - the blob was fixed in storage within seconds
    // and no one was ever told.
    //
    // The live puppet is updated from the same bytes that were just validated and stored,
    // so the thing on screen and the thing in the record cannot drift apart.
    if (pPlayer->Puppet && pPlayer->Puppet.is_alive())
    {
        if (auto* pAppearance = pPlayer->Puppet.get_mut<AppearanceComponent>())
        {
            pAppearance->ccstate.assign(blob.begin(), blob.end());
            pPlayer->Puppet.modified<AppearanceComponent>();

            if (auto* pLevel = m_pWorld->get_mut<Level>())
                pLevel->BroadcastAppearance(pPlayer->Puppet);
        }
    }

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

/**
 * What THIS character's phone shows for a number.
 *
 * Three answers in priority order, and the order is the whole point:
 *
 *  1. The name this character saved. A phone book is a private annotation, so somebody
 *     saved as "Ripper - Watson" stays that even after the person behind it renames.
 *  2. Whoever currently holds the number, when nothing was saved. An entry added by
 *     number should say who it reaches rather than showing a bare string.
 *  3. The number itself, for a number nobody holds.
 *
 * Takes the VIEWER's character rather than an account, because the saved name belongs to
 * one character. Their other character has a different phone book and must get a
 * different answer from this function.
 */
static std::string PhoneBookName(const CharacterRecord* apViewer, const std::string& acNumber)
{
    if (apViewer)
    {
        if (const auto* pContact = PlayerStore::FindContact(*apViewer, acNumber))
        {
            if (!pContact->Name.empty())
                return pContact->Name;
        }
    }

    std::string ownerId;
    const auto* pOwner = GServer->GetPlayerStore().FindCharacterByPhoneNumber(acNumber, &ownerId);

    if (pOwner)
        return DisplayNameFor(ownerId, pOwner, acNumber);

    return acNumber;
}

/**
 * The same, for a character named by id rather than by number.
 *
 * The inbox stores CharacterIds - that is what makes a thread survive a rename and what
 * keeps two characters on one account apart - but a player thinks in numbers and names.
 * This is the one place that translation happens.
 */
static std::string PhoneBookNameForCharacter(const CharacterRecord* apViewer,
                                             const std::string& acCharacterId,
                                             std::string* apNumber = nullptr)
{
    std::string ownerId;
    const auto* pOther = GServer->GetPlayerStore().FindCharacterById(acCharacterId, &ownerId);

    if (!pOther)
    {
        // A retired or deleted character. The thread is still theirs and still readable -
        // deleting the person does not un-say what was said - so it is labelled rather
        // than hidden.
        if (apNumber)
            apNumber->clear();

        return "(unknown)";
    }

    if (apNumber)
        *apNumber = pOther->PhoneNumber;

    // The viewer's own saved name wins here too, for the same reason as above.
    if (apViewer && !pOther->PhoneNumber.empty())
    {
        if (const auto* pContact = PlayerStore::FindContact(*apViewer, pOther->PhoneNumber))
        {
            if (!pContact->Name.empty())
                return pContact->Name;
        }
    }

    return DisplayNameFor(ownerId, pOther, pOther->PhoneNumber);
}

/**
 * How long ago, in words. "3m", "2h", "4d".
 *
 * A phone shows relative time because that is the question being asked - "is this recent"
 * - and an absolute timestamp makes the reader do arithmetic to answer it. Kept crude on
 * purpose: nobody needs seconds, and the widest unit that is still true is the most
 * readable one.
 */
static std::string Ago(int64_t aWhen)
{
    if (aWhen <= 0)
        return "?";

    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    const auto seconds = now - aWhen;

    if (seconds < 60)
        return "now";
    if (seconds < 3600)
        return fmt::format("{}m", seconds / 60);
    if (seconds < 86400)
        return fmt::format("{}h", seconds / 3600);

    return fmt::format("{}d", seconds / 86400);
}

void ChatSystem::DeliverPendingMessages(const PlayerComponent& acPlayer)
{
    auto& store = GServer->GetPlayerStore();

    const auto* pCharacter = store.FindCharacter(acPlayer.DiscordId);

    // No character means nothing is addressed to them yet. A message is sent to a
    // CharacterId, and somebody who has not made one does not have an inbox to fill.
    if (!pCharacter || pCharacter->CharacterId.empty())
        return;

    auto& messages = GServer->GetMessages();

    const auto pending = messages.Undelivered(pCharacter->CharacterId);

    if (pending.empty())
        return;

    Tell(acPlayer, fmt::format("--- {} unread message(s) ---", pending.size()));

    for (const auto& message : pending)
    {
        std::string number;
        const auto who = PhoneBookNameForCharacter(pCharacter, message.FromCharacterId, &number);

        Tell(acPlayer, fmt::format("  {} {}: {}", Ago(message.SentAt),
                                   number.empty() ? who : fmt::format("{} ({})", who, number),
                                   message.Body));
    }

    Tell(acPlayer, "Reply with /text <number> <message>.");

    // Marked only after every line has been handed to the connection. A crash partway
    // through leaves them undelivered and they arrive again next time, which is the right
    // way round: showing a message twice is a blemish, losing one is the bug.
    messages.MarkDelivered(pCharacter->CharacterId);
}

flecs::entity ChatSystem::FindByActiveCharacter(const std::string& acCharacterId) const
{
    flecs::entity found{};

    if (acCharacterId.empty())
        return found;

    m_pWorld->each(
        [&](flecs::entity aEntity, const PlayerComponent& aPlayer)
        {
            if (found)
                return;

            // Resolved through the ACTIVE character, not through FindCharacterById.
            //
            // FindCharacterById would find the record wherever it sits, including on an
            // account whose player is currently playing a different character - and
            // ringing that connection would put one character's call on another
            // character's screen. The question here is not "does this character exist", it
            // is "is somebody holding this phone right now".
            const auto* pActive = GServer->GetPlayerStore().FindCharacter(aPlayer.DiscordId);

            if (pActive && pActive->CharacterId == acCharacterId)
                found = aEntity;
        });

    return found;
}

void ChatSystem::AnnounceCall(const CallSession& acSession, CallState aState)
{
    const auto caller = FindByActiveCharacter(acSession.CallerCharacterId);
    const auto target = FindByActiveCharacter(acSession.TargetCharacterId);

    const auto* pCallerPlayer = caller ? caller.get<PlayerComponent>() : nullptr;
    const auto* pTargetPlayer = target ? target.get<PlayerComponent>() : nullptr;

    auto& store = GServer->GetPlayerStore();

    // Each side is named the way the OTHER side's phone book has them, so a saved name
    // wins over whatever the person currently calls themselves.
    const auto* pCallerCharacter =
        pCallerPlayer ? store.FindCharacter(pCallerPlayer->DiscordId) : nullptr;
    const auto* pTargetCharacter =
        pTargetPlayer ? store.FindCharacter(pTargetPlayer->DiscordId) : nullptr;

    const auto callerAsSeenByTarget = PhoneBookName(pTargetCharacter, acSession.CallerNumber);
    const auto targetAsSeenByCaller = PhoneBookName(pCallerCharacter, acSession.TargetNumber);

    /**
     * The PHONE first, chat second.
     *
     * Both sides get a NotifyCall carrying the finished display name - resolved here,
     * through the RECIPIENT's own phone book, because the saved name lives in the
     * character record and the client has no copy of it. Two people must be able to see
     * different names for the same number, so the resolution cannot be done once and
     * shared.
     *
     * The chat lines below are kept as a FALLBACK, not as the interface. They are what a
     * player sees if the phone UI fails to present - which is worth having, because a call
     * that rings somewhere the player cannot see is indistinguishable from the server
     * being broken.
     */
    const auto notify = [&](const PlayerComponent& acPlayer, const std::string& acName,
                            const std::string& acNumber, bool aIncoming)
    {
        server::NotifyCall message;
        message.set_call_id(acSession.CallId.c_str());
        message.set_state(static_cast<uint32_t>(aState));
        message.set_display_name(acName.c_str());
        message.set_number(acNumber.c_str());
        message.set_incoming(aIncoming);

        GServer->Send(acPlayer.Connection, message);
    };

    if (pCallerPlayer)
        notify(*pCallerPlayer, targetAsSeenByCaller, acSession.TargetNumber, false);

    if (pTargetPlayer)
        notify(*pTargetPlayer, callerAsSeenByTarget, acSession.CallerNumber, true);

    const auto tellCaller = [&](const std::string& acLine)
    {
        if (pCallerPlayer)
            Tell(*pCallerPlayer, acLine);
    };

    const auto tellTarget = [&](const std::string& acLine)
    {
        if (pTargetPlayer)
            Tell(*pTargetPlayer, acLine);
    };

    switch (aState)
    {
    case CallState::Ringing:
        tellCaller(fmt::format("Calling {} ({})... /hangup to give up.", targetAsSeenByCaller,
                               acSession.TargetNumber));
        tellTarget(fmt::format("### Incoming call - {} ({}) ###", callerAsSeenByTarget,
                               acSession.CallerNumber));
        tellTarget("/answer to pick up, /decline to send them away.");
        break;

    case CallState::Connected:
        // Said on both sides, because the useful half is not "connected" - it is that
        // voice now reaches somebody who is not standing next to you, which is otherwise
        // impossible on this server and is the entire point of a call.
        tellCaller(fmt::format("Connected to {}. They hear you wherever you are. /hangup to end.",
                               targetAsSeenByCaller));
        tellTarget(fmt::format("Connected to {}. They hear you wherever you are. /hangup to end.",
                               callerAsSeenByTarget));
        break;

    case CallState::Declined:
        tellCaller(fmt::format("{} declined.", targetAsSeenByCaller));
        tellTarget("Declined.");
        break;

    case CallState::Missed:
        tellCaller(fmt::format("No answer from {}.", targetAsSeenByCaller));
        tellTarget(fmt::format("Missed call - {} ({}).", callerAsSeenByTarget,
                               acSession.CallerNumber));
        break;

    case CallState::Busy:
        tellCaller(fmt::format("{} is on another call.", targetAsSeenByCaller));
        break;

    case CallState::Cancelled:
        tellCaller("Hung up.");
        tellTarget(fmt::format("{} hung up before you answered.", callerAsSeenByTarget));
        break;

    case CallState::Ended:
    {
        const auto seconds = acSession.ConnectedAt > 0
                                 ? std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                           .count() -
                                       acSession.ConnectedAt
                                 : 0;

        const auto line = fmt::format("Call ended ({}s).", seconds);

        tellCaller(line);
        tellTarget(line);
        break;
    }

    case CallState::Failed:
        tellCaller("The call failed.");
        tellTarget("The call failed.");
        break;

    default:
        break;
    }
}

/**
 * Both sides of a trade, as the server currently understands it.
 *
 * Printed on every change, to BOTH people, because a trade is the one place where the two
 * players must be looking at the same thing before they agree. A private view of a shared
 * deal is how somebody confirms something they never saw.
 */
void ChatSystem::ShowTrade(const TradeSession& acSession)
{
    auto& store = GServer->GetPlayerStore();

    const auto describe = [&](const std::string& acCharacterId, const TradeOffer& acOffer)
    {
        std::string line = fmt::format("{} eddies", acOffer.Money);

        for (const auto& stack : acOffer.Items)
            line += fmt::format(", {}x item {:x}", stack.Quantity, stack.Id);

        std::string who = "(gone)";

        if (const auto* pCharacter = store.FindCharacterById(acCharacterId))
            who = pCharacter->Name.empty() ? acCharacterId : pCharacter->Name;

        return fmt::format("  {}{}: {}", who, acOffer.Confirmed ? " [CONFIRMED]" : "", line);
    };

    for (const auto& id : {acSession.A, acSession.B})
    {
        const auto entity = FindByActiveCharacter(id);
        if (!entity)
            continue;

        const auto* pPlayer = entity.get<PlayerComponent>();
        if (!pPlayer)
            continue;

        Tell(*pPlayer, "--- trade ---");
        Tell(*pPlayer, describe(acSession.A, acSession.OfferA));
        Tell(*pPlayer, describe(acSession.B, acSession.OfferB));

        if (!acSession.BothConfirmed())
            Tell(*pPlayer, "/trade confirm when you are happy, /trade cancel to walk away.");
    }
}

void ChatSystem::EndTradeFor(const std::string& acCharacterId, TradeState aState,
                             const std::string& acWhy)
{
    auto& trades = GServer->GetTrades();

    auto* pSession = trades.EndFor(acCharacterId, aState);

    if (!pSession)
        return;

    // The other side is told, and told why. A trade window that simply stops responding
    // reads as the server breaking rather than as the other person leaving.
    const auto other = pSession->Other(acCharacterId);

    if (const auto entity = FindByActiveCharacter(other))
    {
        if (const auto* pPlayer = entity.get<PlayerComponent>())
            Tell(*pPlayer, fmt::format("Trade cancelled - {}.", acWhy));
    }

    trades.Sweep();
}

void ChatSystem::TickTrades()
{
    auto& trades = GServer->GetTrades();

    const auto expired = trades.Expired();

    for (auto* pSession : expired)
    {
        trades.End(*pSession, TradeState::Expired);

        for (const auto& id : {pSession->A, pSession->B})
        {
            if (const auto entity = FindByActiveCharacter(id))
            {
                if (const auto* pPlayer = entity.get<PlayerComponent>())
                    Tell(*pPlayer, "Trade expired.");
            }
        }
    }

    if (!expired.empty())
        trades.Sweep();

    /**
     * Distance, checked continuously rather than only at the start.
     *
     * A trade agreed face to face and completed from across the district is the shape every
     * remote scam takes. Checked here rather than at confirm alone so that walking away
     * ENDS it visibly, instead of leaving somebody holding a window that will refuse them
     * at the last moment for a reason they cannot see.
     */
    std::vector<std::pair<std::string, std::string>> tooFar;

    for (auto* pSession : trades.Live())
    {
        if (pSession->State != TradeState::Open)
            continue;

        const auto left = FindByActiveCharacter(pSession->A);
        const auto right = FindByActiveCharacter(pSession->B);

        if (!left || !right)
            continue;   // a disconnect is handled where it happens

        const auto* pLeftPlayer = left.get<PlayerComponent>();
        const auto* pRightPlayer = right.get<PlayerComponent>();

        if (!pLeftPlayer || !pRightPlayer || !pLeftPlayer->Puppet || !pRightPlayer->Puppet)
            continue;

        const auto* pLeftMove = pLeftPlayer->Puppet.get<MovementComponent>();
        const auto* pRightMove = pRightPlayer->Puppet.get<MovementComponent>();

        if (!pLeftMove || !pRightMove)
            continue;

        if (glm::distance(pLeftMove->Position, pRightMove->Position) > kTradeDistance * 3.f)
            tooFar.emplace_back(pSession->A, "you moved too far apart");
    }

    for (const auto& [id, why] : tooFar)
        EndTradeFor(id, TradeState::Cancelled, why);
}

void ChatSystem::TickMedical()
{
    const auto now = MedicalNow();

    auto* pLevel = m_pWorld->get_mut<Level>();

    // Gathered first, applied after. The loop below sends messages and changes state, and
    // mutating while iterating a flecs query is how a system starts behaving differently
    // depending on how many people are on the server.
    std::vector<flecs::entity> died;
    std::vector<std::pair<flecs::entity, bool>> finished;   // puppet, wasRevive

    m_pWorld->each(
        [&](flecs::entity aPuppet, HealthComponent& aHealth)
        {
            // A procedure that has run its course, whoever it was on.
            if (aHealth.TreatmentEndsAt > 0 && now >= aHealth.TreatmentEndsAt)
            {
                const bool wasRevive = aHealth.LifeState == LifeState::kReviving;
                finished.emplace_back(aPuppet, wasRevive);
                return;
            }

            if (aHealth.LifeState != LifeState::kDowned)
                return;

            // Stabilised patients do not bleed out. That is what the treatment IS - see
            // Medical.h on why this is not modelled as extra time.
            if (aHealth.Stabilized)
                return;

            if (aHealth.DownedAt > 0 && now - aHealth.DownedAt >= kBleedoutSeconds)
                died.push_back(aPuppet);
        });

    for (const auto& [puppet, wasRevive] : finished)
    {
        auto* pHealth = puppet.get_mut<HealthComponent>();
        if (!pHealth)
            continue;

        pHealth->TreatmentEndsAt = 0;
        pHealth->TreatedBy.clear();

        if (wasRevive)
        {
            pHealth->LifeState = LifeState::kAlive;
            pHealth->Health = kRevivedHealth;
            pHealth->DownedAt = 0;
            pHealth->Stabilized = false;

            spdlog::info("[MEDICAL] entity {:x} revived at {:.0f}%", puppet.id(), kRevivedHealth);
        }
        else
        {
            pHealth->Stabilized = true;
            spdlog::info("[MEDICAL] entity {:x} stabilised", puppet.id());
        }

        if (pLevel)
            pLevel->BroadcastCombatState(puppet);

        if (const auto owner = puppet.parent())
        {
            if (const auto* pPlayer = owner.get<PlayerComponent>())
            {
                Tell(*pPlayer, wasRevive ? "You are back on your feet - barely. Find a ripperdoc."
                                         : "You have been stabilised. You are still down.");
            }
        }
    }

    for (const auto& puppet : died)
    {
        auto* pHealth = puppet.get_mut<HealthComponent>();
        if (!pHealth)
            continue;

        pHealth->LifeState = LifeState::kDead;
        pHealth->TreatedBy.clear();
        pHealth->TreatmentEndsAt = 0;

        spdlog::info("[MEDICAL] entity {:x} bled out after {}s", puppet.id(), kBleedoutSeconds);

        if (pLevel)
            pLevel->BroadcastCombatState(puppet);

        if (const auto owner = puppet.parent())
        {
            if (const auto* pPlayer = owner.get<PlayerComponent>())
            {
                Tell(*pPlayer, "You bled out. Nobody reached you in time.");
                Tell(*pPlayer, "Use /respawn when you are ready.");

                // A trade does not survive its owner dying - the brief's §36, and the same
                // reasoning as a disconnect: assets must not move on behalf of somebody who
                // is no longer in a position to agree.
                if (const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(pPlayer->DiscordId))
                    EndTradeFor(pCharacter->CharacterId, TradeState::Cancelled, "they went down");
            }
        }
    }
}

void ChatSystem::BeginCall(const PlayerComponent& acPlayer, const std::string& acNumber)
{
    if (!IsPhoneNumberShaped(acNumber))
    {
        Tell(acPlayer, fmt::format("'{}' is not a number. They look like 555-014-372.", acNumber));
        return;
    }

    auto& store = GServer->GetPlayerStore();
    auto& calls = GServer->GetCalls();

    // THE CALLER COMES FROM THE CONNECTION, never from the request. Neither the phone nor
    // the chat command carries a "who is calling" field, so ringing somebody as a third
    // party cannot be spelled - the same rule the voice path uses.
    const auto* pSelf = store.FindCharacter(acPlayer.DiscordId);

    if (!pSelf || pSelf->CharacterId.empty())
    {
        Tell(acPlayer, "You have no character yet.");
        return;
    }

    if (pSelf->PhoneNumber.empty())
    {
        Tell(acPlayer, "You have no number yet - it is assigned on your next save.");
        return;
    }

    if (calls.Active(pSelf->CharacterId))
    {
        Tell(acPlayer, "You are already on a call.");
        return;
    }

    std::string recipientId;
    const auto* pTarget = store.FindCharacterByPhoneNumber(acNumber, &recipientId);

    if (!pTarget)
    {
        Tell(acPlayer, fmt::format("Nobody has the number {}.", acNumber));
        return;
    }

    if (pTarget->CharacterId == pSelf->CharacterId)
    {
        Tell(acPlayer, "That is your own number.");
        return;
    }

    // You blocked them: said plainly, because you already know.
    if (PlayerStore::IsBlockedBy(*pSelf, acNumber))
    {
        Tell(acPlayer, fmt::format("You have blocked {}.", acNumber));
        return;
    }

    /**
     * Offline and "they blocked you" answer identically, on purpose.
     *
     * A refusal that differs is a refusal that tells somebody they were blocked, which is
     * exactly the thing a block exists to withhold. Combined into one branch rather than
     * two branches with the same text, so a later edit cannot make one of them specific
     * without noticing it is doing so.
     */
    const auto targetEntity = FindByActiveCharacter(pTarget->CharacterId);

    if (!targetEntity || PlayerStore::IsBlockedBy(*pTarget, pSelf->PhoneNumber))
    {
        Tell(acPlayer, fmt::format("{} is not answering.", PhoneBookName(pSelf, acNumber)));
        return;
    }

    if (calls.Active(pTarget->CharacterId))
    {
        auto& session = calls.Begin(pSelf->CharacterId, pSelf->PhoneNumber,
                                    pTarget->CharacterId, acNumber);

        // Recorded as a real call so both histories show the attempt. A busy signal that
        // leaves no trace is indistinguishable from never having called.
        calls.End(session, CallState::Busy);
        AnnounceCall(session, CallState::Busy);
        calls.Sweep();
        return;
    }

    auto& session = calls.Begin(pSelf->CharacterId, pSelf->PhoneNumber,
                                pTarget->CharacterId, acNumber);

    AnnounceCall(session, CallState::Ringing);

    spdlog::info("{} is calling {}", acPlayer.Username, acNumber);
}

void ChatSystem::ControlCall(const PlayerComponent& acPlayer, const std::string& acCallId,
                             uint32_t aAction)
{
    auto& store = GServer->GetPlayerStore();
    auto& calls = GServer->GetCalls();

    const auto* pSelf = store.FindCharacter(acPlayer.DiscordId);

    if (!pSelf || pSelf->CharacterId.empty())
    {
        Tell(acPlayer, "You have no character yet.");
        return;
    }

    /**
     * Resolved from the CONNECTION's own active call, never looked up by the id sent.
     *
     * The difference is the whole security story. A call id used as a lookup key is a way
     * to hang up somebody else's call; a call id CHECKED AGAINST your own is only a way to
     * notice that the call you were looking at has already been replaced.
     */
    auto* pSession = calls.Active(pSelf->CharacterId);

    if (!pSession)
    {
        Tell(acPlayer, "You are not on a call.");
        return;
    }

    // An empty id means "whatever I am in" - the chat fallback, which has no id to send.
    // The phone always sends the one it is displaying, so a button pressed a moment too
    // late acts on nothing instead of on the call that replaced it.
    if (!acCallId.empty() && acCallId != pSession->CallId)
        return;

    const bool isTarget = pSession->TargetCharacterId == pSelf->CharacterId;

    switch (aAction)
    {
    case 0:   // answer
    {
        if (!isTarget || pSession->State != CallState::Ringing)
            return;

        // The caller may have walked away between the ring and the answer. Connecting
        // somebody to nobody leaves a call that only the timeout can clear.
        if (!FindByActiveCharacter(pSession->CallerCharacterId))
        {
            calls.End(*pSession, CallState::Failed);
            AnnounceCall(*pSession, CallState::Failed);
            calls.Sweep();
            return;
        }

        pSession->State = CallState::Connected;
        pSession->ConnectedAt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        AnnounceCall(*pSession, CallState::Connected);

        spdlog::info("{} answered a call from {}", acPlayer.Username, pSession->CallerNumber);
        return;
    }

    case 1:   // decline
    {
        if (!isTarget || pSession->State != CallState::Ringing)
            return;

        calls.End(*pSession, CallState::Declined);
        AnnounceCall(*pSession, CallState::Declined);
        calls.Sweep();
        return;
    }

    case 2:   // hang up
    {
        // Cancelling a call that never connected and ending one that did are different
        // history lines, so they are different states rather than one "ended".
        const auto state =
            pSession->State == CallState::Connected ? CallState::Ended : CallState::Cancelled;

        calls.End(*pSession, state);
        AnnounceCall(*pSession, state);
        calls.Sweep();
        return;
    }

    default:
        // Refused rather than defaulted. An unknown action quietly becoming "hang up"
        // would drop calls for no visible reason on any client that got the numbering
        // wrong, and the symptom would look like a network fault.
        spdlog::warn("{} sent call action {}, which is not one", acPlayer.Username, aAction);
        return;
    }
}

void ChatSystem::HandleCallRequest(const PacketEvent<client::CallRequest>& aMessage)
{
    auto* pPlayerManager = m_pWorld->get<PlayerManager>();

    const auto entity = pPlayerManager->GetByConnectionId(aMessage.ConnectionId);
    if (!entity || !entity.has<PlayerComponent>())
        return;

    BeginCall(*entity.get<PlayerComponent>(), aMessage.get_number().c_str());
}

void ChatSystem::HandleCallControlRequest(const PacketEvent<client::CallControlRequest>& aMessage)
{
    auto* pPlayerManager = m_pWorld->get<PlayerManager>();

    const auto entity = pPlayerManager->GetByConnectionId(aMessage.ConnectionId);
    if (!entity || !entity.has<PlayerComponent>())
        return;

    ControlCall(*entity.get<PlayerComponent>(), aMessage.get_call_id().c_str(),
                aMessage.get_action());
}

void ChatSystem::EndCallFor(const std::string& acCharacterId, CallState aState)
{
    auto& calls = GServer->GetCalls();

    auto* pSession = calls.Active(acCharacterId);

    if (!pSession)
        return;

    // The OTHER side is told, and told the truth. A call that simply goes quiet leaves
    // somebody talking to a phone that will never do anything again, and the natural
    // reading of that is that the server broke rather than that the other person left.
    const auto other = pSession->Other(acCharacterId);

    calls.End(*pSession, aState);

    if (const auto entity = FindByActiveCharacter(other))
    {
        if (const auto* pPlayer = entity.get<PlayerComponent>())
        {
            Tell(*pPlayer, aState == CallState::Ended ? "Call ended - they hung up."
                                                      : "Call ended - they are gone.");
        }
    }

    calls.Sweep();
}

void ChatSystem::TickCalls()
{
    auto& calls = GServer->GetCalls();

    const auto expired = calls.Expired();

    if (expired.empty())
        return;

    for (auto* pSession : expired)
    {
        // Ended BEFORE the announcement, so the history line exists by the time either
        // player could type /calls and look for it.
        calls.End(*pSession, CallState::Missed);
        AnnounceCall(*pSession, CallState::Missed);
    }

    calls.Sweep();
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

    /*
     * Through the economy mutator, for the same reason /pay is.
     *
     * The seller's credit was unguarded: a seller near the ceiling wrapped, or ended on a
     * balance the save path already refuses. Transfer checks the seller's headroom BEFORE
     * the buyer is charged, so the failure is "no sale" rather than "charged, and the money
     * went nowhere".
     */
    if (const auto moved = Economy::Transfer(buyer, seller, acSale.Price);
        moved != Economy::Result::Success)
    {
        vehicles.Unlock(acSale.VehicleId, acSale.Token);

        spdlog::warn("[MONEY] refused a vehicle sale of {}: {}", acSale.Price,
                     Economy::Describe(moved));

        Tell(acBuyer, fmt::format("That sale could not complete - {}. You have not been charged.",
                                  Economy::Describe(moved)));
        return;
    }

    store.SaveCharacter(acSale.BuyerId, acBuyer.Username, buyer);
    store.SaveCharacter(acSale.SellerId, seller.Name, seller);

    if (!vehicles.Transfer(acSale.VehicleId, acSale.BuyerId, acSale.Token))
    {
        // Put the money back. A transfer that fails here is a bug rather than a refusal -
        // the lock is ours and ownership was checked - but leaving somebody charged for a
        // car they did not receive is not something to risk on that reasoning.
        // The refund goes back through the same boundary, in the other direction. Hand-
        // written it was the one arithmetic left that could put a balance somewhere the
        // save path would refuse - and a refund that cannot land is the worst possible
        // moment to find that out.
        const auto refunded = Economy::Transfer(seller, buyer, acSale.Price);

        if (refunded != Economy::Result::Success)
        {
            // Should be unreachable: the money moved this way a moment ago, so the room
            // exists. Logged at error rather than assumed away, because if it ever does
            // happen somebody has been charged for a car they did not get.
            spdlog::error("[MONEY] vehicle {} refund FAILED ({}) - buyer {} may be charged "
                          "for a car they did not receive",
                          acSale.VehicleId, Economy::Describe(refunded), acSale.BuyerId);
        }

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
        flecs::entity byCharacterPrefix{};

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

                // People type the FIRST NAME.
                //
                // Exact matching alone meant "/revive aldi" could not find "Aldi Do" -
                // live, 2026-09-04: zeldfep tried "aldi do", then "aldi", then gave up
                // and used the Discord name, which is the one name nobody in the world
                // is called. A prefix match on the character's own name is what a person
                // reading a nameplate will type. Ranked BELOW every exact match, so an
                // exact name can never be stolen by somebody else's prefix.
                if (!pCharacter->Name.empty() && acQuery.size() >= 2 &&
                    pCharacter->Name.size() >= acQuery.size() &&
                    equalsInsensitive(pCharacter->Name.substr(0, acQuery.size()), acQuery))
                {
                    if (!byCharacterPrefix) byCharacterPrefix = aEntity;
                }
            });

        if (byCharacterId)     return byCharacterId;
        if (byDiscordId)       return byDiscordId;
        if (byCharacterName)   return byCharacterName;
        if (byUsername)        return byUsername;

        return byCharacterPrefix;
    };

    // The name to SAY to people: the one its owner picked, not their Discord handle.
    //
    // zeldfep, live 2026-09-04: "revive feature should not be discord name it should be
    // player picked name." In the world nobody is called by their Discord account - the
    // nameplate over their head is their character's name, so a message about them that
    // uses anything else is asking the reader to translate. Falls back to the account
    // name only when a character has not been named yet, because an empty name in a
    // sentence is worse than the wrong one. Server LOGS keep the Discord name - there
    // the account is the point, and it is what a ban or a role check acts on.
    const auto sayName = [](const PlayerComponent& acPlayer) -> std::string
    {
        const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(acPlayer.DiscordId);
        if (pCharacter && !pCharacter->Name.empty())
            return pCharacter->Name;

        return acPlayer.Username;
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
        /*
         * /tp spawn - sends the caller to the spawn point.
         *
         * Cam, 2026-09-04: "we should make it where we can also /tp to spawn", asked while
         * he was stuck under the map.
         *
         * STAFF-GATED, like every other form of /tp. It shipped open for one build on my
         * reading that an unstuck player beats a clean rule, and Cam closed it: "i want
         * /tp spawn staff-gated". On an RP server a free ride back to a known location is
         * an escape from anything - a fight, a chase, a robbery - and that is worth more
         * than the convenience.
         *
         * Players who fall out of the world are covered without this: the client-side
         * fall-through guard re-places them automatically, and the server now refuses to
         * hand out an origin start point at all. This is the manual backstop for when both
         * of those miss, which is a staff job.
         */
        if (target == "spawn")
        {
            if (!acSender.HasAtLeast(EPermissionLevel::kEventStaff))
                return deny(EPermissionLevel::kEventStaff);

            glm::vec3 position;
            float yaw = 0.f;

            if (!GServer->GetRespawnPoint(position, yaw))
            {
                Tell(acSender, "No spawn point is set. Ask an admin to run /setspawn.");
                return true;
            }

            // Where they were, so /return still works on somebody who unstuck themselves.
            if (const auto* pMine = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr)
            {
                auto* pSelf = aSender.get_mut<PlayerComponent>();
                pSelf->ReturnPosition = pMine->Position;
                pSelf->ReturnRotation = pMine->Rotation;
                pSelf->HasReturnPoint = true;
            }

            server::NotifyTeleport teleport;
            common::Vector3 destination;
            destination.set_x(position.x);
            destination.set_y(position.y);
            destination.set_z(position.z);
            teleport.set_position(destination);
            teleport.set_rotation(yaw);

            GServer->Send(acSender.Connection, teleport);

            spdlog::info("{} teleported themselves to spawn", acSender.Username);

            Tell(acSender, "Sent you to the spawn point.");
            return true;
        }

        if (!acSender.HasAtLeast(EPermissionLevel::kEventStaff))
            return deny(EPermissionLevel::kEventStaff);

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

        if (!pVictim->Puppet)
        {
            Tell(acSender, fmt::format("{} has not spawned in yet.", pVictim->Username));
            return true;
        }

        auto* pHealth = pVictim->Puppet.get_mut<HealthComponent>();
        if (!pHealth)
        {
            Tell(acSender, fmt::format("{} has no health state on the server yet.", pVictim->Username));
            return true;
        }

        if (pHealth->LifeState != LifeState::kAlive)
        {
            Tell(acSender, fmt::format("{} is already down.", pVictim->Username));
            return true;
        }

        // Downed WHERE THEY STAND, not teleported to the respawn point.
        //
        // Cam, 2026-09-04: "/kill doesnt actually down or flatline the player." It did not,
        // and the old comment here explained why it deliberately did not - a FLATLINED
        // screen ends the session, which is the thing the medical system exists to replace.
        // That reasoning was right about death and wrong about downing: the medical system
        // has modelled a downed state, a bleedout timer and a revive since it was built, and
        // /kill was the one route into it nobody had wired up. So this now enters that state
        // rather than inventing a death - the player collapses, medics can reach them, and
        // the bleedout decides what happens if nobody does.
        //
        // The teleport is gone with it. Moving a downed body to the respawn point would skip
        // the whole medical loop, which is the part worth having.
        pHealth->Health = 0.f;
        pHealth->LifeState = LifeState::kDowned;
        pHealth->DownedAt = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        pHealth->Stabilized = false;
        pHealth->TreatedBy.clear();
        pHealth->TreatmentEndsAt = 0;

        // What actually makes it visible. Without this the state lives only on the server
        // and the player keeps walking around - which is exactly the bug being fixed.
        if (auto* pLevel = m_pWorld->get_mut<Level>())
            pLevel->BroadcastCombatState(pVictim->Puppet);

        spdlog::info("{} downed {} ({})", acSender.Username, pVictim->Username, rest);

        Tell(*pVictim, fmt::format("You were put down by {}{}", acSender.Username,
                                   rest.empty() ? "." : (" - " + rest)));
        Tell(*pVictim, fmt::format("A medic can revive you. You have {} seconds.", kBleedoutSeconds));

        Broadcast("SERVER", fmt::format("{} was put down by {}{}", pVictim->Username, acSender.Username,
                                        rest.empty() ? "" : (" - " + rest)).c_str());
        return true;
    }

    // ------------------------------------------------------------ /respawn ----
    //
    // Back on your feet at the respawn point, after bleeding out.
    //
    // BUILT BECAUSE IT WAS ALREADY BEING ADVERTISED. The bleedout path has told players
    // "Use /respawn when you are ready" since the medical system landed, and the command it
    // names was never written - so anybody who actually bled out was told to type something
    // that did nothing, and stayed dead with no route back. Nobody hit it because /kill was
    // the only way to go down and /kill never downed anyone, so the two gaps hid each other.
    //
    // Not staff-gated: this is how a dead player rejoins the world.
    if (command == "/respawn")
    {
        if (!acSender.Puppet)
        {
            Tell(acSender, "You are not in the world yet.");
            return true;
        }

        auto* pHealth = acSender.Puppet.get_mut<HealthComponent>();
        if (!pHealth)
        {
            Tell(acSender, "No health state on the server for you yet.");
            return true;
        }

        // Only from DEAD. Respawning out of a downed state would make every medic pointless
        // and turn the bleedout into a 180-second inconvenience nobody would ever wait out.
        if (pHealth->LifeState != LifeState::kDead)
        {
            if (pHealth->LifeState == LifeState::kAlive)
                Tell(acSender, "You are not down.");
            else
                Tell(acSender, "You are down, not dead - a medic can still reach you. Wait it out.");

            return true;
        }

        glm::vec3 position;
        float yaw = 0.f;

        if (!GServer->GetRespawnPoint(position, yaw))
        {
            Tell(acSender, "No respawn point is set. Ask an admin to run /setspawn.");
            return true;
        }

        // Jail wins, same as an ordinary death. Dying should not be a way out of a cell.
        if (const auto* pRecord = GServer->GetPlayerStore().Find(acSender.DiscordId))
        {
            if (pRecord->JailedUntil > 0)
                position = {pRecord->JailX, pRecord->JailY, pRecord->JailZ};
        }

        pHealth->Health = kRevivedHealth;
        pHealth->LifeState = LifeState::kAlive;
        pHealth->DownedAt = 0;
        pHealth->Stabilized = false;
        pHealth->TreatedBy.clear();
        pHealth->TreatmentEndsAt = 0;

        server::NotifyTeleport teleport;
        common::Vector3 destination;
        destination.set_x(position.x);
        destination.set_y(position.y);
        destination.set_z(position.z);
        teleport.set_position(destination);
        teleport.set_rotation(yaw);

        GServer->Send(acSender.Connection, teleport);

        // State after the move. The client stands them up on this, and doing it second means
        // they are never briefly upright at the place they died.
        if (auto* pLevel = m_pWorld->get_mut<Level>())
            pLevel->BroadcastCombatState(acSender.Puppet);

        spdlog::info("{} respawned at ({:.1f}, {:.1f}, {:.1f})", acSender.Username, position.x, position.y,
                     position.z);

        Tell(acSender, fmt::format("You are back on your feet at {:.0f}% health.", kRevivedHealth));
        return true;
    }

    // ---------------------------------------------------------- /inventory ----
    //
    // What the SERVER believes you are carrying, and the ids /trade item wants.
    //
    // BUILT BECAUSE IT WAS ALREADY BEING ADVERTISED, same as /respawn. The trade error for a
    // malformed item said "Usage: /trade item <id> <quantity>   (see /inventory)" and there
    // was no /inventory - so the one message whose whole job was to tell somebody where to
    // find an item id pointed at nothing. Found by the new Verify check, not by a person.
    //
    // Ids, not names. The server stores raw TweakDBIDs and deliberately does not interpret
    // them (see CharacterRecord::ItemStack) - and the client-side helper that turns one into
    // a name returns empty strings on 2.31, which is how equipment sync once spent a week
    // looking fine and shipping nothing. A number you can paste into /trade is worth more
    // than a name that might be blank.
    if (command == "/inventory")
    {
        const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(acSender.DiscordId);

        if (!pCharacter)
        {
            Tell(acSender, "No character loaded for you on the server yet.");
            return true;
        }

        Tell(acSender, fmt::format("Eddies: {}", pCharacter->Money));

        if (pCharacter->Inventory.empty())
        {
            Tell(acSender, "Carrying nothing the server has recorded yet.");
            return true;
        }

        Tell(acSender, fmt::format("Carrying {} stack(s) - the id is what /trade item takes:",
                                   pCharacter->Inventory.size()));

        // Capped. A full character carries hundreds of stacks and the chat box shows a
        // handful of lines, so an uncapped list scrolls its own beginning away - the same
        // reason /help became topic-based.
        constexpr size_t kMaxLines = 20;
        size_t shown = 0;

        for (const auto& stack : pCharacter->Inventory)
        {
            if (shown >= kMaxLines)
            {
                Tell(acSender, fmt::format("...and {} more.", pCharacter->Inventory.size() - shown));
                break;
            }

            Tell(acSender, fmt::format("  {:#018x}  x{}", stack.Id, stack.Quantity));
            ++shown;
        }

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
    /**
     * /rename <character-id> <new name> - repair a character's name. Admins and above.
     *
     * This exists because the mod broke people's names. The server asks for a character
     * name right after spawn, and until 96da4bf the chat box was being drawn off in the
     * corner of the screen - so players typed blind into a field they could not see, and a
     * name is chosen ONCE. Cam's own character came out as "JulianJulian Vale", doubled
     * because he could not see what he had already entered. The once-only lock that makes
     * names meaningful is exactly what makes a typo permanent, so there has to be a way to
     * fix one.
     *
     * Keyed on the CHARACTER id, not the Discord account. Cam: "just in case they rename
     * their discord account." A display name is not an identifier.
     *
     * Deliberately does NOT clear NameChosen. This repairs a name; it does not hand the
     * player a fresh naming attempt, which would turn an admin tool into a way to rename
     * yourself repeatedly by asking.
     */
    if (command == "/rename")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        const auto idStart = acLine.find(' ');
        std::string rest = (idStart == std::string::npos) ? std::string{} : acLine.substr(idStart + 1);

        while (!rest.empty() && rest.front() == ' ')
            rest.erase(rest.begin());

        const auto split = rest.find(' ');

        if (split == std::string::npos)
        {
            Tell(acSender, "Usage: /rename <character-id> <new name>");
            Tell(acSender, "The character id is shown by /whois - it does not change when somebody renames their Discord.");
            return true;
        }

        const std::string characterId = rest.substr(0, split);
        std::string wanted = rest.substr(split + 1);

        while (!wanted.empty() && wanted.front() == ' ')
            wanted.erase(wanted.begin());

        while (!wanted.empty() && wanted.back() == ' ')
            wanted.pop_back();

        if (wanted.empty())
        {
            Tell(acSender, "Usage: /rename <character-id> <new name>");
            return true;
        }

        // Same guards the player-facing /name applies. An admin should not be able to put a
        // name on somebody else's character that the owner could not have chosen.
        if (wanted.front() == '/')
        {
            Tell(acSender, "That looks like a command, not a name.");
            return true;
        }

        if (wanted.size() > 32)
            wanted.resize(32);

        auto& store = GServer->GetPlayerStore();

        std::string ownerDiscordId;
        std::string ownerUsername;
        const auto* pCharacter = store.FindCharacterById(characterId, &ownerDiscordId, &ownerUsername);

        if (!pCharacter)
        {
            Tell(acSender, fmt::format("No character with id '{}'.", characterId));
            return true;
        }

        const std::string previous = pCharacter->Name.empty() ? std::string("unnamed") : pCharacter->Name;

        // Copy the stored record and change only the name. SaveCharacter replaces the record
        // wholesale with what it is given, so building one from scratch here would silently
        // reset every field this command does not mention - which is how SpawnedBefore got
        // wiped by face edits once already.
        CharacterRecord updated = *pCharacter;
        updated.Name = wanted;

        store.SaveCharacter(ownerDiscordId, ownerUsername, updated);

        spdlog::info("{} renamed character {} ('{}') to '{}' - owner {}", acSender.Username, characterId,
                     previous, wanted, ownerUsername);

        Tell(acSender, fmt::format("Character {} renamed: '{}' -> '{}'.", characterId, previous, wanted));

        // Tell the owner, if they are online. Somebody else changing your character's name
        // without a word is worse than the typo it fixed.
        //
        // The world is walked rather than indexed - PlayerManager has no lookup by Discord
        // id, and this is the same walk the vehicle-sale notification does for the same
        // reason. A handful of players is a walk of a handful.
        m_pWorld->each(
            [this, &ownerDiscordId, &wanted](flecs::entity, const PlayerComponent& acOther)
            {
                if (acOther.DiscordId != ownerDiscordId)
                    return;

                Tell(acOther, fmt::format("An admin corrected your character's name to '{}'.", wanted));
            });

        return true;
    }

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
    // The vehicle half of /call is folded into the phone-call command further down, which
    // is the only dispatch for it now.
    //
    // THIS BLOCK USED TO RETURN HERE, and it made player-to-player calling completely
    // unreachable: it matched first, printed the vehicle hint, and returned true, so
    // "/call 555-014-372" answered "use your phone" and rang nobody. Two features wanted
    // the same verb and the older one silently won.

    // ---------------------------------------------------------- /givecar ----
    //
    // Admin: create an owned vehicle. Stands in for a dealership until there is one.
    if (command == "/givecar")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kEventStaff))
            return deny(EPermissionLevel::kEventStaff);

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
        // Same warning as /npc (the helper rule): the record persists and syncs to
        // everyone forever - a mod-only vehicle record is invisible to anyone without
        // that mod, and the server cannot tell the difference.
        Tell(acSender, "Base-game records only: a modded record will not render for players without that mod.");
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
            Tell(acSender, "Usage: /addcontact 555-014-372 [name]   (ask them for their number)");
            return true;
        }

        if (!IsPhoneNumberShaped(target))
        {
            // Separated from "nobody has that number" deliberately. The two failures read
            // identically to a player and have completely different fixes.
            Tell(acSender, fmt::format("'{}' is not a number. They look like 555-014-372.", target));
            return true;
        }

        // Everything after the number is the name they want it saved under, spaces and
        // all. Optional: adding by number alone still works and falls back to whoever
        // holds it, which is what every existing contact does.
        std::string savedName;
        {
            const auto numberAt = acLine.find(target);

            if (numberAt != std::string::npos)
            {
                savedName = acLine.substr(numberAt + target.size());

                while (!savedName.empty() && savedName.front() == ' ')
                    savedName.erase(savedName.begin());
                while (!savedName.empty() && savedName.back() == ' ')
                    savedName.pop_back();
            }
        }

        std::string ownerId;
        const auto* pOwner = GServer->GetPlayerStore().FindCharacterByPhoneNumber(target, &ownerId);

        if (!pOwner)
        {
            Tell(acSender, fmt::format("Nobody has the number {}.", target));
            return true;
        }

        // Compared by CHARACTER, not by account.
        //
        // This used to compare Discord ids, which was right while an account had one
        // character and became wrong the moment slots existed: a player's second character
        // would be told their first character's number was "your own number" and refused.
        // Those are two different people who happen to share an owner, and one saving the
        // other's number is an ordinary thing to do.
        const auto* pSelf = GServer->GetPlayerStore().FindCharacter(acSender.DiscordId);

        if (pSelf && !pSelf->PhoneNumber.empty() && pSelf->PhoneNumber == target)
        {
            Tell(acSender, "That is your own number.");
            return true;
        }

        if (!GServer->GetPlayerStore().AddContact(acSender.DiscordId, target, savedName))
        {
            Tell(acSender, fmt::format("{} is already in your contacts. "
                                       "Rename it with /contactname {} <name>.", target, target));
            return true;
        }

        // Only the person who added them is told. Nobody else's phone gains an entry, and
        // the owner is not notified either - looking somebody up is not an event that should
        // announce itself to them.
        Tell(acSender, fmt::format("Added {} - {}.",
                                   savedName.empty() ? DisplayNameFor(ownerId, pOwner, target)
                                                     : savedName,
                                   target));
        return true;
    }

    // -------------------------------------------------------- /contactname ---
    //
    // Renaming an entry, kept separate from adding one. An upsert would silently create a
    // contact when somebody meant to rename one, and a phone book that grows entries by
    // typo is worse than one that says "you do not have that number".
    if (command == "/contactname")
    {
        const auto restStart = acLine.find(' ');
        std::string rest = (restStart == std::string::npos) ? std::string{} : acLine.substr(restStart + 1);

        while (!rest.empty() && rest.front() == ' ')
            rest.erase(rest.begin());

        std::string number = rest;
        std::string name;

        if (const auto space = rest.find(' '); space != std::string::npos)
        {
            number = rest.substr(0, space);
            name = rest.substr(space + 1);

            while (!name.empty() && name.front() == ' ')
                name.erase(name.begin());
        }

        if (number.empty())
        {
            Tell(acSender, "Usage: /contactname 555-014-372 <name>   (empty name clears it)");
            return true;
        }

        if (!GServer->GetPlayerStore().SetContactName(acSender.DiscordId, number, name))
        {
            Tell(acSender, fmt::format("{} is not in your contacts.", number));
            return true;
        }

        if (name.empty())
            Tell(acSender, fmt::format("Cleared the name on {}.", number));
        else
            Tell(acSender, fmt::format("Saved {} as {}.", number, name));

        return true;
    }

    // -------------------------------------------------------- /delcontact ---
    if (command == "/delcontact")
    {
        if (target.empty())
        {
            Tell(acSender, "Usage: /delcontact 555-014-372");
            return true;
        }

        if (!GServer->GetPlayerStore().RemoveContact(acSender.DiscordId, target))
        {
            Tell(acSender, fmt::format("{} is not in your contacts.", target));
            return true;
        }

        // Said out loud, because it is the surprising half. Forgetting somebody's name is
        // not the same as un-saying what passed between you, and a player who expects a
        // delete to erase the thread should find out here rather than from the thread.
        Tell(acSender, fmt::format("Removed {}. Your messages with them are kept.", target));
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

        /**
         * Read from the SERVER's record, never from anything the client claimed. This is
         * the entire reason money became server-owned: a transfer must not be talked into
         * existence by the machine that benefits from it.
         *
         * AGAINST WHAT IS AVAILABLE, NOT WHAT IS OWNED. Money promised in a live trade is
         * not spendable here, or the reservation is decorative: somebody with 10,000 who
         * has offered 8,000 across a trade window could otherwise send 8,000 by phone and
         * have both succeed, and the 16,000 would come from nowhere. That is the exact
         * duplication the trade system exists to prevent, and it would arrive through this
         * command rather than through trading.
         */
        const auto reserved = GServer->GetTrades().ReservedMoney(pSenderCharacter->CharacterId);
        const auto available = PlayerStore::AvailableMoney(*pSenderCharacter, reserved);

        if (available < amount)
        {
            if (reserved > 0)
            {
                Tell(acSender, fmt::format("You have {} eddies available - {} is promised in a "
                                           "trade.", available, reserved));
            }
            else
            {
                Tell(acSender, fmt::format("You have {} eddies and tried to send {}.",
                                           pSenderCharacter->Money, amount));
            }

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

        /*
         * Through the economy mutator rather than by hand.
         *
         * The arithmetic here was `sender.Money -= amount; recipient.Money += amount;`, and
         * the second half was unguarded: a recipient near the int64 ceiling wrapped, and a
         * transfer that pushed anybody past the plausible-balance ceiling produced a balance
         * the SAVE path already refuses to accept - legal here, impossible there.
         *
         * Transfer checks the recipient's headroom BEFORE the payer is touched, because
         * money that leaves one side and cannot arrive at the other has been destroyed and
         * no error code gives it back.
         */
        const auto moved = Economy::Transfer(sender, recipient, amount);

        if (moved != Economy::Result::Success)
        {
            spdlog::warn("[MONEY] refused a transfer of {} from {}: {}", amount,
                         acSender.Username, Economy::Describe(moved));

            Tell(acSender, fmt::format("That transfer could not be made - {}.",
                                       Economy::Describe(moved)));
            return true;
        }

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

        for (const auto& contact : pCharacter->Contacts)
        {
            // A number whose owner has retired still shows, with the truth next to it.
            // Silently dropping it would look like the contact was never added.
            const auto* pResolved =
                GServer->GetPlayerStore().FindCharacterByPhoneNumber(contact.Number);

            const auto blocked = PlayerStore::IsBlockedBy(*pCharacter, contact.Number);

            Tell(acSender, fmt::format("  {}  {}{}{}", contact.Number,
                                       PhoneBookName(pCharacter, contact.Number),
                                       pResolved ? "" : "  (no longer in service)",
                                       blocked ? "  [blocked]" : ""));
        }

        return true;
    }

    // ------------------------------------------------------------- /text ---
    //
    // Sending a message. Written against CharacterIds rather than accounts, so a player's
    // second character has its own inbox and cannot see the first one's - see MessageStore.h.
    if (command == "/text")
    {
        const auto restStart = acLine.find(' ');
        std::string rest = (restStart == std::string::npos) ? std::string{} : acLine.substr(restStart + 1);

        while (!rest.empty() && rest.front() == ' ')
            rest.erase(rest.begin());

        std::string number = rest;
        std::string body;

        if (const auto space = rest.find(' '); space != std::string::npos)
        {
            number = rest.substr(0, space);
            body = rest.substr(space + 1);

            while (!body.empty() && body.front() == ' ')
                body.erase(body.begin());
        }

        if (number.empty() || body.empty())
        {
            Tell(acSender, "Usage: /text 555-014-372 <message>");
            return true;
        }

        if (!IsPhoneNumberShaped(number))
        {
            Tell(acSender, fmt::format("'{}' is not a number. They look like 555-014-372.", number));
            return true;
        }

        auto& store = GServer->GetPlayerStore();

        const auto* pSelf = store.FindCharacter(acSender.DiscordId);

        if (!pSelf || pSelf->CharacterId.empty())
        {
            Tell(acSender, "You have no character yet.");
            return true;
        }

        if (pSelf->PhoneNumber.empty())
        {
            Tell(acSender, "You have no number yet - it is assigned on your next save.");
            return true;
        }

        std::string recipientId;
        const auto* pRecipient = store.FindCharacterByPhoneNumber(number, &recipientId);

        if (!pRecipient)
        {
            Tell(acSender, fmt::format("Nobody has the number {}.", number));
            return true;
        }

        // Compared by number, so a player CAN text their own other character. They are two
        // people; the only thing that must be refused is a thread with itself.
        if (pRecipient->CharacterId == pSelf->CharacterId)
        {
            Tell(acSender, "That is your own number.");
            return true;
        }

        /**
         * Blocked, and told nothing.
         *
         * Accepted and dropped rather than refused. A refusal is a signal - somebody who
         * gets "delivery failed" for one number and "sent" for another has learned they
         * were blocked, and on a roleplay server that is information a block exists
         * precisely to withhold.
         *
         * Nothing is stored either. Storing it and never delivering would look identical
         * today and dump the whole backlog on whoever later unblocks them, which is the
         * opposite of what the block was for.
         *
         * Read off the recipient's own record rather than re-resolved from their account:
         * a block belongs to a CHARACTER, and asking the account again could answer with
         * whichever of their characters is currently active instead of the one being
         * texted.
         */
        if (PlayerStore::IsBlockedBy(*pRecipient, pSelf->PhoneNumber))
        {
            Tell(acSender, fmt::format("Sent to {}.", PhoneBookName(pSelf, number)));

            spdlog::info("{} texted {} - dropped, blocked", acSender.Username, number);
            return true;
        }

        std::string reason;
        const auto messageId = GServer->GetMessages().Send(pSelf->CharacterId,
                                                           pRecipient->CharacterId, body, &reason);

        if (messageId.empty())
        {
            // Stable codes turned into sentences HERE, at the one surface that has a
            // player in front of it. The store answers in codes so a phone app can answer
            // differently without the store having opinions about wording.
            if (reason == "too_long")
                Tell(acSender, fmt::format("Too long - {} characters at most.", kMessageBodyLimit));
            else if (reason == "store_unreadable")
                Tell(acSender, "Messages are unavailable right now. Staff have been told.");
            else
                Tell(acSender, "That message could not be sent.");

            return true;
        }

        Tell(acSender, fmt::format("To {}: {}", PhoneBookName(pSelf, number), body));

        // Delivered immediately if they are here, so a conversation between two people who
        // are both online reads like a conversation rather than like mail.
        //
        // Only when the character they are PLAYING is the one that was texted. Somebody
        // logged in as their other character is, for this purpose, offline - and the
        // message stays undelivered until they switch back, which is exactly right.
        const auto recipientEntity = findPlayer(recipientId);

        if (recipientEntity)
        {
            if (const auto* pRecipientPlayer = recipientEntity.get<PlayerComponent>())
            {
                const auto* pActive = store.FindCharacter(recipientId);

                if (pActive && pActive->CharacterId == pRecipient->CharacterId)
                {
                    Tell(*pRecipientPlayer,
                         fmt::format("Text from {} ({}): {}",
                                     PhoneBookName(pActive, pSelf->PhoneNumber),
                                     pSelf->PhoneNumber, body));

                    GServer->GetMessages().MarkDelivered(pRecipient->CharacterId);
                }
            }
        }

        spdlog::info("{} texted {} ({} chars)", acSender.Username, number, body.size());
        return true;
    }

    // ------------------------------------------------------------ /texts ---
    //
    // The inbox: who this character has threads with, most recent first.
    if (command == "/texts")
    {
        const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(acSender.DiscordId);

        if (!pCharacter || pCharacter->CharacterId.empty())
        {
            Tell(acSender, "You have no character yet.");
            return true;
        }

        const auto inbox = GServer->GetMessages().Inbox(pCharacter->CharacterId);

        if (inbox.empty())
        {
            Tell(acSender, "No messages. Send one with /text <number> <message>.");
            return true;
        }

        Tell(acSender, fmt::format("{} conversation(s):", inbox.size()));

        for (const auto& thread : inbox)
        {
            std::string number;
            const auto who = PhoneBookNameForCharacter(pCharacter, thread.OtherCharacterId, &number);

            // The last line, truncated. Enough to recognise the thread, not so much that
            // the list becomes the thread.
            auto preview = thread.LastBody;
            if (preview.size() > 40)
                preview = preview.substr(0, 37) + "...";

            Tell(acSender, fmt::format("  {}{}  {}  {}{}",
                                       thread.Unread ? fmt::format("({}) ", thread.Unread) : "",
                                       number.empty() ? who : fmt::format("{} {}", who, number),
                                       Ago(thread.LastMessageAt),
                                       thread.LastWasMine ? "you: " : "",
                                       preview));
        }

        Tell(acSender, "Read one with /read <number>.");
        return true;
    }

    // ------------------------------------------------------------- /read ---
    if (command == "/read")
    {
        if (target.empty())
        {
            Tell(acSender, "Usage: /read 555-014-372");
            return true;
        }

        auto& store = GServer->GetPlayerStore();

        const auto* pCharacter = store.FindCharacter(acSender.DiscordId);

        if (!pCharacter || pCharacter->CharacterId.empty())
        {
            Tell(acSender, "You have no character yet.");
            return true;
        }

        const auto* pOther = store.FindCharacterByPhoneNumber(target, nullptr);

        if (!pOther)
        {
            Tell(acSender, fmt::format("Nobody has the number {}.", target));
            return true;
        }

        auto& messages = GServer->GetMessages();

        const auto thread = messages.Thread(pCharacter->CharacterId, pOther->CharacterId);

        if (thread.empty())
        {
            Tell(acSender, fmt::format("Nothing between you and {}.",
                                       PhoneBookName(pCharacter, target)));
            return true;
        }

        Tell(acSender, fmt::format("--- {} ({}) ---", PhoneBookName(pCharacter, target), target));

        for (const auto& message : thread)
        {
            const bool mine = message.SenderCharacterId == pCharacter->CharacterId;

            Tell(acSender, fmt::format("  {} {}: {}", Ago(message.SentAt),
                                       mine ? "you" : PhoneBookName(pCharacter, target),
                                       message.Body));
        }

        // Reading a thread is what makes it read. Only this character's - MarkDelivered
        // takes a CharacterId, so their other character's unread messages are untouched.
        messages.MarkDelivered(pCharacter->CharacterId);
        return true;
    }

    // ------------------------------------------------------------ /block ---
    //
    // Per character, silent, and aimed at a number. See CharacterRecord::Blocked.
    if (command == "/block" || command == "/unblock")
    {
        const bool blocking = command == "/block";

        if (target.empty())
        {
            Tell(acSender, fmt::format("Usage: {} 555-014-372", command));
            return true;
        }

        if (!IsPhoneNumberShaped(target))
        {
            Tell(acSender, fmt::format("'{}' is not a number. They look like 555-014-372.", target));
            return true;
        }

        auto& store = GServer->GetPlayerStore();

        const auto* pSelf = store.FindCharacter(acSender.DiscordId);

        if (pSelf && pSelf->PhoneNumber == target)
        {
            Tell(acSender, "That is your own number.");
            return true;
        }

        const bool changed = blocking ? store.Block(acSender.DiscordId, target)
                                      : store.Unblock(acSender.DiscordId, target);

        if (!changed)
        {
            Tell(acSender, blocking ? fmt::format("{} is already blocked.", target)
                                    : fmt::format("{} is not blocked.", target));
            return true;
        }

        // Nobody but the blocker is told, ever. The other side finds out by not finding
        // out, which is the point.
        Tell(acSender, blocking
                           ? fmt::format("Blocked {}. They are not told.", target)
                           : fmt::format("Unblocked {}.", target));

        return true;
    }

    // --------------------------------------------------- the chat FALLBACK ---
    //
    // Calls belong in the phone app. These are kept so that a player whose phone UI fails
    // to present still has a way to answer - a call ringing somewhere they cannot see is
    // indistinguishable from the server being broken.
    //
    // Deliberately thin. Every rule lives in BeginCall/ControlCall, which the phone also
    // uses, so there is no second copy for one surface to drift away from.
    if (command == "/call")
    {
        /*
         * ONE /call, TWO THINGS PEOPLE MEAN BY IT.
         *
         * A number rings a person. No number is almost always somebody reaching for the
         * vehicle summon, which this command used to be - so that hint lives here rather
         * than in a second dispatch. It had its own block earlier in this function, which
         * matched first and returned, and made player calling unreachable.
         */
        if (target.empty())
        {
            Tell(acSender, "Usage: /call 555-014-372   (or answer from the phone)");
            Tell(acSender, "Calling a CAR? Use your phone - your vehicles are in the vehicle "
                           "menu, like normal.");
            Tell(acSender, "/garage shows the paperwork: which specific car is which, and its plate.");
            return true;
        }

        BeginCall(acSender, target);
        return true;
    }

    if (command == "/answer" || command == "/decline" || command == "/hangup")
    {
        const uint32_t action = command == "/answer" ? 0u : (command == "/decline" ? 1u : 2u);

        // No call id: the fallback acts on whatever the player is in. The phone always
        // sends the id it is showing, which is what makes a stale button press safe there.
        ControlCall(acSender, {}, action);
        return true;
    }

    // ------------------------------------------------------------ /calls ---
    if (command == "/calls")
    {
        const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(acSender.DiscordId);

        if (!pCharacter || pCharacter->CharacterId.empty())
        {
            Tell(acSender, "You have no character yet.");
            return true;
        }

        // THIS character's history. Their other character's calls are a different list
        // filed under a different id, and nothing here can reach them.
        const auto history = GServer->GetCalls().History(pCharacter->CharacterId);

        if (history.empty())
        {
            Tell(acSender, "No calls yet. Ring somebody with /call <number>.");
            return true;
        }

        Tell(acSender, fmt::format("{} recent call(s):", history.size()));

        for (const auto& entry : history)
        {
            Tell(acSender, fmt::format("  {} {} {} {}{}", Ago(entry.StartedAt),
                                       entry.Direction == "out" ? "->" : "<-",
                                       PhoneBookName(pCharacter, entry.OtherNumber),
                                       entry.Result,
                                       entry.Duration > 0 ? fmt::format(" ({}s)", entry.Duration)
                                                          : std::string{}));
        }

        return true;
    }

    if (command == "/blocked")
    {
        const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(acSender.DiscordId);

        if (!pCharacter || pCharacter->Blocked.empty())
        {
            Tell(acSender, "Nobody blocked.");
            return true;
        }

        Tell(acSender, fmt::format("{} blocked:", pCharacter->Blocked.size()));

        for (const auto& number : pCharacter->Blocked)
            Tell(acSender, fmt::format("  {}", number));

        return true;
    }

    // --------------------------------------- /assess /stabilize /revive ---
    //
    // Medical. Everything is checked against the SERVER's health state - a client cannot
    // declare itself downed, cannot declare itself revived, and cannot decide that a
    // procedure finished. See Medical.h.
    if (command == "/assess" || command == "/stabilize" || command == "/stabilise" ||
        command == "/revive")
    {
        if (!acSender.HasAtLeast(kTreatPermission))
            return deny(kTreatPermission);

        if (target.empty())
        {
            Tell(acSender, fmt::format("Usage: {} <player>", command));
            return true;
        }

        const auto patient = findPlayer(target);

        if (!patient)
        {
            Tell(acSender, fmt::format("No player called '{}' - try their character name, or the first part of it.", target));
            return true;
        }

        const auto* pPatientPlayer = patient.get<PlayerComponent>();

        if (!pPatientPlayer || !pPatientPlayer->Puppet || !pPatientPlayer->Puppet.is_alive())
        {
            Tell(acSender, "They are not in the world.");
            return true;
        }

        auto* pHealth = pPatientPlayer->Puppet.get_mut<HealthComponent>();

        if (!pHealth)
        {
            Tell(acSender, "You cannot read their condition.");
            return true;
        }

        // Hands-on. Checked against server positions, never a distance the client claims.
        const auto* pMedicMove = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr;
        const auto* pPatientMove = pPatientPlayer->Puppet.get<MovementComponent>();

        if (!pMedicMove || !pPatientMove ||
            glm::distance(pMedicMove->Position, pPatientMove->Position) > kTreatmentDistance)
        {
            Tell(acSender, "You need to be next to them.");
            return true;
        }

        const auto now = MedicalNow();

        // ---- assess ---------------------------------------------------------------
        if (command == "/assess")
        {
            const char* condition = "uninjured";

            switch (pHealth->LifeState)
            {
            case LifeState::kDowned: condition = pHealth->Stabilized ? "STABLE, still down"
                                                                     : "CRITICAL, bleeding out"; break;
            case LifeState::kDead: condition = "DEAD"; break;
            case LifeState::kReviving: condition = "being revived"; break;
            default: condition = pHealth->Health < pHealth->MaxHealth ? "injured" : "uninjured"; break;
            }

            Tell(acSender, fmt::format("--- {} ---", sayName(*pPatientPlayer)));
            Tell(acSender, fmt::format("  condition: {}", condition));
            Tell(acSender, fmt::format("  health:    {:.0f}%", pHealth->Health));

            if (pHealth->LifeState == LifeState::kDowned && !pHealth->Stabilized &&
                pHealth->DownedAt > 0)
            {
                const auto left = kBleedoutSeconds - (now - pHealth->DownedAt);
                Tell(acSender, fmt::format("  time left: {}s", left > 0 ? left : 0));
            }

            if (!pHealth->TreatedBy.empty())
                Tell(acSender, "  somebody is already treating them.");

            return true;
        }

        // ---- both procedures share their preconditions -----------------------------
        if (pHealth->LifeState == LifeState::kDead)
        {
            Tell(acSender, "They are gone. Nothing you do here will help.");
            return true;
        }

        if (pHealth->LifeState != LifeState::kDowned && pHealth->LifeState != LifeState::kReviving)
        {
            Tell(acSender, "They are on their feet.");
            return true;
        }

        // One medic per patient. Two procedures on one body and neither can say whose
        // finished - the brief's §27, and the reason TreatedBy exists at all.
        if (!pHealth->TreatedBy.empty() && pHealth->TreatmentEndsAt > now)
        {
            Tell(acSender, "Somebody is already working on them.");
            return true;
        }

        const auto* pSelf = GServer->GetPlayerStore().FindCharacter(acSender.DiscordId);
        const auto medicId = pSelf ? pSelf->CharacterId : acSender.DiscordId;

        if (command == "/revive")
        {
            // Stabilise first. It is the procedure that stops them dying, and allowing a
            // revive straight from critical would make stabilisation pointless - which
            // would in turn make the bleedout timer the only thing that ever mattered.
            if (!pHealth->Stabilized)
            {
                Tell(acSender, "They are not stable enough. /stabilize first.");
                return true;
            }

            pHealth->LifeState = LifeState::kReviving;
            pHealth->TreatedBy = medicId;
            pHealth->TreatmentEndsAt = now + kReviveSeconds;

            Tell(acSender, fmt::format("Reviving {} - {}s.", sayName(*pPatientPlayer), kReviveSeconds));
            Tell(*pPatientPlayer, "Somebody is working on you. Hold on.");

            spdlog::info("[MEDICAL] {} began reviving {}", acSender.Username, pPatientPlayer->Username);
        }
        else
        {
            if (pHealth->Stabilized)
            {
                Tell(acSender, "They are already stable. /revive when you are ready.");
                return true;
            }

            pHealth->TreatedBy = medicId;
            pHealth->TreatmentEndsAt = now + kStabilizeSeconds;

            Tell(acSender, fmt::format("Stabilising {} - {}s.", sayName(*pPatientPlayer),
                                       kStabilizeSeconds));
            Tell(*pPatientPlayer, "Somebody is stabilising you.");

            spdlog::info("[MEDICAL] {} began stabilising {}", acSender.Username,
                         pPatientPlayer->Username);
        }

        if (auto* pLevel = m_pWorld->get_mut<Level>())
            pLevel->BroadcastCombatState(pPatientPlayer->Puppet);

        return true;
    }

    // ------------------------------------------------------------ /trade ---
    if (command == "/trade")
    {
        auto& store = GServer->GetPlayerStore();
        auto& trades = GServer->GetTrades();

        const auto* pSelf = store.FindCharacter(acSender.DiscordId);

        if (!pSelf || pSelf->CharacterId.empty())
        {
            Tell(acSender, "You have no character yet.");
            return true;
        }

        const auto me = pSelf->CharacterId;

        // Sub-commands, parsed off the rest of the line.
        const auto restStart = acLine.find(' ');
        std::string rest = (restStart == std::string::npos) ? std::string{} : acLine.substr(restStart + 1);

        while (!rest.empty() && rest.front() == ' ')
            rest.erase(rest.begin());

        std::string verb = rest;
        std::string arg;

        if (const auto space = rest.find(' '); space != std::string::npos)
        {
            verb = rest.substr(0, space);
            arg = rest.substr(space + 1);
        }

        if (verb.empty())
        {
            Tell(acSender, "Usage: /trade <player>   then  /trade money <n>, /trade item <id> <n>,");
            Tell(acSender, "       /trade confirm, /trade view, /trade cancel");
            return true;
        }

        // ---- cancel: always allowed, except mid-commit -------------------------
        if (verb == "cancel")
        {
            if (!trades.Active(me))
            {
                Tell(acSender, "You are not trading.");
                return true;
            }

            Tell(acSender, "Trade cancelled.");
            EndTradeFor(me, TradeState::Cancelled, "they walked away");
            return true;
        }

        // ---- accept an invitation ----------------------------------------------
        if (verb == "accept")
        {
            auto* pSession = trades.Active(me);

            if (!pSession || pSession->State != TradeState::Requested)
            {
                Tell(acSender, "Nobody has asked to trade with you.");
                return true;
            }

            // Only the person INVITED accepts. The inviter accepting their own invitation
            // would open a trade the other party never agreed to be in.
            if (pSession->B != me)
            {
                Tell(acSender, "You sent that invitation - wait for them.");
                return true;
            }

            pSession->State = TradeState::Open;
            pSession->TouchedAt = TradeStore::Now();

            ShowTrade(*pSession);
            return true;
        }

        auto* pSession = trades.Active(me);

        // ---- view ---------------------------------------------------------------
        if (verb == "view")
        {
            if (!pSession)
            {
                Tell(acSender, "You are not trading.");
                return true;
            }

            ShowTrade(*pSession);
            return true;
        }

        // ---- offer money --------------------------------------------------------
        if (verb == "money")
        {
            if (!pSession || pSession->State != TradeState::Open)
            {
                Tell(acSender, "You are not in an open trade.");
                return true;
            }

            int64_t amount = 0;

            try
            {
                size_t consumed = 0;
                amount = std::stoll(arg, &consumed);

                if (consumed != arg.size())
                    throw std::invalid_argument("trailing");
            }
            catch (...)
            {
                Tell(acSender, fmt::format("'{}' is not an amount.", arg));
                return true;
            }

            if (amount < 0)
            {
                Tell(acSender, "You cannot offer a negative amount.");
                return true;
            }

            /**
             * Checked against what is AVAILABLE, not what is owned.
             *
             * Money already promised in another live trade is not spendable here - though
             * with one trade per character that is currently always zero, which is exactly
             * why the check is written against the reservation rather than against the
             * balance. The day a second concurrent commitment exists, this is already
             * right instead of being a bug nobody remembered to look for.
             */
            const auto reserved = trades.ReservedMoney(me) - pSession->OfferFor(me)->Money;
            const auto available = PlayerStore::AvailableMoney(*pSelf, reserved);

            if (amount > available)
            {
                Tell(acSender, fmt::format("You have {} eddies available.", available));
                return true;
            }

            auto* pOffer = pSession->OfferFor(me);
            pOffer->Money = amount;

            // Both confirmations reset. Somebody who agreed to the old offer has not agreed
            // to this one, and carrying their confirmation across is how a person ends up
            // having accepted a deal they never saw.
            pSession->OfferA.Touch();
            pSession->OfferB.Touch();
            pSession->TouchedAt = TradeStore::Now();

            ShowTrade(*pSession);
            return true;
        }

        // ---- offer an item ------------------------------------------------------
        if (verb == "item")
        {
            if (!pSession || pSession->State != TradeState::Open)
            {
                Tell(acSender, "You are not in an open trade.");
                return true;
            }

            std::string idText = arg;
            std::string qtyText = "1";

            if (const auto space = arg.find(' '); space != std::string::npos)
            {
                idText = arg.substr(0, space);
                qtyText = arg.substr(space + 1);
            }

            uint64_t itemId = 0;
            uint32_t quantity = 0;

            try
            {
                itemId = std::stoull(idText, nullptr, 0);
                quantity = static_cast<uint32_t>(std::stoul(qtyText));
            }
            catch (...)
            {
                Tell(acSender, "Usage: /trade item <id> <quantity>   (see /inventory)");
                return true;
            }

            if (quantity == 0)
            {
                Tell(acSender, "Offer at least one.");
                return true;
            }

            auto* pOffer = pSession->OfferFor(me);

            if (pOffer->Items.size() >= kTradeItemLimit)
            {
                Tell(acSender, "That is as much as one trade can carry.");
                return true;
            }

            // Against what is HELD, minus anything already promised elsewhere. Clamped
            // nowhere: asking for more than you have is refused rather than quietly
            // reduced, because a silently reduced offer is a different deal.
            const auto held = PlayerStore::HeldQuantity(*pSelf, itemId);
            const auto reservedElsewhere =
                trades.ReservedItems(me, itemId) -
                [&]
                {
                    uint32_t mine = 0;
                    for (const auto& stack : pOffer->Items)
                        if (stack.Id == itemId)
                            mine += stack.Quantity;
                    return mine;
                }();

            if (quantity > held || quantity > held - reservedElsewhere)
            {
                Tell(acSender, fmt::format("You have {} of that.", held - reservedElsewhere));
                return true;
            }

            // Replace rather than accumulate, so offering twice sets the amount instead of
            // doubling it - the shape a double-click takes.
            pOffer->Items.erase(std::remove_if(pOffer->Items.begin(), pOffer->Items.end(),
                                               [itemId](const CharacterRecord::ItemStack& acStack)
                                               { return acStack.Id == itemId; }),
                                pOffer->Items.end());

            pOffer->Items.push_back({itemId, quantity});

            pSession->OfferA.Touch();
            pSession->OfferB.Touch();
            pSession->TouchedAt = TradeStore::Now();

            ShowTrade(*pSession);
            return true;
        }

        // ---- confirm, and commit when both have -----------------------------------
        if (verb == "confirm")
        {
            if (!pSession || pSession->State != TradeState::Open)
            {
                Tell(acSender, "You are not in an open trade.");
                return true;
            }

            pSession->OfferFor(me)->Confirmed = true;
            pSession->TouchedAt = TradeStore::Now();

            if (!pSession->BothConfirmed())
            {
                Tell(acSender, "Confirmed. Waiting for them.");
                ShowTrade(*pSession);
                return true;
            }

            /**
             * FINAL VALIDATION, against the records as they are NOW.
             *
             * Not against what was checked when each item was offered. Everything can have
             * changed since: money spent elsewhere, items sold, a character deleted. The
             * brief is explicit that the check at offer time is not the check that matters,
             * and it is right - the only figures worth trusting are the ones read in the
             * same breath as the write.
             */
            const auto* pLeft = store.FindCharacterById(pSession->A);
            const auto* pRight = store.FindCharacterById(pSession->B);

            if (!pLeft || !pRight)
            {
                Tell(acSender, "That character is no longer there.");
                EndTradeFor(me, TradeState::Failed, "the other character is gone");
                return true;
            }

            // Both still here, and still close enough. A trade agreed face to face and
            // completed across the district is the shape every remote scam takes.
            const auto leftEntity = FindByActiveCharacter(pSession->A);
            const auto rightEntity = FindByActiveCharacter(pSession->B);

            if (!leftEntity || !rightEntity)
            {
                Tell(acSender, "They are gone.");
                EndTradeFor(me, TradeState::Failed, "the other player left");
                return true;
            }

            // COMMITTING: from here nothing may cancel it - see TradeStore::EndFor.
            pSession->State = TradeState::Committing;

            PlayerStore::TradeSide left;
            left.CharacterId = pSession->A;
            left.Money = pSession->OfferA.Money;
            left.Items = pSession->OfferA.Items;

            PlayerStore::TradeSide right;
            right.CharacterId = pSession->B;
            right.Money = pSession->OfferB.Money;
            right.Items = pSession->OfferB.Items;

            std::string reason;

            if (!store.ApplyTrade(left, right, &reason))
            {
                // Nothing moved. ApplyTrade validates on copies and assigns only when both
                // directions succeed, so a failure here means the records are exactly as
                // they were.
                spdlog::warn("Trade {} failed: {}", pSession->TradeId, reason);

                trades.End(*pSession, TradeState::Failed);

                for (const auto& id : {pSession->A, pSession->B})
                {
                    if (const auto entity = FindByActiveCharacter(id))
                    {
                        if (const auto* pPlayer = entity.get<PlayerComponent>())
                            Tell(*pPlayer, reason == "insufficient_funds"
                                               ? "Trade failed - somebody could not cover it."
                                               : "Trade failed - nothing was moved.");
                    }
                }

                trades.Sweep();
                return true;
            }

            // The ledger, before either client is told. Both directions as separate lines,
            // so a character's history is a filter on subject alone - the same shape the
            // money transfer ledger uses.
            auto& audit = GServer->GetAuditLog();

            audit.Record("trade.completed", acSender.DiscordId, acSender.DiscordId,
                         {{"trade", pSession->TradeId},
                          {"a", pSession->A},
                          {"b", pSession->B},
                          {"a_money", pSession->OfferA.Money},
                          {"b_money", pSession->OfferB.Money},
                          {"a_items", pSession->OfferA.Items.size()},
                          {"b_items", pSession->OfferB.Items.size()}});

            spdlog::info("Trade {} completed: {} <-> {}", pSession->TradeId, pSession->A,
                         pSession->B);

            trades.End(*pSession, TradeState::Completed);

            // Both clients are corrected from the SERVER's figures, not from what either
            // of them believed. Their next autosave would otherwise report the old balance
            // and undo the exchange - the same race /pay already works around.
            for (const auto& id : {pSession->A, pSession->B})
            {
                const auto entity = FindByActiveCharacter(id);
                if (!entity)
                    continue;

                const auto* pPlayer = entity.get<PlayerComponent>();
                if (!pPlayer)
                    continue;

                if (const auto* pCharacter = store.FindCharacterById(id))
                    PushMoney(*pPlayer, static_cast<int32_t>(pCharacter->Money), "trade");

                Tell(*pPlayer, "Trade complete.");
                Tell(*pPlayer, "Your things are updated - reconnect if anything looks stale.");
            }

            trades.Sweep();
            return true;
        }

        // ---- otherwise: an invitation to a named player ---------------------------
        const auto invitee = findPlayer(verb);

        if (!invitee)
        {
            Tell(acSender, fmt::format("No player called '{}'.", verb));
            return true;
        }

        const auto* pInvitee = invitee.get<PlayerComponent>();

        if (!pInvitee)
        {
            Tell(acSender, "That player is not here.");
            return true;
        }

        const auto* pTheirCharacter = store.FindCharacter(pInvitee->DiscordId);

        if (!pTheirCharacter || pTheirCharacter->CharacterId.empty())
        {
            Tell(acSender, "They have no character.");
            return true;
        }

        if (pTheirCharacter->CharacterId == me)
        {
            Tell(acSender, "You cannot trade with yourself.");
            return true;
        }

        if (trades.Active(me))
        {
            Tell(acSender, "You are already trading. /trade cancel first.");
            return true;
        }

        if (trades.Active(pTheirCharacter->CharacterId))
        {
            Tell(acSender, "They are already trading with somebody.");
            return true;
        }

        // Close enough to be doing this face to face.
        const auto* pMyMove = acSender.Puppet ? acSender.Puppet.get<MovementComponent>() : nullptr;
        const auto* pTheirMove =
            pInvitee->Puppet ? pInvitee->Puppet.get<MovementComponent>() : nullptr;

        if (!pMyMove || !pTheirMove ||
            glm::distance(pMyMove->Position, pTheirMove->Position) > kTradeDistance)
        {
            Tell(acSender, "You need to be standing next to them.");
            return true;
        }

        auto& session = trades.Begin(me, pTheirCharacter->CharacterId);

        Tell(acSender, fmt::format("Asked {} to trade.", pTheirCharacter->Name));
        Tell(*pInvitee, fmt::format("{} wants to trade. /trade accept, or ignore it.",
                                    DisplayNameFor(acSender.DiscordId, pSelf, acSender.Username)));

        spdlog::info("{} asked {} to trade ({})", acSender.Username, pInvitee->Username,
                     session.TradeId);
        return true;
    }

    // -------------------------------------------------------- /vehseats ---
    //
    // Who is sitting where, from the SERVER's own state. Exists because every seat bug so
    // far has started with two people describing what they can see and neither of them
    // being able to see what the server thinks - and the server's view is the one that
    // decides who gets refused.
    if (command == "/vehseats")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
            return deny(EPermissionLevel::kModerator);

        // Vehicle -> its occupants, gathered in one pass. Reported per vehicle rather than
        // per player because "is this car full" is the question being asked.
        std::map<uint64_t, std::vector<std::string>> byVehicle;

        m_pWorld->each(
            [&](flecs::entity aOccupant, const AttachmentComponent& aAttachment)
            {
                std::string who = fmt::format("{:x}", aOccupant.id());

                // Name the person where we can. An id is enough to debug with and useless
                // to talk about.
                if (const auto owner = aOccupant.parent())
                {
                    if (const auto* pOwner = owner.get<PlayerComponent>())
                    {
                        const auto* pCharacter =
                            GServer->GetPlayerStore().FindCharacter(pOwner->DiscordId);

                        who = DisplayNameFor(pOwner->DiscordId, pCharacter, pOwner->Username);
                    }
                }

                byVehicle[aAttachment.Parent].push_back(
                    fmt::format("{} = {}", VehicleSeatName(aAttachment.SlotId), who));
            });

        if (byVehicle.empty())
        {
            Tell(acSender, "Nobody is in a vehicle.");
            return true;
        }

        Tell(acSender, fmt::format("{} occupied vehicle(s):", byVehicle.size()));

        for (const auto& [vehicleId, occupants] : byVehicle)
        {
            const flecs::entity vehicle(m_pWorld->get_world(), vehicleId);

            // The authority holder, because "who is simulating this car" is the other half
            // of every vehicle question and is not visible from the seats alone.
            std::string authority = "parked";

            if (vehicle && vehicle.is_alive())
            {
                if (const auto owner = vehicle.parent())
                {
                    if (const auto* pOwner = owner.get<PlayerComponent>())
                        authority = pOwner->Username;
                }
            }

            Tell(acSender, fmt::format("  vehicle {:x}  ({} occupant(s), simulated by {})",
                                       vehicleId, occupants.size(), authority));

            for (const auto& line : occupants)
                Tell(acSender, fmt::format("    {}", line));
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
        if (!acSender.HasAtLeast(EPermissionLevel::kEventStaff))
            return deny(EPermissionLevel::kEventStaff);

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
        if (!acSender.HasAtLeast(EPermissionLevel::kEventStaff))
            return deny(EPermissionLevel::kEventStaff);

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
        if (!acSender.HasAtLeast(EPermissionLevel::kEventStaff))
            return deny(EPermissionLevel::kEventStaff);

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

        // Catches exactly what produced five ghost NPCs on the test server: the usage
        // line's own placeholder (`Character.<record>`) pasted verbatim instead of a
        // real record name. `<`/`>` never appear in an actual TweakDBID record name, so
        // this is a pure placeholder-paste signature - refuse rather than warn, because
        // the warning already existed below and did not stop it happening once.
        if (record.find('<') != std::string::npos || record.find('>') != std::string::npos)
        {
            Tell(acSender, fmt::format("'{}' looks like the usage line's placeholder, not a real record - "
                                       "nothing declared. Usage: /npc <Character.record> [name]", target));
            return true;
        }

        const std::string name = rest.empty() ? "NPC" : rest;

        pNpcs->Spawn(record, name, pMovement->Position, pMovement->Rotation.z);
        pNpcs->Save();

        spdlog::info("{} declared NPC '{}' ({})", acSender.Username, name, record);
        Tell(acSender, fmt::format("'{}' now exists here for everyone, forever. /npc clear removes all.", name));
        // The helper rule (crew decree 2026-08-22): the record replays to every future
        // joiner, so a record only a MOD provides bakes that mod into the world - it
        // renders for you and stands invisible or broken for everyone else. The server
        // cannot verify records against the game's TweakDB, so the warning rides the
        // reply instead of a refusal.
        Tell(acSender, "Base-game records only: a modded record will not render for players without that mod.");
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
        if (!acSender.HasAtLeast(EPermissionLevel::kEventStaff))
            return deny(EPermissionLevel::kEventStaff);

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
            Tell(acSender, "  /character new confirm  - retire this one and start again");
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

        // Retiring a character is destructive enough to need a second, deliberate step.
        //
        // Live evidence (2026-08-23 01:16): Cam typed /character new again AFTER already
        // spawning as his saved character - it is a habit left over from when every join
        // needed it, and one stray line in chat retired the character he was standing in.
        // Worse than losing it outright, retiring the record underneath a LIVE character
        // does something other than what the command says: the server keeps simulating
        // somebody whose record has gone and the next autosave writes it straight back, so
        // the retire silently half-undoes and the player cannot tell which character they
        // now are. HandleCharacterDelete refuses outright for exactly this reason, but the
        // selector's client half does not exist yet, so chat is still the ONLY way to start
        // a new character - refusing here would leave no way at all. It asks instead.
        if (target == "new")
        {
            const auto* pCharacter = store.FindCharacter(acSender.DiscordId);

            if (!pCharacter)
            {
                Tell(acSender, "You had no character yet, so there was nothing to retire.");
                Tell(acSender, "Change how you look at any ripperdoc - it saves by itself.");
                return true;
            }

            // The confirmation carries the word, not just a bare repeat: typing the same
            // line twice is exactly what a habit does.
            if (rest != "confirm")
            {
                Tell(acSender, fmt::format("This retires {} and starts you over from nothing.",
                                           pCharacter->Name.empty() ? "your character" : pCharacter->Name));

                if (acSender.Puppet && acSender.Puppet.is_alive())
                    Tell(acSender, "You are playing as that character RIGHT NOW - retire it and what you are standing in stops matching your record.");

                Tell(acSender, "If you meant it, type:  /character new confirm");
                Tell(acSender, "If you just want to look different, visit any ripperdoc - that keeps your character.");
                return true;
            }

            if (store.RetireCharacter(acSender.DiscordId))
            {
                Tell(acSender, "Your old character has been retired - it is kept, not deleted.");
                spdlog::info("{} retired their character via /character new confirm", acSender.Username);
            }
            else
            {
                Tell(acSender, "That character could not be retired.");
                return true;
            }

            Tell(acSender, "Change how you look at any ripperdoc - it saves by itself.");
            return true;
        }

        Tell(acSender, "Usage: /character [show | create | new confirm | save <name>]");
        return true;
    }

    // --------------------------------------------------------------- /help ----
    //
    // Nobody discovers a chat channel by accident, and an unlisted feature may as well
    // not exist. Only the commands the asker can actually use are listed - offering
    // someone /ban and then refusing it is worse than not mentioning it.
    /*
     * /help, and /help <topic>.
     *
     * Cam, 2026-09-04: "make sure you also update the /help command so we can see all the
     * new commands and actions we can do."
     *
     * It had drifted badly. The phone, trading, medical, vehicles and character systems all
     * shipped without ever being listed, so roughly forty commands existed that no player
     * could discover from inside the game - and the four lines that WERE listed described
     * /kill as something it no longer does.
     *
     * TOPICS RATHER THAN ONE WALL. Printing every command at once is around fifty lines
     * into a chat box that shows a handful, which scrolls the answer away as it arrives.
     * The bare command is a map; each topic is the detail. Staff sections stay folded into
     * the same scheme so nobody has to remember a second command to find their own tools.
     */
    // ------------------------------------------------------------- /audit ----
    //
    // Read the ledger back. The trade brief asks for this directly - "allow staff to search
    // TradeID, CharacterID, ... extremely useful for staff investigating exploits" - and
    // until now everything was being recorded faithfully and could only be read by someone
    // with shell access to the box. That is half an audit trail: an exploit report is
    // answered in minutes or it is not answered.
    //
    // ONE SUBSTRING, not a query language. Every id here is already distinctive - a
    // character id, a Discord id, a trade id, a dotted action like "trade.completed" - so a
    // contains-match answers every question the brief lists with one code path and nothing
    // for a moderator to memorise. `/audit TRADE-000184`, `/audit trade.completed`,
    // `/audit <character id>` all work the same way.
    //
    // ADMIN, not moderator. This is every player's money and item history, which is a
    // heavier thing to hand out than /whois - and per Cam's ladder senior moderator carries
    // what admin used to, so the people who investigate still have it. One word to lower if
    // that turns out to be too tight in practice.
    if (command == "/audit")
    {
        if (!acSender.HasAtLeast(EPermissionLevel::kAdmin))
            return deny(EPermissionLevel::kAdmin);

        if (target.empty())
        {
            Tell(acSender, "Usage: /audit <what> [count]");
            Tell(acSender, "  <what> is any id or action - a trade id, a character id,");
            Tell(acSender, "  a Discord id, or an action like trade.completed");
            Tell(acSender, "  Newest first. Default 8, most 20.");
            return true;
        }

        // Count is optional and second. A bad number is treated as "not given" rather than
        // refused - somebody investigating should not be arguing with the parser.
        size_t wanted = 8;

        if (const auto space = rest.find_first_not_of(' '); space != std::string::npos)
        {
            try
            {
                const auto asked = std::stoul(rest.substr(space));
                if (asked > 0)
                    wanted = std::min<size_t>(asked, 20);
            }
            catch (...)
            {
                // Left at the default.
            }
        }

        const auto lines = GServer->GetAuditLog().Search(target, wanted);

        if (lines.empty())
        {
            Tell(acSender, fmt::format("Nothing in the ledger matches '{}'.", target));
            return true;
        }

        Tell(acSender, fmt::format("--- {} entr{} for '{}', newest first ---", lines.size(),
                                   lines.size() == 1 ? "y" : "ies", target));

        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

        for (const auto& line : lines)
        {
            // Rendered rather than dumped. A raw ledger line is JSON with an instance id and
            // a millisecond timestamp in it - correct for a tool, unreadable in a chat box
            // that shows a handful of short lines.
            try
            {
                const auto entry = nlohmann::json::parse(line);

                const auto at = entry.value("at", int64_t{0});
                const auto ageSeconds = at > 0 ? (nowMs - at) / 1000 : 0;

                std::string when = "?";
                if (at > 0)
                {
                    if (ageSeconds < 60)
                        when = fmt::format("{}s ago", ageSeconds);
                    else if (ageSeconds < 3600)
                        when = fmt::format("{}m ago", ageSeconds / 60);
                    else if (ageSeconds < 86400)
                        when = fmt::format("{}h ago", ageSeconds / 3600);
                    else
                        when = fmt::format("{}d ago", ageSeconds / 86400);
                }

                const auto action = entry.value("action", std::string{"?"});
                const auto actor = entry.value("actor", std::string{});
                const auto subject = entry.value("subject", std::string{});

                std::string who = actor;
                if (!subject.empty() && subject != actor)
                    who = fmt::format("{} -> {}", actor, subject);

                std::string details;
                if (entry.contains("details") && entry["details"].is_object())
                {
                    details = entry["details"].dump();

                    // The details are free-form and some carry whole item lists. Truncated
                    // so one fat entry cannot push the other seven off the screen.
                    if (details.size() > 90)
                        details = details.substr(0, 87) + "...";
                }

                Tell(acSender, fmt::format("  {} {} {} {}", when, action, who, details));
            }
            catch (...)
            {
                // A line that will not parse is still evidence - show it raw rather than
                // dropping it, because a malformed entry is itself worth seeing.
                Tell(acSender, fmt::format("  (unparsed) {}",
                                           line.size() > 110 ? line.substr(0, 107) + "..." : line));
            }
        }

        return true;
    }

    if (command == "/help")
    {
        // Folded here rather than through a helper - this file has no ToLower, and adding
        // one for a single use would be a header change for nothing. "/help PHONE" should
        // work, because somebody who types it that way is not making a mistake.
        std::string topic = target;
        std::transform(topic.begin(), topic.end(), topic.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (topic.empty())
        {
            Tell(acSender, "Type /help <topic> for detail. Topics:");
            Tell(acSender, "  chat     talking, and who can hear you");
            Tell(acSender, "  me       your character, name and id");
            Tell(acSender, "  phone    numbers, contacts, texts and calls");
            Tell(acSender, "  money    paying people, and trading face to face");
            Tell(acSender, "  cars     your garage, buying and selling");
            Tell(acSender, "  medical  being down, and helping someone who is");
            Tell(acSender, "  stuck    fell through the world? start here");

            if (acSender.HasAtLeast(EPermissionLevel::kModerator))
                Tell(acSender, "  staff    moderation and admin tools");

            return true;
        }

        if (topic == "chat")
        {
            Tell(acSender, fmt::format("  just type          - local, heard within {:.0f}m", ChatRange::kLocal));
            Tell(acSender, fmt::format("  /yell <message>    - heard within {:.0f}m", ChatRange::kYell));
            Tell(acSender, fmt::format("  /whisper <message> - heard within {:.0f}m", ChatRange::kWhisper));
            Tell(acSender, "  /who               - everyone online");

            if (acSender.HasAtLeast(EPermissionLevel::kAdmin))
                Tell(acSender, "  /advert <message>  - the whole server");

            return true;
        }

        if (topic == "me")
        {
            Tell(acSender, "  /character         - your character sheet (/char works too)");
            Tell(acSender, "  /inventory         - what you are carrying, and your eddies");
            Tell(acSender, "  /name <name>       - set your character's name");
            return true;
        }

        if (topic == "phone")
        {
            Tell(acSender, "  /number            - your own number");
            Tell(acSender, "  /contacts          - your contact list");
            Tell(acSender, "  /addcontact <number> [name]");
            Tell(acSender, "  /contactname <number> <name>   - rename a contact");
            Tell(acSender, "  /delcontact <number>");
            Tell(acSender, "  /text <number> <message>       - send a text");
            Tell(acSender, "  /texts             - conversations waiting for you");
            Tell(acSender, "  /read <number>     - read one conversation");
            Tell(acSender, "  /call <number>     - ring somebody");
            Tell(acSender, "  /answer, /decline, /hangup     - a call that is ringing");
            Tell(acSender, "  /calls             - who is ringing you");
            Tell(acSender, "  /block <number>, /unblock <number>, /blocked");
            return true;
        }

        if (topic == "money")
        {
            Tell(acSender, "  /pay <player> <amount>         - hand over eddies");
            Tell(acSender, "  /trade <player>    - start a face-to-face trade");
            Tell(acSender, "  /trade money <amount>          - put money in");
            Tell(acSender, "  /trade item <name> [qty]       - put an item in");
            Tell(acSender, "  /trade view        - what is on the table");
            Tell(acSender, "  /trade accept      - agree to it");
            Tell(acSender, "  /trade confirm     - final, both sides must confirm");
            Tell(acSender, "  /trade cancel      - walk away");
            return true;
        }

        if (topic == "cars")
        {
            Tell(acSender, "  /garage            - what you own");
            Tell(acSender, "  /sellcar <player> <vehicle> <price>");
            Tell(acSender, "  /buycar            - accept an offer made to you");
            Tell(acSender, "  /declinecar        - refuse one");

            if (acSender.HasAtLeast(EPermissionLevel::kEventStaff))
                Tell(acSender, "  /givecar <player> <vehicle>    - event staff");

            if (acSender.HasAtLeast(EPermissionLevel::kModerator))
                Tell(acSender, "  /vehseats <player> - who is sitting where");

            return true;
        }

        if (topic == "medical")
        {
            Tell(acSender, fmt::format("  Down is not dead. You have {} seconds before you bleed out.",
                                       kBleedoutSeconds));
            Tell(acSender, "  /assess <player>   - how badly hurt are they");
            Tell(acSender, "  /stabilize <player>            - stop their bleedout clock");
            Tell(acSender, "  /revive <player>   - bring them back up");
            Tell(acSender, "  /respawn           - only once you have actually bled out");
            return true;
        }

        if (topic == "stuck")
        {
            Tell(acSender, "  Fell through the world? It should put you back on its own -");
            Tell(acSender, "  wait a couple of seconds before doing anything else.");
            Tell(acSender, "  If you are still stuck, ask a staff member in chat.");

            // Only shown to people who can actually run it. Advertising a staff command to
            // everybody is how you get a queue of players typing it and getting refused.
            if (acSender.HasAtLeast(EPermissionLevel::kEventStaff))
                Tell(acSender, "  /tp spawn          - staff: sends you to the spawn point");

            return true;
        }

        if (topic == "staff")
        {
            if (!acSender.HasAtLeast(EPermissionLevel::kModerator))
                return deny(EPermissionLevel::kModerator);

            Tell(acSender, "Moderator:");
            Tell(acSender, "  /kick <player> [reason], /bans");
            Tell(acSender, "  /jail <player> <minutes> [reason] - cell is where you stand");
            Tell(acSender, "  /unjail <player>");
            Tell(acSender, "  /kill <player> [reason] - puts them down where they stand");
            Tell(acSender, "  /whois <player>    - their ids");

            if (acSender.HasAtLeast(EPermissionLevel::kEventStaff))
            {
                Tell(acSender, "Event staff:");
                Tell(acSender, "  /tp <player>       - brings them to you");
                Tell(acSender, "  /tp to <player>    - sends you to them");
                Tell(acSender, "  /return <player>   - puts them back");
                Tell(acSender, "  /time HH:MM        - the shared clock (/time real mirrors reality)");
                Tell(acSender, "  /weather <state>   - the shared sky (sunny, rain, fog...)");
                Tell(acSender, "  /npc <record> [name] - a persistent NPC here (/npc clear)");
            }

            if (acSender.HasAtLeast(EPermissionLevel::kAdmin))
            {
                Tell(acSender, "Admin:");
                Tell(acSender, "  /ban <player> [reason], /unban <discord id>");
                Tell(acSender, "  /rename <character> <name>");
                Tell(acSender, "  /audit <what> [n]  - search the ledger (trade id, character, action)");
                Tell(acSender, "  /setspawn          - where players wake up after dying");
                Tell(acSender, "  /setstart          - where brand-new characters arrive");
                Tell(acSender, "  /quest, /fact      - quest and world state");
            }

            return true;
        }

        Tell(acSender, fmt::format("No help topic called '{}'. Type /help for the list.", topic));
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

    /*
     * FLOOD CONTROL, before anything is parsed, logged or relayed.
     *
     * Asked for by both briefs - the phone's section 27 ("prevent spam without making
     * normal RP communication annoying") and the trade brief's section 30 - and chat had
     * none. Quickhacks have per-hack cooldowns and movement rejects floods; this was the
     * one path a client could drive as fast as it liked, and it is the path that copies
     * text to every player in range AND appends it to the log on disk.
     *
     * Checked FIRST, so a flooding client costs the server a comparison rather than a
     * broadcast and a disk write. Everything below this - truncation, control-character
     * stripping, command dispatch - is work a flood should never reach.
     *
     * A SLIDING WINDOW rather than a minimum gap between messages. A fixed delay punishes
     * somebody firing off several short lines in a scene, which is the thing a roleplay
     * server exists for, while still permitting a sustained stream at exactly the limit.
     *
     * TUNED DELIBERATELY LOOSE. A real flood sends thousands a second, so anything in this
     * range stops it equally - which means the number should be chosen to never catch a
     * real player rather than to be tight. Both briefs say the same thing: "prevent spam
     * WITHOUT making normal RP communication annoying". The first draft was 10 per 5s and
     * the test caught it refusing a line every 400ms, which is fast but not inhuman. Twenty
     * is four a second sustained - past anyone typing - and still bounds the server to four
     * broadcasts and four log writes per player per second, which is the actual cost.
     *
     * NOT exempted for staff. A single limit has no privilege hole to find, and no
     * moderator types faster than this either.
     */
    constexpr int64_t kChatWindowMs = 5000;
    constexpr uint32_t kChatBurst = 20;

    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

    auto* pRate = entity.get_mut<PlayerComponent>();

    if (nowMs - pRate->ChatWindowStartMs >= kChatWindowMs)
    {
        pRate->ChatWindowStartMs = nowMs;
        pRate->ChatInWindow = 0;
        pRate->ChatFloodWarned = false;
    }

    ++pRate->ChatInWindow;

    if (pRate->ChatInWindow > kChatBurst)
    {
        // Told once per window. Replying to every message of a flood is the same denial of
        // service with the server volunteering to do the work.
        if (!pRate->ChatFloodWarned)
        {
            pRate->ChatFloodWarned = true;

            Tell(*pPlayer, "You are sending messages too quickly - slow down.");

            spdlog::warn("[chat] rate limited {} ({} in {}ms)", pPlayer->Username, pRate->ChatInWindow,
                         kChatWindowMs);
        }

        return;
    }

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
