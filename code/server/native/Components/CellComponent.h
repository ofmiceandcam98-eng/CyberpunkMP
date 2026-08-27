#pragma once

#include "Game/GridCell.h"

struct CellComponent
{
    /**
     * A RAW pointer, and it has to be. This was gsl::not_null<GridCell*>, which is a
     * better type in every way except the one that matters here: not_null has no default
     * constructor - that is the entire point of it - and flecs REQUIRES its components to
     * be default-constructible. It default-constructs them whenever it grows a table or
     * moves an entity between archetypes, which is not something calling code controls.
     *
     * So every time an entity carrying a CellComponent changed archetype, flecs called a
     * constructor that does not exist and the server died with ecs_ctor_illegal, in
     * Level::Add <- AddPlayer <- HandleSpawnCharacterRequest. Level::Add is precisely
     * where a player gets LevelActorTag added and this component set, so it fired on join.
     *
     * It was intermittent because table growth and archetype moves depend on how many
     * entities of which shapes already exist - so it tracked server population and timing
     * rather than anything anyone changed. It cost most of a day, and was variously
     * blamed on a GPU driver, CET, a redscript probe and two of my own changes.
     *
     * The not-null guarantee is not really lost: GetCell() still returns
     * gsl::not_null<GridCell*>, so the value assigned here is still checked at the point
     * it is produced. What is given up is the guarantee surviving INSIDE the component,
     * and that is the price of living in an ECS that owns its own storage.
     *
     * Anything added to a component from here on must be default-constructible. That rules
     * out not_null, references, and any type whose default constructor is deleted.
     */
    GridCell* pCell{nullptr};
};