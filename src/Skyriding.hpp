// Read-only diagnostics for the native WarcraftXL Skyriding controller.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include <array>
#include <cstdint>

namespace wxl_skyriding
{
    struct Diagnostics
    {
        bool playerAvailable = false;
        bool active = false;
        bool grounded = false;
        bool flying = false;
        bool falling = false;
        bool braking = false;
        bool stalled = false;
        bool forcedForward = false;
        bool impulseActive = false;
        bool launchActive = false;
        uint32_t movementFlags = 0;
        uint32_t impulseSequence = 0;
        uint32_t launchElapsedMs = 0;
        uint32_t launchDurationMs = 0;
        float facing = 0.0f;
        float pitch = 0.0f;
        float flightRate = 0.0f;
        float coastYardsPerSec = 0.0f;
        float launchProgress = 0.0f;
        float launchForwardSpeed = 0.0f;
        float launchForwardYards = 0.0f;
        float launchRiseYards = 0.0f;
        std::array<float, 3> position{};
        std::array<float, 3> impulseVelocity{};
        std::array<float, 2> launchHeading{};
        int forcedAnimation = -1;
        int boneAnimation = -1;
        int cruiseAnimation = -1;
        const char* phase = "idle";
        const char* lastCommand = "";
    };

    Diagnostics GetDiagnostics() noexcept;
}
