#pragma once

struct VehicleComponent
{
    uint64_t TweakDBID{0};

    /**
     * Which PERSISTENT vehicle this live entity is, or empty for a car nobody owns.
     *
     * The model is not an identity. TweakDBID says "a Hella"; it cannot say "YOUR Hella",
     * and until this field existed nothing could - the live world and VehicleStore never
     * referred to each other at all, so a spawned car had no way back to the record that
     * remembers who bought it and what its plate is. Everything that has to outlive a
     * single drive - garages, damage that persists, theft, cargo, recovery - was blocked
     * on that one missing link rather than on the features themselves.
     *
     * Empty is a normal, common state, not a failure: a car boosted off the street belongs
     * to no record and should not be invented one. Only bind what someone actually owns.
     *
     * A string rather than an index, matching VehicleRecord::Id - a reference into the
     * store's vector would dangle the moment Create() reallocates, which the store's own
     * header warns about.
     */
    std::string RecordId;

    static void Register(flecs::world& aWorld);
};