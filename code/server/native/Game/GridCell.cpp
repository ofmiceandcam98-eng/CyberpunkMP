#include "GridCell.h"
#include "Components/CellComponent.h"

GridCell::GridCell(TPosition aPosition)
    : m_position(aPosition)
{
    
}

GridCell::~GridCell()
{
}

void GridCell::Add(flecs::entity aEntity) noexcept
{
    m_entities.push_back(aEntity);
    aEntity.emplace<CellComponent>(this);
}

void GridCell::Remove(flecs::entity aEntity) noexcept
{
    aEntity.remove<CellComponent>();
    m_entities.erase(std::ranges::find(m_entities, aEntity));
}

