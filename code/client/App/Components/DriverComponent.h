#pragma once

#include <chrono>

#include "Game/Animation/PuppetDriver.h"

// A puppet moved and animated by the mod-owned PuppetDriver instead of the legacy
// engine-attached controller. Record-agnostic; the engine cannot tear it off. Shared
// ptr because flecs components must be copyable.
struct DriverComponent
{
    std::shared_ptr<PuppetDriver> Driver;

    // Driver stand-down deadline. A vehicle exit rebuilds the puppet's components
    // over several engine frames; every one of the 2026-08-19 crashes happened 0-15s
    // after a remote driver-puppet's exit, with the driver writing transforms and
    // re-binding into that rebuild. While now() is before this point, the drive path
    // does nothing at all.
    std::chrono::steady_clock::time_point SuppressUntil{};
};
