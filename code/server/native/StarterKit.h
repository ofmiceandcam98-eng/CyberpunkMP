#pragma once

#include <cstdint>
#include <cstddef>
#include <span>

/**
 * What a brand new character owns the moment they finish the creator.
 *
 * Before this existed a new character was simply stripped: the restore pass took back
 * everything the loaded save happened to be carrying and left them standing in Night City
 * naked with no money. Correct in that the server owns possessions, useless as a starting
 * point.
 *
 * Three lifepaths only for now - Corporate, StreetKid, Nomad - because those are the three
 * the game itself offers, and each one gets the clothes that lifepath actually starts in
 * plus one ordinary sidearm. Nothing iconic, nothing legendary, nothing from a quest.
 *
 * EVERY ID BELOW WAS VERIFIED AGAINST THE INSTALLED 2.31 DATABASE, not against a wiki.
 *
 * Release builds ship no debug name table (TDBID.ToStringDEBUG returns empty strings on
 * 2.31, which is how equipment sync once spent a week looking fine and shipping nothing),
 * so a record name cannot be read back out of the game to check it. It can be checked the
 * other way round though: a TweakDBID is CRC32(name) | (length << 32), so hashing a
 * candidate name and searching r6/cache/tweakdb.bin for that 8-byte value answers "does
 * this record exist in this install" exactly.
 *
 * The static_asserts at the bottom of this file are that check, frozen. Each one compares
 * the ID this header computes from the name against the value found in tweakdb.bin by
 * hand. If the hash ever computes differently the build stops, so a wrong ID cannot reach
 * a player as a silently missing item.
 */
namespace StarterKit
{
/**
 * CRC32 (IEEE, reflected, poly 0xEDB88320) at compile time.
 *
 * Written out rather than pulled from a library because it has to run in a constant
 * expression for the static_asserts below to mean anything.
 */
constexpr uint32_t Crc32(const char* acpString, const size_t acLength) noexcept
{
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < acLength; ++i)
    {
        crc ^= static_cast<uint8_t>(acpString[i]);

        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    }

    return crc ^ 0xFFFFFFFFu;
}

constexpr size_t Length(const char* acpString) noexcept
{
    size_t n = 0;
    while (acpString[n] != '\0')
        ++n;
    return n;
}

/**
 * The record name's TweakDBID: the CRC32 in the low 32 bits, the name's length in the
 * high ones. The length matters - two records whose names collide on CRC32 alone are
 * still distinct if they are different lengths, which is why the game packs it in.
 */
constexpr uint64_t TweakId(const char* acpName) noexcept
{
    const size_t length = Length(acpName);
    return static_cast<uint64_t>(Crc32(acpName, length)) | (static_cast<uint64_t>(length) << 32);
}

/**
 * Mirrors the game's own gamedataLifePath, whose values come straight out of
 * tools/redmod/scripts/core/data/tweakDBEnums.script:
 *
 *     import enum gamedataLifePath { Corporate, Nomad, StreetKid, Count, Invalid }
 *
 * Note the order: Nomad is 1 and StreetKid is 2, which is not the order they appear in
 * anywhere else. Note also that the game calls it Corporate, not Corpo.
 */
enum class Lifepath : uint32_t
{
    Corporate = 0,
    Nomad = 1,
    StreetKid = 2,

    Unknown = 0xFFFFFFFFu,
};

/**
 * The client sends the raw gamedataLifePath value. Anything that is not one of the three
 * we support becomes Unknown, which grants nothing rather than guessing a kit - a player
 * with no clothes is a bug report, a player with the wrong lifepath's kit is a silent one.
 */
constexpr Lifepath FromWire(const uint32_t aValue) noexcept
{
    switch (aValue)
    {
    case 0: return Lifepath::Corporate;
    case 1: return Lifepath::Nomad;
    case 2: return Lifepath::StreetKid;
    default: return Lifepath::Unknown;
    }
}

constexpr const char* ToString(const Lifepath aLifepath) noexcept
{
    switch (aLifepath)
    {
    case Lifepath::Corporate: return "corpo";
    case Lifepath::StreetKid: return "streetkid";
    case Lifepath::Nomad: return "nomad";
    default: return "unknown";
    }
}

constexpr Lifepath FromString(const std::string_view aName) noexcept
{
    if (aName == "corpo") return Lifepath::Corporate;
    if (aName == "streetkid") return Lifepath::StreetKid;
    if (aName == "nomad") return Lifepath::Nomad;
    return Lifepath::Unknown;
}

/**
 * One entry in a kit. The name is carried alongside the id purely so the grant can be
 * logged in a form a human can read - the server never needs to know what an item IS.
 */
struct KitItem
{
    const char* Name;
    uint64_t Id;
    uint32_t Quantity;
};

/**
 * Every new character starts with this much, once, at creation. There is no banking
 * system, so this is simply their money - no cash/bank split, no account.
 */
constexpr int64_t kStartingMoney = 20000;

/**
 * Handgun ammunition, shared by all three kits because all three sidearms are pistols.
 * A hundred rounds: enough to matter, not enough to be a supply drop.
 */
