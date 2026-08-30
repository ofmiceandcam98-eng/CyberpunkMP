#include "WorldFacts.h"

void WorldFactStore::Load(const std::filesystem::path& acPath) noexcept
{
    m_path = acPath;
    m_facts.clear();

    if (!std::filesystem::exists(m_path))
    {
        // Written rather than left missing, so the file is discoverable. Somebody looking
        // for "where do I open a door" should find a list with a name that explains
        // itself, not an absence they have to guess at.
        //
        // It starts with one entry, because a server with none is a server where Dogtown
        // is shut. Proven 2026-08-29 against the shipped EP1 quest graphs:
        // ep1\openworld\combat_zone_gate\combat_zone_gate.questphase holds the gate's
        // passage branches behind a pause condition on ep1_side_content >= 1, and reads
        // no q301 fact at all - finishing Dog Eat Dog was never what opened the gate. The
        // same fact is the root gate on every Dogtown community, the vendors, the world
        // encounters and the mini world stories, so without it the district is both shut
        // and empty. CDPR's own ep1_chicken_unlocks.questphase opens it exactly this way.
        //
        // Safe to set unconditionally: the game writes this fact = 1 in thirteen places
        // across EP1 and never writes 0, and every use in the main quests is a setter
        // rather than a condition, so nothing reads it to decide whether to start a
        // story. It unlocks the place without starting Phantom Liberty.
        //
        // Seeded only when the file is being created. An existing server's list is its
        // own - re-adding this on every load would quietly undo /fact remove.
        m_facts.push_back({"ep1_side_content", 1});

        Save();
        spdlog::info("No world facts yet - created {} with Dogtown open (ep1_side_content)", m_path.string());
        return;
    }

    try
    {
        std::ifstream file(m_path);
        nlohmann::json document;
        file >> document;

        m_facts = document.get<std::vector<WorldFact>>();

        spdlog::info("Loaded {} world fact(s) from {}", m_facts.size(), m_path.string());
    }
    catch (const std::exception& e)
    {
        // A malformed file is left exactly as it is. Overwriting it with an empty list
        // would destroy whatever somebody was mid-edit on, and an unreadable file is far
        // easier to fix than a deleted one.
        spdlog::error("Could not read {} - {}. No facts will be applied; the file is untouched.",
                      m_path.string(), e.what());
    }
}

void WorldFactStore::Save() const noexcept
{
    if (m_path.empty())
        return;

    try
    {
        std::filesystem::create_directories(m_path.parent_path());

        std::ofstream file(m_path);
        file << nlohmann::json(m_facts).dump(2);
    }
    catch (const std::exception& e)
    {
        spdlog::error("Could not write {} - {}", m_path.string(), e.what());
    }
}

bool WorldFactStore::Set(const std::string& acName, int32_t aValue) noexcept
{
    if (acName.empty())
        return false;

    for (auto& fact : m_facts)
    {
        if (fact.Name == acName)
        {
            fact.Value = aValue;
            Save();
            return true;
        }
    }

    m_facts.push_back({acName, aValue});
    Save();
    return true;
}

bool WorldFactStore::Remove(const std::string& acName) noexcept
{
    const auto before = m_facts.size();

    std::erase_if(m_facts, [&acName](const WorldFact& acFact) { return acFact.Name == acName; });

    if (m_facts.size() == before)
        return false;

    Save();
    return true;
}
