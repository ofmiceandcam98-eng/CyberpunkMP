#pragma once

#include <cstdint>
#include <unordered_map>

// What a netrunner can actually afford, owned by the server.
//
// THE HOLE THIS CLOSES. Quickhack damage used to arrive through the ordinary hit path,
// which means it was the CLIENT's number - clamped to one health pool, but otherwise
// whatever they said. There was no RAM, no cooldown and no ownership check, so a modified
// client could land a full-health Overheat as fast as it could send packets, for free.
//
// RAM is the resource that makes quickhacking a decision rather than a button. If the
// client owns it, there is no decision.
//
// DELIBERATELY NOT A MODEL OF CYBERPUNK'S NETRUNNING. There are no perks here, no cyberdeck
// tiers, no quickhack upgrade levels, no RAM regeneration curve. Reimplementing those
// server-side would be months of work that drifted from the game every patch - the same
// reasoning that keeps weapon damage on the engine's side. What the server needs is the
// smallest state that makes cheating expensive: how much RAM is left, when each hack was
// last used, and what a hack is allowed to cost and do.
struct QuickhackComponent
{
    // Cyberpunk's Memory pool, in the same units the client reports, so nothing has to be
    // converted and a mismatch is visible rather than silently scaled.
    float Ram{100.f};
    float MaxRam{100.f};

    // When the pool was last recomputed, so regeneration can be applied lazily.
    //
    // Lazily rather than on a tick: RAM only matters at the instant somebody spends it, so
    // a per-frame system updating every player's pool would be work nobody observes. The
    // elapsed time since this stamp is what regenerates.
    uint64_t RamStampMs{0};

    // Per second. Cyberpunk's own regeneration is a stat-modified curve that pauses in
    // combat and differs per deck - not something reproducible here. This is a deliberately
    // CONSERVATIVE flat rate: slower than the game's, so the server's pool is the tighter of
    // the two and a player is limited by their own game rather than by us.
    //
    // The consequence to be aware of: somebody with a heavily upgraded deck will occasionally
    // be refused a hack their own game would allow. That is the right direction to be wrong
    // in - the alternative is a pool that refills faster than the game's, which is free
    // hacking with extra steps.
    static constexpr float kRamRegenPerSecond = 2.f;

    // Server time each quickhack was last accepted from this player, keyed by TweakDBID.
    //
    // Per hack rather than one global cooldown, because that is how the game works - firing
    // Ping does not lock out Overheat. A map rather than a fixed array: the set of hacks is
    // TweakDB's to decide, not ours.
    std::unordered_map<uint64_t, uint64_t> LastUsedMs;

    // Rises per player, same replay rejection as combat and weapon events, and separate
    // from both so a burst of hacks and a burst of shots cannot invalidate each other.
    uint32_t LastSequence{0};
    bool HasSequence{false};
};

// What a quickhack costs and does, decided by the SERVER.
//
// The client is never asked. A request names WHICH hack; everything about what that means
// is looked up here, which is the whole difference between "the client asked for Overheat"
// and "the client says Overheat does 999999 damage".
//
// Values are deliberately conservative placeholders rather than Cyberpunk's real numbers,
// which live in TweakDB and vary by cyberdeck, perks and hack tier. Getting them exactly
// right needs the same live-dump treatment the objectActions needed; getting them ROUGHLY
// right is enough to make the authority real today, and a wrong damage figure is a balance
// problem rather than a security one.
struct QuickhackRule
{
    // A CEILING on what this hack may charge, not its price.
    //
    // The price cannot live here. GetCost() computes it per use from stat modifiers against
    // the attacker's deck and perks, so it differs between two players using the same hack -
    // there is no correct number to store. The client reports what the game charged and this
    // bounds it, which is what stops a client claiming an Overheat cost it 0.1 RAM.
    //
    // Generous on purpose. A ceiling that is too tight refuses legitimate expensive hacks
    // from a heavily-modified deck; one that is merely sane still makes free hacking
    // impossible, which is the actual objective.
    float MaxRamCost{40.f};

    // ORDER MATTERS. The rule table uses braced initialisers - {ram, damage, cooldown} -
    // so moving these silently reassigns every entry rather than failing to compile.
    // ALWAYS ZERO for anything Cyberpunk damages itself, which is every damaging quickhack.
    //
    // The game applies quickhack damage through its ordinary hit pipeline, so the hit hook
    // already carries the real figure - computed with the attacker's deck, their perks and
    // the target's resistances. Adding a number here would double-count it.
    //
    // Kept as a field rather than deleted because a future effect the game does NOT damage
    // through the hit path would need it, and finding that out later is easier if the shape
    // is already here with this note attached.
    float Damage{0.f};

    // A FLOOR on how often this hack may be used, not the game's cooldown.
    //
    // Same reasoning as the ceiling above, and the same shape as the weapon fire-rate floor:
    // the real cooldown is stat-modified and varies per player, so this is not it. What it
    // does is separate a fast netrunner from a script sending requests every frame, which is
    // the attack worth stopping.
    uint64_t MinIntervalMs{3000};
};

/**
 * A TweakDBID as its number, computed from the record name.
 *
 * NOT a plain hash of the string, which is the trap. A TweakDBID is FNV1a32 of the name in
 * the low 32 bits with the name's LENGTH in bits 32-39, and TDBID.ToNumber - what the client
 * sends - returns exactly that. Keying the rule table on an ordinary 64-bit hash would
 * compile, run, and never match a single request, refusing every quickhack as unknown while
 * looking entirely correct.
 *
 * Verified against a live dump from the game rather than derived from documentation:
 *
 *     ToTweakDBID{ hash = 0xC9259006, length = 26 --[[ QuickHack.BaseOverheatHack --]] }
 *
 * "QuickHack.BaseOverheatHack" is 26 characters, and the static_assert below fails the
 * build if this ever stops producing 0xC9259006.
 */
constexpr uint64_t TweakDBIDFromName(const char* acpName, size_t aLength) noexcept
{
    // CRC32, not FNV. The first attempt at this used FNV1a32 - a reasonable guess, since
    // the rest of this project hashes with FNV - and the assert below caught it at compile
    // time. Without that assert it would have built cleanly and refused every quickhack in
    // the game as "unknown", which is the kind of bug that costs a day.
    //
    // Standard IEEE/zlib CRC32: reflected, polynomial 0xEDB88320, initialised to all ones
    // and finally inverted. Computed here rather than table-driven so it stays constexpr.
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < aLength; ++i)
    {
        crc ^= static_cast<uint8_t>(acpName[i]);

        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
    }

    crc ^= 0xFFFFFFFFu;

    return static_cast<uint64_t>(crc) | (static_cast<uint64_t>(aLength & 0xFF) << 32);
}

template <size_t N> constexpr uint64_t TweakDBIDFromName(const char (&acName)[N]) noexcept
{
    // N includes the terminating null, which is not part of the name.
    return TweakDBIDFromName(acName, N - 1);
}

// The ground truth from the dump. If a future change breaks the hash, this fails at compile
// time rather than silently refusing every quickhack in the game.
static_assert((TweakDBIDFromName("QuickHack.BaseOverheatHack") & 0xFFFFFFFFull) == 0xC9259006ull,
              "TweakDBID hashing no longer matches the game - every quickhack rule would miss");

static_assert((TweakDBIDFromName("QuickHack.BaseOverheatHack") >> 32) == 26,
              "TweakDBID length field no longer matches the game");
