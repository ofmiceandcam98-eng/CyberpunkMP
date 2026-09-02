#pragma once

#include <string>
#include <vector>
#include <random>
#include <cstring>
#include <cctype>

/**
 * A multiplayer character, owned by the server.
 *
 * The point of this file is that a player's identity stops living in a Cyberpunk save.
 * Those describe a singleplayer game: they are tens of megabytes, they belong to one
 * machine, and nothing about the server or anyone else's session is in them. Asking
 * somebody to pick one before they can play means they must first have played the
 * singleplayer campaign, and it means the character they roleplay is whatever V they
 * happened to make years ago.
 *
 * The server keeps this instead. It is small, it is authoritative, and it is keyed on the
 * one identifier a player cannot change.
 */
struct CharacterRecord
{
    // Which slot this occupies. Only 0 is used today.
    //
    // Present from the start deliberately: retrofitting a key is painful and adding one
    // that is always zero costs nothing. Multiple characters, and the "continue or start
    // fresh" choice, both hang off this.
    int Slot{0};

    // The name the character is known by in the world - NOT the Discord name. A player
    // being "noremacxxi" and their character being someone else is the entire point of
    // roleplay, and conflating the two is a decision that is very hard to undo later.
    std::string Name;

    /**
     * The appearance, as the game's own CharacterCustomizationState.
     *
     * Base64 of the exact byte blob the client already produces and consumes - the mod has
     * serialised this since before any of this existed, to show remote players correctly.
     * All that changes here is that it now survives a disconnect.
     *
     * Deliberately opaque. The server does not parse it, has no opinion about its
     * contents, and does not break when a game patch changes the format - it stores what
     * the client gave it and hands the same bytes back. A server that understood this
     * would need updating every patch.
     */
    std::string Appearance;

    // Which body the puppet is built from. Kept out of the blob because the server needs
    // it without parsing: it decides which record other clients spawn for this player.
    bool IsMale{true};

    // Progression. Applied on load rather than trusted from the client.
    //
    // Zero level means "not initialised yet" - a freshly created character is brought up
    // to the server's starting state on first spawn, rather than being written here at
    // creation time. That way changing the starting loadout affects everyone who has not
    // spawned yet, instead of only people who create a character afterwards.
    int Level{0};
    int AttributePoints{0};
    int PerkPoints{0};

    // Set once the post-Act-1 initialisation has run for this character. Without it there
    // is no way to tell "a new character" from "a character whose progression happens to
    // be zero", and the loadout would be granted again on every single spawn.
    bool Initialised{false};

    // Whether the PLAYER chose this name, as opposed to it defaulting to their account.
    //
    // Name is never empty - it falls back to the Discord username so a character always
    // has something to be called - which means emptiness cannot be the test for "needs
    // asking". That is exactly the bug this fixes: the prompt asked only when Name was
    // empty, so it could never fire for anybody who already had a character, which was
    // everybody who had ever played. Both existing players were silently skipped.
    //
    // Defaults false, so every character that predates this gets asked once.
    bool NameChosen{false};

    // Has this CHARACTER been into the world yet?
    //
    // The arrivals point set by /setstart is for brand-new characters, and "brand-new" was
    // being decided by whether the ACCOUNT had a record - which is wrong twice over. A
    // record is created the first time anything about a player is stored, so by the time
    // anybody spawned they already had one and the start point was skipped; and somebody
    // replacing their character with a new one kept the old character's position, which is
    // precisely the case the arrivals point exists for.
    //
    // Kept on the character rather than the account so a replacement starts fresh: a new
    // CharacterRecord defaults this to false and is therefore sent to the start point,
    // without anything having to remember to reset it.
    bool SpawnedBefore{false};

    // Which of the game's three lifepaths this character chose, as "corpo", "streetkid" or
    // "nomad" - see StarterKit::ToString. Stored rather than derived because the choice
    // lives in the player's save on the client, and the server has to be able to answer
    // "what is this character" without a client being connected to ask.
    std::string Lifepath;

    // Whether this character has already been handed their lifepath's starting kit.
    //
    // Separate from Initialised on purpose. The kit is granted once per CHARACTER, at
    // creation, and the obvious wrong place to do it is on join - which would top a player
    // up with a fresh outfit, pistol and 20,000 eddies every single time they reconnected.
    // Anything that grants the kit must check this first and set it in the same breath.
    bool StarterKitGranted{false};

