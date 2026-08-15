#pragma once

#include <string>
#include <vector>

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

    int64_t CreatedAt{0};
    int64_t UpdatedAt{0};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CharacterRecord, Slot, Name, Appearance, IsMale,
                                                Level, AttributePoints, PerkPoints, Initialised,
                                                NameChosen, SpawnedBefore, CreatedAt, UpdatedAt)
};

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
