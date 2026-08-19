#pragma once

struct MultiMovementController;
struct SpawningComponent
{
    Red::EntityID Id;
    MultiMovementController* Controller{nullptr};

    // Promote onto the PuppetDriver path (player records, or -puppet-driver-all)
    // instead of waiting for the legacy idle-controller hook - which never fires for
    // player records at all.
    bool UsesDriver{false};
};