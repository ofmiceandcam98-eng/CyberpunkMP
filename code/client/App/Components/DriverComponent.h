#pragma once

#include <chrono>

#include <glm/glm.hpp>

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

    // Exit-ease. When the stand-down above lapses, the drive path resumes by writing the
    // LIVE position - and after up to 2s frozen on a remote that left a MOVING car, that
    // first write is a visible teleport (the "exit-grace pop"). These fields blend the
    // frozen position to the live one over a short window on the RESUME frames, which are
    // already past the component rebuild the grace guards - so nothing here writes into
    // the crash window. Mirrors InterpolationComponent's dead-reckoning recovery, which
    // corrects a late sample over a window instead of lurching to the wire position.
    glm::vec3 LastPosition{};
    bool HasLastPosition{false};
    glm::vec3 ExitEaseFrom{};
    std::chrono::steady_clock::time_point ExitEaseUntil{};
};
