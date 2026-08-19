#pragma once

#include "Game/Animation/PuppetDriver.h"

// A puppet moved and animated by the mod-owned PuppetDriver instead of the legacy
// engine-attached controller. Record-agnostic; the engine cannot tear it off. Shared
// ptr because flecs components must be copyable.
struct DriverComponent
{
    std::shared_ptr<PuppetDriver> Driver;
};