    /**
     * What the character owns, held by the server rather than by the player's save.
     *
     * Identity was already server-owned - name, face, body, progression - but possessions
     * were not, and that is the half that decides whether a character is really the
     * server's. Everything a player carried came off their own disk: their guns, their
     * eddies, their cyberware. Two people with the same character name were walking around
     * with whatever their singleplayer file happened to contain, and nothing the server
     * knew could contradict it.
     *
     * Stored as raw TweakDBIDs and counts. The server does not interpret them, for the
     * same reason it does not interpret the appearance blob: a server that understood item
     * records would need updating every game patch, and it has no need to know what a
     * thing IS in order to remember that you have three of it.
     */
    struct ItemStack
    {
        // The item's TweakDBID. A number rather than a name deliberately - names are a
        // client-side convenience and the debug helper that prints them returns empty
        // strings on 2.31, which is how equipment sync spent a week looking fine and
        // shipping nothing.
        uint64_t Id{0};
        uint32_t Quantity{1};

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ItemStack, Id, Quantity)
    };

    std::vector<ItemStack> Inventory;

    /**
     * Skill levels, street cred and character level.
     *
     * All three are the same thing to the game - entries in gamedataProficiencyType - so
     * one list covers what would otherwise be three separate features. Stored as the
     * enum's own numeric value and a level, uninterpreted, for the same reason as items:
     * the server has no need to know what Athletics IS in order to remember that you have
     * eight of it, and a server that did would need updating every patch.
     *
     * Level and AttributePoints above overlap with this and are kept: they are what the
     * spawn path already applies, and rewriting that at the same time as introducing this
     * would make a failure in either impossible to attribute to one of them.
     */
    struct Proficiency
    {
        uint32_t Type{0};
        int32_t Level{0};

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Proficiency, Type, Level)
    };

    std::vector<Proficiency> Proficiencies;

    /**
     * The five attributes, and every perk bought.
     *
     * Kept separate from Proficiencies rather than folded in, even though all three are
     * "a number against a game enum". They are three different enums - gamedataStatType,
     * gamedataNewPerkType, gamedataProficiencyType - and a single list keyed by an
     * untagged number would let a perk id be read back as an attribute the first time
     * anyone reordered one of them. The cost of three lists is three lists; the cost of
     * getting that wrong is somebody's character quietly rebuilt.
     */
    struct Attribute
    {
        uint32_t Type{0};
        int32_t Value{0};

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Attribute, Type, Value)
    };

    struct Perk
    {
        uint32_t Type{0};
        int32_t Level{0};

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Perk, Type, Level)
    };

    std::vector<Attribute> Attributes;
    std::vector<Perk> Perks;

    /**
     * Vehicles this character owns, by the game's own vehicle name.
     *
     * Names rather than TweakDBIDs, unusually - everything else here stores raw ids on the
     * principle that the server should not need to understand game data. Vehicles are the
     * exception because EnablePlayerVehicle takes a STRING, and a TweakDBID cannot be
     * turned back into its string on 2.31: the debug name table release builds ship is
     * empty, which is the same trap that made remote players spawn naked for a week.
     *
     * So the one thing that can survive the round trip is the name, and that is what is
     * kept.
     */
    std::vector<std::string> Vehicles;

    // Eddies. Separate from Inventory because the game models money as an item and this
    // does not - a balance is a number, and treating it as a stack of one item invites
    // somebody to duplicate it by counting wrong.
    int64_t Money{0};

    // This character's own permanent identifier.
    //
    // Everything so far has identified a character as "the one belonging to this Discord
    // account", which works only while there is exactly one. It cannot name a retired
    // character, cannot survive a second slot, and cannot be handed to an admin command
    // without also handing over somebody's account id.
    //
    // Generated once, never derived from the account, and never reused - so it stays
    // stable through renames, retirement and reconnects, while the record it lives in is
    // still filed under the owner's Discord id. Associated with the account, not made out
    // of it.
    std::string CharacterId;

    /**
     * This character's phone number - how other players reach them.
     *
     * Belongs to the CHARACTER, not the account, so somebody who retires a character does
     * not keep the number that people have saved under a name that no longer exists.
     *
     * Assigned once and never regenerated. A number that changes is not a number: every
     * contact list holding the old one silently points at nobody, and the failure shows up
     * as messages that quietly go nowhere rather than as an error.
     */
    std::string PhoneNumber;

    /**
     * Numbers this character has added, and nothing more.
     *
     * Per character on purpose. A contact somebody adds is theirs alone - nobody else's
     * phone gains an entry because one player looked someone up, which is what makes a
     * number worth handing out in the first place.
     *
     * Stores the NUMBER rather than a character id, because the number is what a player
     * types and what they will still have if the person behind it is not online. Resolving
     * a number to whoever currently holds it is the server's job at the moment of use.
     */
    std::vector<std::string> Contacts;

    /**
     * Quests this character is permitted to see, granted one at a time by an admin.
     *
     * Empty is the normal state and means "no quests", which is the default the server
     * wants - so a character created while nobody is looking is quiet rather than dropped
     * into Act 2. Nothing here turns a quest ON by itself; it lifts the suppression for one
     * person and one quest, which is what makes a story beat something an admin hands out
     * rather than something the game inflicts.
     */
    std::vector<std::string> AllowedQuests;

    int64_t CreatedAt{0};
    int64_t UpdatedAt{0};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CharacterRecord, Slot, Name, Appearance, IsMale,
                                                Level, AttributePoints, PerkPoints, Initialised,
                                                NameChosen, SpawnedBefore, CharacterId,
                                                Lifepath, StarterKitGranted,
                                                Inventory, Money, Proficiencies,
                                                Attributes, Perks, Vehicles,
                                                PhoneNumber, Contacts, AllowedQuests,
                                                CreatedAt, UpdatedAt)
};

