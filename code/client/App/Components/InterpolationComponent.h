#pragma once

struct InterpolationComponent
{
    struct Timepoint
    {
        glm::vec3 Position{};
        glm::vec3 Rotation{};
        float Velocity{0.f};
        uint64_t Tick{0};
        int32_t CellX{0};
        int32_t CellY{0};
        uint32_t Sequence{0};
        uint32_t AuthorityEpoch{0};
        bool Correction{false};

        // The mover's PlayerStateMachine states (gamePSMLocomotionStates /
        // gamePSMUpperBodyStates), carried per-sample so the pose travels with the
        // position it belongs to rather than flickering to the newest packet.
        uint32_t Locomotion{0};
        uint32_t UpperBody{0};
    };

    List<Timepoint> TimePoints{};

    // The network sample BEHIND render time. Interpolation runs between this and the
    // first sample ahead of it, which is what makes movement come out at a constant speed
    // across each segment.
    //
    // This used to be overwritten every frame with the pose we had just drawn, so each
    // frame started from wherever the last one finished and covered a fraction of the
    // REMAINING distance. That accelerates into every target and then starts over at the
    // next one, which is what Cam and his friends were seeing as other players
    // "teleporting" rather than walking.
    Timepoint PreviousFrame{};
    bool HasPrevious{false};

    // Render time as of the last frame. The vehicle path needs a frame delta, which used
    // to fall out of PreviousFrame back when that was rewritten every frame.
    //
    // int64, not float. Ticks are milliseconds since the epoch - around 1.787e12 - and a
    // 32-bit float has 24 bits of mantissa, so consecutive representable values that far
    // out are 131072 apart. Every tick within the same two-minute window collapsed onto
    // one value, which made every comparison against render time meaningless. See the
    // note in InterpolateEntity.
    int64_t LastRenderTick{0};
    uint32_t LastSequence{0};
    uint32_t LastAuthorityEpoch{0};
    bool HasSequence{false};
    bool HasAuthorityEpoch{false};
};