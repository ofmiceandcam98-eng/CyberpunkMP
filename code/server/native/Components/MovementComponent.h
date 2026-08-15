#pragma once

struct MovementComponent
{
    glm::vec3 Position;
    glm::vec3 Rotation;
    float Velocity;
    uint64_t Tick;

    // Counts updates for this entity, so replication can send only every Nth one to
    // players who are far away. A plain counter rather than a timestamp: it is the same
    // for every recipient, so no per-pair state has to be kept anywhere.
    uint32_t Sequence{0};

    static void Register(flecs::world& aWorld);
};