/**
 * A fresh character id: 16 hex characters, random.
 *
 * Random rather than sequential or hashed. A counter leaks how many characters the server
 * has ever created and collides the moment two servers merge data; a hash of the account
 * makes the id a reversible restatement of who owns it, which defeats being able to pass
 * it around.
 */
/**
 * A phone number, nine digits: 555-014-372.
 *
 * The 555 prefix is deliberate. It is the block real telephone networks reserve for fiction
 * precisely so a number said out loud cannot ring a real person - and players say these
 * numbers to each other in chat, on stream, and in screenshots.
 *
 * The remaining six digits are a million numbers, and the point of that is NOT collisions -
 * the caller checks for those and would catch them at any length. It is guessability. With
 * four digits somebody could work through every number on the server in an afternoon and
 * text strangers who never gave it to them, which quietly turns a phone book into a
 * broadcast channel. A million makes that pointless, and a number is only private while
 * finding one by accident is hard.
 */
inline std::string GeneratePhoneNumber()
{
    static std::mt19937 engine{std::random_device{}()};
    static std::uniform_int_distribution<int> digits{0, 999999};

    const int value = digits(engine);

    char number[16] = {};
    std::snprintf(number, sizeof(number), "555-%03d-%03d", value / 1000, value % 1000);

    return number;
}

/**
 * Is this something a player could have typed as a number?
 *
 * Checked before any lookup so a malformed argument is answered with "that is not a number"
 * rather than a silent miss that reads identically to "nobody has that number".
 */
inline bool IsPhoneNumberShaped(const std::string& acValue)
{
    // 555-014-372: nine digits, dashes after the third and sixth.
    if (acValue.size() != 11 || acValue[3] != '-' || acValue[7] != '-')
        return false;

    for (size_t i = 0; i < acValue.size(); ++i)
    {
        if (i == 3 || i == 7)
            continue;

        if (acValue[i] < '0' || acValue[i] > '9')
            return false;
    }

    return true;
}

/**
 * The character id alphabet: 23 symbols, none of which can be misread as another.
 *
 * No 0 against O. No 1 against I or L. No 5 against S. No 8 against B, no 2 against Z, no U
 * against V. What is left is what survives being read aloud down a voice channel and typed
 * back by somebody who has never seen it written.
 *
 * TWENTY-THREE IS PRIME AND THAT IS THE WHOLE POINT - see CharacterIdCheckSymbol. Do not add
 * a symbol to "get more ids": it silently destroys the error detection. Six payload symbols
 * over this alphabet is 148 million characters, which is not the constraint on this project.
 */
inline constexpr char kCharacterIdAlphabet[] = "34679ACDEFGHJKMNPRTWXYZ";
inline constexpr size_t kCharacterIdBase = 23; // sizeof(alphabet) - 1, and prime
inline constexpr size_t kCharacterIdPayload = 6;

/**
 * The check symbol: the one that brings a weighted sum to zero modulo 23.
 *
 * Weights are 2,3,4,5,6,7 - distinct, and none of them zero or one. With a PRIME modulus and
 * DISTINCT weights this catches every single-symbol substitution and every transposition of
 * two adjacent symbols, which between them are almost every way a person mis-hears or
 * mis-types a code.
 *
 * Why it exists at all: without a check symbol a typo lands on a DIFFERENT VALID id. An admin
 * running /rename repairs a stranger's character, or a lookup silently answers about somebody
 * else, and nothing anywhere reports an error. That is the failure this prevents, and it is
 * why the 16 random hex characters this replaced were the wrong shape as soon as Cam asked
 * for /rename - hex has no check, so every typo of an id is another valid id.
 */
