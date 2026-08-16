#pragma once

#include <string>
#include <vector>
#include <random>
#include <cstdint>

struct AttributesRecord
{
    int Body{6};
    int Reflexes{6};
    int TechnicalAbility{6};
    int Intelligence{6};
    int Cool{6};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AttributesRecord, Body, Reflexes, TechnicalAbility, Intelligence, Cool)
};

struct ItemRecord
{
    std::string TweakId;
    int Count{1};
    int Slot{0};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ItemRecord, TweakId, Count, Slot)
};

struct CyberwareRecord
{
    std::string Slot;
    std::string ItemTweakId;
    bool Equipped{true};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CyberwareRecord, Slot, ItemTweakId, Equipped)
};

struct WantedStatusRecord
{
    int Level{0};
    float Bounty{0.f};
    std::string Reason;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(WantedStatusRecord, Level, Bounty, Reason)
};

/**
 * A multiplayer character, owned by the server.
 *
 * Character Profile schema enforcing the hierarchy:
 * Discord Account -> Player ID -> Character ID -> Character Profile -> Character Entity
 */
struct CharacterRecord
{
    int Slot{0};

    std::string Name;
    std::string Appearance;
    bool IsMale{true};

    // Progression
    int Level{1};
    int AttributePoints{0};
    int PerkPoints{0};
    AttributesRecord Attributes;
    std::vector<std::string> Perks;

    // Vitals & Wallet
    float Health{100.f};
    float MaxHealth{100.f};
    int64_t Money{1000};

    // Inventory & Gear
    std::vector<ItemRecord> Inventory;
    std::vector<CyberwareRecord> Cyberware;

    // Roleplay Identity & Profile Attributes
    std::string Occupation{"Solo"};
    std::string Lifepath{"Streetkid"};
    std::string Affiliation{"Unaffiliated"};
    std::string Bio;

    // Profile Rules & Permission Constraints
    bool BioSet{false};                 // /setbio can only be run once by normal players
    int64_t LastAffiliationChange{0};   // Unix timestamp for 1-week cooldown check
    bool IsAffiliationLeader{false};    // Leadership flag for granting affiliation to others

    // Status & Location
    WantedStatusRecord WantedStatus;
    float PositionX{0.f};
    float PositionY{0.f};
    float PositionZ{0.f};
    float Yaw{0.f};

    bool Initialised{false};
    bool NameChosen{false};
    bool SpawnedBefore{false};

    std::string CharacterId;

    int64_t CreatedAt{0};
    int64_t UpdatedAt{0};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CharacterRecord, Slot, Name, Appearance, IsMale,
                                                Level, AttributePoints, PerkPoints, Attributes,
                                                Perks, Health, MaxHealth, Money, Inventory,
                                                Cyberware, Occupation, Lifepath, Affiliation,
                                                Bio, BioSet, LastAffiliationChange,
                                                IsAffiliationLeader, WantedStatus, PositionX,
                                                PositionY, PositionZ, Yaw, Initialised, NameChosen,
                                                SpawnedBefore, CharacterId, CreatedAt, UpdatedAt)
};

/**
 * A fresh character id: 16 hex characters, random.
 */
inline std::string GenerateCharacterId()
{
    static std::mt19937_64 engine{std::random_device{}()};
    static std::uniform_int_distribution<uint64_t> dist;

    const uint64_t value = dist(engine);

    static constexpr char kHex[] = "0123456789abcdef";
    std::string id(16, '0');

    for (int i = 0; i < 16; ++i)
        id[15 - i] = kHex[(value >> (i * 4)) & 0xF];

    return id;
}

/**
 * Base64 helper for opaque byte serialization
 */
namespace Base64
{
inline const char* Alphabet()
{
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

inline std::string Encode(const std::vector<uint8_t>& acBytes)
{
    const char* table = Alphabet();

    std::string out;
    out.reserve(((acBytes.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < acBytes.size())
    {
        const uint32_t triple = (acBytes[i] << 16) | (acBytes[i + 1] << 8) | acBytes[i + 2];
        out += table[(triple >> 18) & 0x3F];
        out += table[(triple >> 12) & 0x3F];
        out += table[(triple >> 6) & 0x3F];
        out += table[triple & 0x3F];
        i += 3;
    }

    if (i < acBytes.size())
    {
        const auto remaining = static_cast<uint32_t>(acBytes.size() - i);
        uint32_t triple = acBytes[i] << 16;
        if (remaining == 2)
            triple |= acBytes[i + 1] << 8;

        out += table[(triple >> 18) & 0x3F];
        out += table[(triple >> 12) & 0x3F];
        out += (remaining == 2) ? table[(triple >> 6) & 0x3F] : '=';
        out += '=';
    }

    return out;
}

inline std::vector<uint8_t> Decode(const std::string& acText)
{
    static const auto lookup = []()
    {
        std::vector<int> table(256, -1);
        const char* alphabet = Alphabet();
        for (int i = 0; i < 64; ++i)
            table[static_cast<unsigned char>(alphabet[i])] = i;
        return table;
    }();

    std::vector<uint8_t> out;
    out.reserve((acText.size() / 4) * 3);

    uint32_t buffer = 0;
    int bits = 0;

    for (const char c : acText)
    {
        if (c == '=')
            break;

        const int value = lookup[static_cast<unsigned char>(c)];
        if (value < 0)
            continue;

        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;

        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }

    return out;
}
} // namespace Base64
