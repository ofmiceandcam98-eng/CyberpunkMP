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
    float RamCost{4.f};
    float Damage{0.f};
    uint64_t CooldownMs{8000};
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
