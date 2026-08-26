#pragma once

#include "Math/Vector2.h"


struct GridCell
{
    using TPosition = Vector2<int16_t>;

    GridCell(TPosition aPosition);
    ~GridCell();

    void Add(flecs::entity aEntity) noexcept;
    void Remove(flecs::entity aEntity) noexcept;

    [[nodiscard]] TPosition GetPosition() const noexcept { return m_position; }
    [[nodiscard]] size_t Count() const noexcept { return m_entities.size(); }

    template <class T> void ForEach(T func)
    {
        for (auto pChar : m_entities)
            func(pChar);
    }

    // At least one filter type, deliberately: with `class... Args` allowed to be empty
    // this overload has the same signature as the unfiltered one above, so a plain
    // ForEach(lambda) matched both and every such call was an ambiguous-overload error.
    // Requiring a First makes "no filter" and "filtered" two distinct signatures again.
    template <class T, class First, class... Rest> void ForEach(T func)
    {
        for (auto pChar : m_entities)
        {
            if (pChar.has<First, Rest...>())
                func(pChar);
        }
    }

private:

    Vector<flecs::entity> m_entities;
    TPosition m_position;
};