constexpr uint32_t kStartingAmmo = 100;

// --- Corpo: the office clothes V is wearing when the lifepath opens, plus a Lexington.
constexpr KitItem kCorpo[] = {
    {"Items.Q000_Corpo_FormalJacket",  TweakId("Items.Q000_Corpo_FormalJacket"),  1},
    {"Items.Q000_Corpo_FormalPants",   TweakId("Items.Q000_Corpo_FormalPants"),   1},
    {"Items.Q000_Corpo_FormalShoes",   TweakId("Items.Q000_Corpo_FormalShoes"),   1},
    {"Items.Preset_Lexington_Default", TweakId("Items.Preset_Lexington_Default"), 1},
    {"Ammo.HandgunAmmo",               TweakId("Ammo.HandgunAmmo"),               kStartingAmmo},
};

// --- Streetkid: the street clothes, plus a Unity.
constexpr KitItem kStreetKid[] = {
    {"Items.Q000_StreetKid_TShirt", TweakId("Items.Q000_StreetKid_TShirt"), 1},
    {"Items.Q000_StreetKid_Pants",  TweakId("Items.Q000_StreetKid_Pants"),  1},
    {"Items.Q000_StreetKid_Shoes",  TweakId("Items.Q000_StreetKid_Shoes"),  1},
    {"Items.Preset_Unity_Default",  TweakId("Items.Preset_Unity_Default"),  1},
    {"Ammo.HandgunAmmo",            TweakId("Ammo.HandgunAmmo"),            kStartingAmmo},
};

// --- Nomad: the badlands outfit including the Bakkers-patch vest, plus a Nova.
//
// Deliberately the patched vest and not Items.Q000_Nomad_noPatch_Vest. Both records exist
// in 2.31 and were verified; granting both would put two vests in one inventory, so the
// no-patch one is left out rather than listed and commented.
constexpr KitItem kNomad[] = {
    {"Items.Q000_Nomad_TShirt",   TweakId("Items.Q000_Nomad_TShirt"),   1},
    {"Items.Q000_Nomad_Pants",    TweakId("Items.Q000_Nomad_Pants"),    1},
    {"Items.Q000_Nomad_Boots",    TweakId("Items.Q000_Nomad_Boots"),    1},
    {"Items.Q000_Nomad_Vest",     TweakId("Items.Q000_Nomad_Vest"),     1},
    {"Items.Preset_Nova_Default", TweakId("Items.Preset_Nova_Default"), 1},
    {"Ammo.HandgunAmmo",          TweakId("Ammo.HandgunAmmo"),          kStartingAmmo},
};

/**
 * The kit for a lifepath, or an empty span for one we do not support yet. Empty is a
 * deliberate answer, not a failure to find one - see FromWire.
 */
constexpr std::span<const KitItem> For(const Lifepath aLifepath) noexcept
{
    switch (aLifepath)
    {
    case Lifepath::Corporate: return {kCorpo};
    case Lifepath::StreetKid: return {kStreetKid};
    case Lifepath::Nomad: return {kNomad};
    default: return {};
    }
}

// ---------------------------------------------------------------------------------------
// Verification, frozen.
//
// The right-hand values were obtained by hashing each name and finding that exact 8-byte
// value inside the installed r6/cache/tweakdb.bin. The method was checked with controls
// first: two invented names were absent, and "Items.HandgunAmmo" was absent while
// "Ammo.HandgunAmmo" was present, so it distinguishes a near miss rather than only
// nonsense.
// ---------------------------------------------------------------------------------------

static_assert(TweakId("Items.Q000_Corpo_FormalJacket")  == 0x0000001D3FB32D3Bull);
static_assert(TweakId("Items.Q000_Corpo_FormalPants")   == 0x0000001C1247314Bull);
static_assert(TweakId("Items.Q000_Corpo_FormalShoes")   == 0x0000001C7AF44D36ull);

static_assert(TweakId("Items.Q000_StreetKid_TShirt")    == 0x0000001BCC508217ull);
static_assert(TweakId("Items.Q000_StreetKid_Pants")     == 0x0000001A70DB4434ull);
static_assert(TweakId("Items.Q000_StreetKid_Shoes")     == 0x0000001A18683849ull);

static_assert(TweakId("Items.Q000_Nomad_TShirt")        == 0x0000001729B34891ull);
static_assert(TweakId("Items.Q000_Nomad_Pants")         == 0x0000001646AFCFF9ull);
static_assert(TweakId("Items.Q000_Nomad_Boots")         == 0x00000016BD92161Full);
static_assert(TweakId("Items.Q000_Nomad_Vest")          == 0x0000001514156BE6ull);

static_assert(TweakId("Items.Preset_Lexington_Default") == 0x0000001E1601AA89ull);
static_assert(TweakId("Items.Preset_Unity_Default")     == 0x0000001A78EAB3EAull);
static_assert(TweakId("Items.Preset_Nova_Default")      == 0x00000019B1E27E8Eull);

static_assert(TweakId("Ammo.HandgunAmmo")               == 0x00000010FE92A980ull);
} // namespace StarterKit