inline char CharacterIdCheckSymbol(const std::string& acPayload)
{
    size_t sum = 0;

    for (size_t i = 0; i < acPayload.size(); ++i)
    {
        const char* pFound = std::strchr(kCharacterIdAlphabet, acPayload[i]);
        if (!pFound)
            return '\0';

        sum += static_cast<size_t>(pFound - kCharacterIdAlphabet) * (i + 2);
    }

    return kCharacterIdAlphabet[(kCharacterIdBase - (sum % kCharacterIdBase)) % kCharacterIdBase];
}

/**
 * A new character id: six random symbols plus a check symbol, grouped as H7K-M4X3.
 *
 * Grouped because people read codes in chunks, and the hyphen is a reading aid rather than
 * part of the value - ParseCharacterId strips it, so H7KM4X3, h7k m4x3 and H7K-M4X3 are the
 * same id.
 */
inline std::string GenerateCharacterId()
{
    static std::mt19937_64 engine{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, kCharacterIdBase - 1);

    std::string payload;
    payload.reserve(kCharacterIdPayload);

    for (size_t i = 0; i < kCharacterIdPayload; ++i)
        payload.push_back(kCharacterIdAlphabet[dist(engine)]);

    const char check = CharacterIdCheckSymbol(payload);

    return payload.substr(0, 3) + "-" + payload.substr(3) + std::string(1, check);
}

/**
 * Normalises what somebody typed into a stored id, or says why it is not one.
 *
 * FORGIVING ABOUT FORM, STRICT ABOUT CONTENT. Case, spaces, hyphens and underscores are all
 * stripped - somebody reading an id back has no idea where the hyphen went. An unrecognised
 * symbol is REFUSED, never dropped: dropping a stray character turns one player's id into
 * another player's id, which is the exact accident the check symbol exists to catch.
 *
 * Legacy ids (16 hex characters, everything issued before 2026-08-30) are accepted unchanged
 * and lower-cased. They carry no check symbol - nothing can be done about that now, and
 * renumbering a stored key to make it prettier is not worth breaking every reference to it.
 *
 * Returns the normalised id, or an empty string with acReason set to one of:
 * "empty", "length", "alphabet", "checksum".
 */
inline std::string ParseCharacterId(const std::string& acInput, std::string* apReason = nullptr)
{
    const auto fail = [apReason](const char* acpWhy) -> std::string
    {
        if (apReason)
            *apReason = acpWhy;
        return {};
    };

    std::string cleaned;
    cleaned.reserve(acInput.size());

    for (const char c : acInput)
    {
        if (c == '-' || c == '_' || c == ' ' || c == '\t')
            continue;

        cleaned.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }

    if (cleaned.empty())
        return fail("empty");

    // Legacy: 16 hex characters, stored lower-case.
    if (cleaned.size() == 16 &&
        cleaned.find_first_not_of("0123456789ABCDEF") == std::string::npos)
    {
        std::string legacy = cleaned;
        for (char& c : legacy)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        return legacy;
    }

    if (cleaned.size() != kCharacterIdPayload + 1)
        return fail("length");

    for (const char c : cleaned)
    {
        if (!std::strchr(kCharacterIdAlphabet, c))
            return fail("alphabet");
    }

    const std::string payload = cleaned.substr(0, kCharacterIdPayload);
    if (CharacterIdCheckSymbol(payload) != cleaned[kCharacterIdPayload])
        return fail("checksum");

    if (apReason)
        apReason->clear();

    return payload.substr(0, 3) + "-" + payload.substr(3) + std::string(1, cleaned[kCharacterIdPayload]);
}

/**
 * Base64, so the appearance blob can live in the same human-readable JSON as everything
 * else.
 *
 * A raw byte array in JSON would work and would be about four times the size - 6KB of
 * appearance becomes 25KB of "[137,80,78,...]" - and would make the file unreadable for
 * the one thing it is good for, which is opening it to see what the server thinks is true.
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

    // The tail, padded. Getting this wrong produces a blob that decodes to very nearly the
    // right thing, which is far worse than one that fails outright.
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
    // Built once. A 256-entry lookup beats searching the alphabet for every character.
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
            continue;   // whitespace or newlines from a hand-edited file

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
