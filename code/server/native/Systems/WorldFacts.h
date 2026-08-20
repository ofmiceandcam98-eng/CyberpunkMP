#pragma once

#include <string>
#include <vector>

/**
 * Quest facts the server sets on every client as they spawn.
 *
 * WHY THIS EXISTS
 *
 * Most locked doors in Night City are gated on a quest fact - the device asks the quest
 * system for one value and decides whether it opens. So "unlock this building for
 * everyone" is, mechanically, "set this fact on everyone", and that is a data question
 * rather than a code one.
 *
 * Kept as a file rather than a list in the source deliberately. There are dozens of places
 * an RP server might want open, each needing its own fact name found by testing, and that
 * list will keep growing for as long as the server exists. Nobody should need a compiler
 * to open a door.
 *
 * WHAT THIS CANNOT DO
 *
 * A fact opens a door. It does not build a room. Several of Night City's most memorable
 * interiors - Konpeki Plaza, All Foods, Clouds during Automatic Love - are quest-scoped:
 * the space is streamed in, dressed and populated only while that quest is running. Set
 * every fact you like and the door will open onto a shell, or onto nothing. Those places
 * need the quest to be live, which is a different problem and possibly not a solvable one.
 *
 * So this list is for places that are simply LOCKED, which is most of the ordinary ones,
 * and not for places that only exist during a story beat.
 */
struct WorldFact
{
    std::string Name;
    int32_t Value{1};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(WorldFact, Name, Value)
};

struct WorldFactStore
{
    // Loaded from config/worldfacts.json at startup, and re-read on /facts reload so a
    // door can be opened without restarting a server people are playing on.
    void Load(const std::filesystem::path& acPath) noexcept;
    void Save() const noexcept;

    const std::vector<WorldFact>& All() const noexcept { return m_facts; }

    // Adds or updates one. Returns false only when the name is empty - anything else is a
    // legitimate value, including zero, which is how a fact gets turned back off.
    bool Set(const std::string& acName, int32_t aValue) noexcept;

    bool Remove(const std::string& acName) noexcept;

private:
    std::vector<WorldFact> m_facts;
    std::filesystem::path m_path;
};
