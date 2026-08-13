// Native client owner for the WarcraftXL skyriding protocol and AdvFly pose.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ExtensionApi.hpp"
#include "Skyriding.hpp"
#include "game/M2Animation.hpp"
#include "game/Movement.hpp"
#include "game/World.hpp"
#include "wxl/WxlOpcodes.h"
#include "engine/events/Event.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace ev = wxl::events;
    namespace m2animation = wxl::game::m2animation;
    namespace movement = wxl::game::movement;
    namespace world = wxl::game::world;

    namespace opcodes
    {
        constexpr uint16_t CmsgSkyriding = WXL_CMSG_SKYRIDING;
        constexpr uint16_t SmsgSkyriding = WXL_SMSG_SKYRIDING;
        constexpr uint16_t CmsgMoveAddImpulseAck = WXL_CMSG_MOVE_ADD_IMPULSE_ACK;
        constexpr uint16_t SmsgMoveAddImpulse = WXL_SMSG_MOVE_ADD_IMPULSE;
    }

    namespace network
    {
        bool RegisterClientOpcode(uint16_t opcode, const char* name)
        {
            const WXL_NetworkApi* api = wxl_skyriding::Network();
            return api && api->RegisterClientOpcode(opcode, name) != 0;
        }

        bool RegisterServerOpcode(uint16_t opcode, const char* name,
                                  WXL_NetworkPacketHandler handler, void* user)
        {
            const WXL_NetworkApi* api = wxl_skyriding::Network();
            return api && api->RegisterServerOpcode(opcode, name, handler, user) != 0;
        }

        bool Send(uint16_t opcode, std::span<const uint8_t> payload)
        {
            const WXL_NetworkApi* api = wxl_skyriding::Network();
            return api && api->Send(opcode, payload.data(),
                                    static_cast<uint32_t>(payload.size())) != 0;
        }
    }

    constexpr uint32_t kMoveForward = 0x00000001;
    constexpr uint32_t kMoveBackward = 0x00000002;
    constexpr uint32_t kMoveDisableGravity = 0x00000400;
    constexpr uint32_t kMoveFalling = 0x00001000;
    constexpr uint32_t kMoveFallingFar = 0x00002000;
    constexpr uint32_t kMovePendingStop = 0x00004000;
    constexpr uint32_t kMoveAscending = 0x00400000;
    constexpr uint32_t kMoveDescending = 0x00800000;
    constexpr uint32_t kMoveFlying = 0x02000000;
    constexpr uint32_t kForwardControlBit = 0x10;

    constexpr int kAnimForward = 1524;
    constexpr int kAnimDown = 1530;
    constexpr int kAnimForwardGlide = 1532;
    // Whirling Surge is a complete one-shot barrel roll on the mounted model.
    // RidingWyvern.m2 authors it as 1558 (2066 ms); the old three-stage path
    // only played the first 350 ms of this sequence and then returned to glide.
    constexpr int kAnimWhirlingSurge = 1558;
    constexpr int kAnimDownStart = 1678;
    constexpr int kAnimFlapBig = 1680;
    constexpr int kAnimFlapUp = 1702;
    constexpr int kAnimSlowFall = 1704;
    constexpr int kAnimForwardGlideSlow = 1722;
    constexpr int kAnimSecondFlapUp = 1726;

    constexpr DWORD kDownStartDurationMs = 1583;
    constexpr DWORD kSurgeForwardDurationMs = 2600;
    constexpr DWORD kWhirlFallbackDurationMs = 2000;
    constexpr DWORD kFlapUpDurationMs = 750;
    constexpr DWORD kSecondFlapUpDurationMs = 2083;
    constexpr DWORD kDoubleJumpWindowMs = 400;
    constexpr DWORD kTakeoffGraceMs = 400;
    constexpr DWORD kDefaultGroundLockMs = 450;
    constexpr DWORD kRecentFlyMs = 1000;
    constexpr DWORD kRecentDiveIntentMs = 1200;
    constexpr float kDiveEnterPitch = -0.35f;
    constexpr float kDiveExitPitch = -0.28f;
    constexpr float kStallSinkPerSec = 8.0f;
    constexpr float kSurgeLiftPerSec = 6.0f;
    constexpr DWORD kSurgeLiftMs = 400;
    constexpr uint32_t kTraceHitFlagsGround = 0x00000051;
    constexpr float kCollisionSkinYards = 0.45f;
    constexpr float kCollisionMaxStepYards = 28.0f;
    constexpr float kCollisionProbeZ = 1.2f;
    constexpr float kSettleYardsXY = 0.12f;
    constexpr float kSettleYardsZ = 0.16f;
    constexpr float kSoftLandMaxMoveZ = 0.55f;
    constexpr int kSettleFrames = 2;
    constexpr float kDiveLandProbeUp = 0.50f;
    constexpr float kDiveLandProbeDown = 2.50f;
    constexpr float kDiveLandContactGap = 0.65f;
    constexpr int kDiveLandConfirmFrames = 2;
    constexpr float kSlowRateEnter = 1.55f;
    constexpr float kSlowRateExit = 1.90f;
    constexpr float kBaseFlightYards = 7.0f;
    constexpr DWORD kImpulseStateDurationMs = 350;
    constexpr DWORD kSkywardLaunchDurationMs =
        kFlapUpDurationMs + kSecondFlapUpDurationMs;
    constexpr float kSkywardForwardBonusYardsPerSec = 2.0f;
    constexpr float kSkywardMinPitch = 0.48f;
    constexpr float kSkywardMaxPitch = 0.62f;
    constexpr size_t kMaxCommandBytes = 128;

    enum class AnimationPhase : uint8_t
    {
        Idle,
        Cruise,
        DiveStart,
        Dive,
        DiveExit,
        Surge,
        Whirl,
        SkywardFirst,
        SkywardSecond
    };

    const char* PhaseName(AnimationPhase phase) noexcept
    {
        switch (phase)
        {
        case AnimationPhase::Idle: return "idle";
        case AnimationPhase::Cruise: return "cruise";
        case AnimationPhase::DiveStart: return "dive start";
        case AnimationPhase::Dive: return "dive";
        case AnimationPhase::DiveExit: return "dive exit";
        case AnimationPhase::Surge: return "surge forward";
        case AnimationPhase::Whirl: return "whirling surge";
        case AnimationPhase::SkywardFirst: return "skyward flap 1";
        case AnimationPhase::SkywardSecond: return "skyward flap 2";
        }
        return "unknown";
    }

    struct State
    {
        bool active = false;
        bool classicVertical = false;
        bool grounded = false;
        bool braking = false;
        bool stalled = false;
        bool forcedForward = false;
        bool previousFlying = false;
        bool previousFalling = false;
        bool haveFacing = false;
        float lastFacing = 0.0f;
        float turnRate = 0.75f;
        float flightRate = 1.0f;
        float coastYardsPerSec = 2.5f * kBaseFlightYards;
        DWORD groundLockUntilMs = 0;
        DWORD takeoffGraceUntilMs = 0;
        DWORD aerialHaltUntilMs = 0;
        DWORD lastSpaceMs = 0;
        DWORD lastFlyingMs = 0;
        DWORD lastFallingMs = 0;
        DWORD lastDiveIntentMs = 0;
        DWORD lastPhysicsMs = 0;
        DWORD surgeLiftUntilMs = 0;
        int lastBrakeSent = -1;
        std::array<float, 3> lastPosition{};
        bool haveLastPosition = false;
        int settleFrames = 0;
        int diveLandFrames = 0;
        float lastMoveXY = -1.0f;
        float lastMoveZ = -1.0f;
        std::array<float, 3> lastCollisionPosition{};
        bool haveLastCollisionPosition = false;
        bool inDive = false;
        bool surgePitchLocked = false;
        bool skywardPitchLocked = false;
        float surgePitch = 0.0f;
        float skywardPitch = 0.0f;
        bool impulseActive = false;
        uint32_t impulseSequence = 0;
        std::array<float, 3> impulseVelocity{};
        DWORD impulseStartedMs = 0;
        bool launchActive = false;
        DWORD launchStartedMs = 0;
        DWORD launchDurationMs = 0;
        float launchProgress = 0.0f;
        float launchForwardYards = 0.0f;
        float launchRiseYards = 0.0f;
        float launchForwardSpeed = 0.0f;
        float launchPitchStart = 0.0f;
        std::array<float, 3> launchOrigin{};
        std::array<float, 2> launchHeading{};
        std::array<char, 64> lastCommand{};

        AnimationPhase phase = AnimationPhase::Idle;
        DWORD phaseUntilMs = 0;
        int forcedAnimation = -1;
        int boneAnimation = -1;
        float boneSpeed = 1.0f;
        int cruiseAnimation = kAnimForwardGlide;
        void* forcedUnit = nullptr;
        void* forcedModel = nullptr;
    };

    State g_state;
    bool g_subscribed = false;

    void* ActivePlayer() noexcept
    {
        __try
        {
            const unsigned long long guid = world::ActivePlayerGuid();
            return guid
                ? world::ResolveObject(
                    guid, world::kTypeMaskUnit | world::kTypeMaskPlayer)
                : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void* UnitModel(void* unit) noexcept
    {
        if (!unit) return nullptr;
        __try
        {
            return movement::UnitModel(unit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool ModelHasSequence(void* model, unsigned int animationId) noexcept
    {
        if (!model) return false;
        __try
        {
            return m2animation::ModelHasSequence(model, animationId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    uint32_t ModelSequenceDuration(
        void* model, unsigned int animationId) noexcept
    {
        if (!model) return 0;
        __try
        {
            return m2animation::ModelSequenceDuration(model, animationId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool ModelSupportsAdvFly(void* model) noexcept
    {
        return ModelHasSequence(model, kAnimForwardGlide);
    }

    void* AnimationModel(void* unit) noexcept
    {
        void* body = UnitModel(unit);
        if (!body) return nullptr;
        __try
        {
            void* parent = movement::ModelParent(body);
            if (parent && ModelSupportsAdvFly(parent)) return parent;
            return ModelSupportsAdvFly(body) ? body : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool OnAdvFlyMount(void* unit) noexcept
    {
        return AnimationModel(unit) != nullptr;
    }

    uint32_t& MovementFlags(void* unit)
    {
        return movement::Flags(unit);
    }

    float& Facing(void* unit)
    {
        return movement::Facing(unit);
    }

    float& Pitch(void* unit)
    {
        return movement::Pitch(unit);
    }

    float* UnitPosition(void* unit)
    {
        return movement::Position(unit);
    }

    int TraceLine(
        float* end, float* start, float* result,
        float* distanceFraction, uint32_t flags) noexcept
    {
        __try
        {
            return movement::TraceLine(
                end, start, result, distanceFraction, flags);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    bool IsFlying(void* unit) noexcept;

    bool ClampMoveAgainstWorld(const float* from, float* to)
    {
        const float dx = to[0] - from[0];
        const float dy = to[1] - from[1];
        const float dz = to[2] - from[2];
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length < 0.0001f || length > kCollisionMaxStepYards)
            return false;

        float start[3] = {
            from[0], from[1], from[2] + kCollisionProbeZ
        };
        float end[3] = {
            to[0], to[1], to[2] + kCollisionProbeZ
        };
        float hit[3]{};
        float fraction = 1.0f;
        TraceLine(end, start, hit, &fraction, kTraceHitFlagsGround);
        if (fraction >= 0.999f)
            return false;

        const float skinFraction = std::min(
            0.9f, kCollisionSkinYards / length);
        const float t = std::clamp(
            fraction - skinFraction, 0.0f, 1.0f);
        to[0] = from[0] + dx * t;
        to[1] = from[1] + dy * t;
        to[2] = from[2] + dz * t;
        return true;
    }

    void ApplyWorldCollision(void* unit)
    {
        if (!unit || !IsFlying(unit))
        {
            g_state.haveLastCollisionPosition = false;
            return;
        }

        __try
        {
            float* position = UnitPosition(unit);
            if (g_state.haveLastCollisionPosition)
            {
                float desired[3] = {
                    position[0], position[1], position[2]
                };
                ClampMoveAgainstWorld(
                    g_state.lastCollisionPosition.data(), desired);
                std::copy_n(desired, 3, position);
            }
            std::copy_n(
                position, 3, g_state.lastCollisionPosition.begin());
            g_state.haveLastCollisionPosition = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_state.haveLastCollisionPosition = false;
        }
    }

    bool IsFlying(void* unit) noexcept
    {
        if (!unit) return false;
        __try
        {
            return (MovementFlags(unit) & kMoveFlying) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsFalling(void* unit) noexcept
    {
        if (!unit) return false;
        __try
        {
            return (MovementFlags(unit) &
                (kMoveFalling | kMoveFallingFar)) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    float AnySequenceTime() noexcept
    {
        float value = 0.0f;
        const uint32_t bits = 0xFFFFFFFFu;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    void ReleaseAnimation()
    {
        g_state.phase = AnimationPhase::Idle;
        g_state.phaseUntilMs = 0;
        g_state.forcedAnimation = -1;
        g_state.boneAnimation = -1;
        g_state.boneSpeed = 1.0f;
        g_state.cruiseAnimation = kAnimForwardGlide;
        g_state.inDive = false;
        g_state.surgePitchLocked = false;
        g_state.skywardPitchLocked = false;
        g_state.surgeLiftUntilMs = 0;
        g_state.forcedUnit = nullptr;
        g_state.forcedModel = nullptr;
    }

    void PlayAnimation(
        void* unit, void* model, int animationId,
        float speed = 1.0f, float sequenceTime = -1.0f,
        bool force = false)
    {
        if (!unit || !model || animationId <= 0 ||
            !ModelSupportsAdvFly(model))
            return;

        g_state.forcedAnimation = animationId;
        const bool same = g_state.boneAnimation == animationId &&
            std::fabs(g_state.boneSpeed - speed) < 0.01f;
        if (same && !force) return;
        if (sequenceTime < 0.0f)
            sequenceTime = speed < 0.0f
                ? static_cast<float>(kDownStartDurationMs) / 1000.0f
                : AnySequenceTime();

        __try
        {
            movement::SetBoneSequence(
                unit, model, animationId, sequenceTime, speed);
            g_state.boneAnimation = animationId;
            g_state.boneSpeed = speed;
            g_state.forcedUnit = unit;
            g_state.forcedModel = model;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ReleaseAnimation();
        }
    }

    void ForceGroundPose(void* unit)
    {
        void* model = AnimationModel(unit);
        if (!unit || !model) return;
        __try
        {
            movement::SetBoneSequence(unit, model, 0, 0.0f, 1.0f);
            Pitch(unit) = 0.0f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        ReleaseAnimation();
    }

    int __cdecl ResolveAnimationOverride(
        void* unit, int, void* model, int resolved)
    {
        if (!g_state.active || g_state.forcedAnimation <= 0 ||
            unit != g_state.forcedUnit || model != g_state.forcedModel ||
            !ModelSupportsAdvFly(model))
            return -1;
        return resolved == g_state.forcedAnimation
            ? -1 : g_state.forcedAnimation;
    }

    void SetForwardControl(bool enabled) noexcept
    {
        __try
        {
            movement::SetForwardControl(enabled, kForwardControlBit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ReleaseForcedForward()
    {
        if (!g_state.forcedForward) return;
        const bool forwardHeld =
            (GetAsyncKeyState('W') & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
        if (!forwardHeld) SetForwardControl(false);
        g_state.forcedForward = false;
    }

    void StopAirborneMomentum(
        void* unit, bool preserveFalling = false)
    {
        const bool forwardHeld =
            (GetAsyncKeyState('W') & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_UP) & 0x8000) != 0;

        if (g_state.forcedForward || !forwardHeld)
            SetForwardControl(false);
        g_state.forcedForward = false;
        g_state.coastYardsPerSec = 0.0f;
        g_state.flightRate = 0.0f;
        g_state.braking = false;
        g_state.stalled = false;
        g_state.impulseActive = false;
        g_state.impulseVelocity = {};
        g_state.launchActive = false;
        g_state.launchProgress = 0.0f;

        if (unit)
        {
            __try
            {
                uint32_t& flags = MovementFlags(unit);
                flags &= ~(kMoveForward | kMoveBackward |
                    kMovePendingStop | kMoveAscending |
                    kMoveDescending | kMoveFlying |
                    kMoveDisableGravity);
                if (!preserveFalling)
                    flags &= ~(kMoveFalling | kMoveFallingFar);
                Pitch(unit) = 0.0f;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        // A physically held W becomes ordinary ground locomotion after the
        // airborne coast claim has been removed.
        if (forwardHeld)
            SetForwardControl(true);
    }

    void ApplyMovementGate(void* unit)
    {
        if (!g_state.active || g_state.classicVertical || !unit)
            return;
        if (!OnAdvFlyMount(unit) && !IsFlying(unit)) return;

        __try
        {
            uint32_t& flags = MovementFlags(unit);
            const uint32_t before = flags;
            uint32_t after =
                before & ~(kMoveAscending | kMoveDescending);
            if ((before & kMoveFlying) == 0 &&
                (before & kMoveAscending) != 0 &&
                (before & (kMoveFalling | kMoveFallingFar)) == 0)
            {
                after &= ~(kMoveFlying | kMoveFalling |
                    kMoveFallingFar | kMoveDisableGravity);
            }
            flags = after;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ApplyGroundLock(void* unit)
    {
        ReleaseForcedForward();
        ReleaseAnimation();
        if (!unit) return;
        __try
        {
            MovementFlags(unit) &= ~(kMoveFlying | kMoveDisableGravity |
                kMoveAscending | kMoveDescending);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ForceGroundFromServer()
    {
        ReleaseForcedForward();
        ReleaseAnimation();
        void* unit = ActivePlayer();
        if (unit)
        {
            __try
            {
                MovementFlags(unit) &= ~(kMoveFlying |
                    kMoveDisableGravity | kMoveAscending |
                    kMoveDescending | kMoveFalling |
                    kMoveFallingFar);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        g_state.grounded = true;
        g_state.groundLockUntilMs =
            GetTickCount() + kDefaultGroundLockMs;
        g_state.haveFacing = false;
        g_state.haveLastCollisionPosition = false;
        g_state.lastPhysicsMs = 0;
    }

    bool AbilityPhase();
    bool SendCommand(std::string_view body);
    bool GroundContactImmediatelyBelow(void* unit, float& gap);

    bool SampleSettle(void* unit)
    {
        if (!unit) return false;
        __try
        {
            float* position = UnitPosition(unit);
            float moveXY = 999.0f;
            float moveZ = 999.0f;
            if (g_state.haveLastPosition)
            {
                const float moveX =
                    position[0] - g_state.lastPosition[0];
                const float moveY =
                    position[1] - g_state.lastPosition[1];
                moveXY = std::sqrt(
                    moveX * moveX + moveY * moveY);
                moveZ = std::fabs(
                    position[2] - g_state.lastPosition[2]);
            }

            g_state.lastMoveXY = moveXY > 90.0f ? -1.0f : moveXY;
            g_state.lastMoveZ = moveZ > 90.0f ? -1.0f : moveZ;
            std::copy_n(position, 3, g_state.lastPosition.begin());
            g_state.haveLastPosition = true;

            if (moveXY < kSettleYardsXY &&
                moveZ < kSettleYardsZ)
                ++g_state.settleFrames;
            else
                g_state.settleFrames = 0;
            return g_state.settleFrames >= kSettleFrames;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_state.haveLastPosition = false;
            g_state.settleFrames = 0;
            return false;
        }
    }

    bool SoftLandZQuiet()
    {
        return g_state.haveLastPosition &&
            g_state.lastMoveZ >= 0.0f &&
            g_state.lastMoveZ < kSoftLandMaxMoveZ;
    }

    bool AcceptLanding(void* unit, bool notifyServer = true)
    {
        if (g_state.grounded) return true;
        float groundGap = 999.0f;
        if (!GroundContactImmediatelyBelow(unit, groundGap))
        {
            WLOG_INFO(
                "skyriding: rejected touchdown without terrain contact");
            return false;
        }

        const bool falling = IsFalling(unit);
        StopAirborneMomentum(unit, falling);
        ReleaseAnimation();
        g_state.grounded = true;
        g_state.groundLockUntilMs =
            GetTickCount() + kDefaultGroundLockMs;
        g_state.takeoffGraceUntilMs = 0;
        g_state.aerialHaltUntilMs = 0;
        g_state.diveLandFrames = 0;
        ApplyGroundLock(unit);
        ForceGroundPose(unit);
        if (notifyServer)
            SendCommand("LAND\t1");
        WLOG_INFO(
            "skyriding: accepted terrain touchdown gap=%.2f falling=%d",
            groundGap, falling ? 1 : 0);
        return true;
    }

    bool GroundContactImmediatelyBelow(
        void* unit, float& gap)
    {
        if (!unit) return false;
        __try
        {
            float* position = UnitPosition(unit);
            float start[3] = {
                position[0], position[1],
                position[2] + kDiveLandProbeUp
            };
            float end[3] = {
                position[0], position[1],
                position[2] - kDiveLandProbeDown
            };
            float hit[3]{};
            float fraction = 1.0f;
            TraceLine(
                end, start, hit, &fraction, kTraceHitFlagsGround);
            if (fraction < 0.0f || fraction >= 0.999f)
                return false;
            gap = fraction *
                (kDiveLandProbeUp + kDiveLandProbeDown) -
                kDiveLandProbeUp;
            return gap >= -kDiveLandProbeUp &&
                gap <= kDiveLandContactGap;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TryDiveGroundLanding(void* unit)
    {
        const DWORD now = GetTickCount();
        const bool recentDive =
            g_state.lastDiveIntentMs &&
            now - g_state.lastDiveIntentMs <=
                kRecentDiveIntentMs;
        if (!unit || !IsFlying(unit) || g_state.grounded ||
            now < g_state.takeoffGraceUntilMs ||
            AbilityPhase() || !recentDive)
        {
            g_state.diveLandFrames = 0;
            return false;
        }

        float groundGap = 999.0f;
        if (!GroundContactImmediatelyBelow(unit, groundGap))
        {
            g_state.diveLandFrames = 0;
            return false;
        }

        __try
        {
            MovementFlags(unit) &= ~(kMoveForward |
                kMoveBackward | kMovePendingStop);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        ReleaseForcedForward();
        if (++g_state.diveLandFrames >=
            kDiveLandConfirmFrames)
        {
            WLOG_INFO(
                "skyriding: dive contact gap=%.2f pitch=%.2f; touchdown",
                groundGap, Pitch(unit));
            g_state.diveLandFrames = 0;
            AcceptLanding(unit);
        }
        return true;
    }

    void ApplyCoast(void* unit)
    {
        if (!g_state.active || !unit || !IsFlying(unit))
        {
            g_state.braking = false;
            ReleaseForcedForward();
            return;
        }

        __try
        {
            uint32_t& flags = MovementFlags(unit);
            const bool backHeld =
                (GetAsyncKeyState('S') & 0x8000) != 0 ||
                (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0 ||
                (flags & kMoveBackward) != 0;
            g_state.braking =
                backHeld;

            const bool forwardHeld =
                (GetAsyncKeyState('W') & 0x8000) != 0 ||
                (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
            flags &= ~(kMoveBackward | kMovePendingStop);
            flags |= kMoveForward;
            if (g_state.braking)
                ReleaseForcedForward();
            else if (!forwardHeld)
            {
                SetForwardControl(true);
                g_state.forcedForward = true;
            }
            else
                g_state.forcedForward = false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ReleaseForcedForward();
        }
    }

    bool ApplyStallDescent(void* unit, float dt)
    {
        if (!unit || !g_state.stalled || !IsFlying(unit) ||
            dt <= 0.0f)
            return false;

        __try
        {
            float* position = UnitPosition(unit);
            const float from[3] = {
                position[0], position[1], position[2]
            };
            position[2] -= kStallSinkPerSec *
                std::clamp(dt, 0.0f, 0.1f);
            ClampMoveAgainstWorld(from, position);
            MovementFlags(unit) |= kMoveDescending;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return false;
    }

    void ApplySurgeLift(void* unit, float dt)
    {
        if (!unit || dt <= 0.0f || !IsFlying(unit))
            return;

        const DWORD now = GetTickCount();
        __try
        {
            if (g_state.surgePitchLocked)
                Pitch(unit) = g_state.surgePitch;
            if (!g_state.surgeLiftUntilMs ||
                now >= g_state.surgeLiftUntilMs)
            {
                if (now >= g_state.surgeLiftUntilMs)
                {
                    g_state.surgeLiftUntilMs = 0;
                    g_state.surgePitchLocked = false;
                }
                return;
            }

            float* position = UnitPosition(unit);
            const float from[3] = {
                position[0], position[1], position[2]
            };
            position[2] += kSurgeLiftPerSec *
                std::clamp(dt, 0.0f, 0.1f);
            ClampMoveAgainstWorld(from, position);
            Pitch(unit) = g_state.surgePitch;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_state.surgeLiftUntilMs = 0;
            g_state.surgePitchLocked = false;
        }
    }

    float NormalizeAngle(float value)
    {
        constexpr float pi = 3.14159265f;
        constexpr float twoPi = 6.2831853f;
        while (value > pi) value -= twoPi;
        while (value < -pi) value += twoPi;
        return value;
    }

    void ApplyTurnInertia(void* unit)
    {
        if (!unit || !IsFlying(unit) || g_state.turnRate >= 0.999f)
        {
            if (unit)
            {
                __try
                {
                    g_state.lastFacing = Facing(unit);
                    g_state.haveFacing = true;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
            }
            return;
        }

        __try
        {
            float& facing = Facing(unit);
            if (!g_state.haveFacing)
            {
                g_state.lastFacing = facing;
                g_state.haveFacing = true;
                return;
            }
            const float delta =
                NormalizeAngle(facing - g_state.lastFacing);
            facing = g_state.lastFacing + delta * g_state.turnRate;
            g_state.lastFacing = facing;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_state.haveFacing = false;
        }
    }

    void BeginPhase(
        AnimationPhase phase, DWORD durationMs)
    {
        g_state.phase = phase;
        g_state.phaseUntilMs =
            durationMs ? GetTickCount() + durationMs : 0;
    }

    bool AbilityPhase()
    {
        return g_state.phase == AnimationPhase::Surge ||
            g_state.phase == AnimationPhase::Whirl ||
            g_state.phase == AnimationPhase::SkywardFirst ||
            g_state.phase == AnimationPhase::SkywardSecond ||
            g_state.phase == AnimationPhase::DiveStart ||
            g_state.phase == AnimationPhase::DiveExit;
    }

    int SupportedOr(
        void* model, int preferred, int fallback) noexcept
    {
        return ModelHasSequence(
            model, static_cast<unsigned int>(preferred))
            ? preferred : fallback;
    }

    void BeginFlap(int kind)
    {
        if (g_state.grounded) return;
        void* unit = ActivePlayer();
        void* model = AnimationModel(unit);
        if (!unit) return;

        g_state.stalled = false;
        g_state.boneAnimation = -1;
        if (kind == 1)
        {
            __try
            {
                g_state.skywardPitch = Pitch(unit);
                g_state.skywardPitchLocked = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_state.skywardPitchLocked = false;
            }
            g_state.takeoffGraceUntilMs =
                GetTickCount() + kTakeoffGraceMs;
            if (model)
                PlayAnimation(
                    unit, model, kAnimFlapUp, 1.0f, 0.0f, true);
            BeginPhase(
                AnimationPhase::SkywardFirst,
                kFlapUpDurationMs);
        }
        else if (kind == 2)
        {
            const int animation = SupportedOr(
                model, kAnimWhirlingSurge, kAnimFlapBig);
            if (model)
                PlayAnimation(
                    unit, model, animation, 1.0f, 0.0f, true);
            uint32_t durationMs = ModelSequenceDuration(
                model, static_cast<uint32_t>(animation));
            if (!durationMs)
                durationMs = kWhirlFallbackDurationMs;
            durationMs = std::clamp<uint32_t>(durationMs, 250, 15000);
            WLOG_INFO(
                "skyriding: whirling animation=%d duration=%u ms",
                animation, durationMs);
            BeginPhase(AnimationPhase::Whirl, durationMs);
        }
        else
        {
            __try
            {
                g_state.surgePitch = Pitch(unit);
                g_state.surgePitchLocked = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_state.surgePitchLocked = false;
            }
            g_state.surgeLiftUntilMs =
                GetTickCount() + kSurgeLiftMs;
            const int animation = kAnimFlapBig;
            if (model)
                PlayAnimation(
                    unit, model, animation, 1.0f, -1.0f, true);
            BeginPhase(
                AnimationPhase::Surge,
                kSurgeForwardDurationMs);
        }
    }

    int CruiseAnimation()
    {
        if (g_state.stalled)
        {
            g_state.cruiseAnimation = kAnimSlowFall;
            return kAnimSlowFall;
        }

        float rate = g_state.coastYardsPerSec / kBaseFlightYards;
        if (rate < 0.1f) rate = 0.1f;
        if (g_state.cruiseAnimation == kAnimSlowFall)
            g_state.cruiseAnimation = kAnimForwardGlideSlow;
        if (g_state.cruiseAnimation == kAnimForwardGlideSlow)
        {
            if (rate >= kSlowRateExit)
                g_state.cruiseAnimation = kAnimForwardGlide;
        }
        else if (rate < kSlowRateEnter)
            g_state.cruiseAnimation = kAnimForwardGlideSlow;
        else
            g_state.cruiseAnimation = kAnimForwardGlide;
        return g_state.cruiseAnimation;
    }

    void TickAnimation(void* unit, void* model)
    {
        if (!unit || !model) return;
        const DWORD now = GetTickCount();

        if (g_state.phase == AnimationPhase::Surge)
        {
            PlayAnimation(
                unit, model, kAnimFlapBig);
            if (now >= g_state.phaseUntilMs)
            {
                g_state.surgePitchLocked = false;
                g_state.surgeLiftUntilMs = 0;
                PlayAnimation(
                    unit, model, kAnimForwardGlide);
                BeginPhase(AnimationPhase::Cruise, 0);
            }
            return;
        }
        else if (g_state.phase == AnimationPhase::Whirl)
        {
            PlayAnimation(
                unit, model,
                SupportedOr(model, kAnimWhirlingSurge, kAnimFlapBig));
            if (now < g_state.phaseUntilMs) return;
            g_state.boneAnimation = -1;
            PlayAnimation(
                unit, model, kAnimForwardGlide,
                1.0f, 0.0f, true);
            BeginPhase(AnimationPhase::Cruise, 0);
            return;
        }
        else if (g_state.phase == AnimationPhase::SkywardFirst)
        {
            if (g_state.skywardPitchLocked)
            {
                __try
                {
                    Pitch(unit) = g_state.skywardPitch;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
            }
            PlayAnimation(unit, model, kAnimFlapUp);
            if (now < g_state.phaseUntilMs) return;
            g_state.boneAnimation = -1;
            PlayAnimation(
                unit, model, kAnimSecondFlapUp,
                1.0f, 0.0f, true);
            BeginPhase(
                AnimationPhase::SkywardSecond,
                kSecondFlapUpDurationMs);
            return;
        }
        else if (g_state.phase == AnimationPhase::SkywardSecond)
        {
            if (g_state.skywardPitchLocked)
            {
                __try
                {
                    Pitch(unit) = g_state.skywardPitch;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
            }
            PlayAnimation(unit, model, kAnimSecondFlapUp);
            if (now >= g_state.phaseUntilMs)
            {
                g_state.skywardPitchLocked = false;
                PlayAnimation(
                    unit, model, kAnimForwardGlide);
                BeginPhase(AnimationPhase::Cruise, 0);
            }
            return;
        }

        float pitch = 0.0f;
        __try
        {
            pitch = Pitch(unit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        if (!g_state.inDive && pitch < kDiveEnterPitch &&
            !AbilityPhase())
        {
            g_state.inDive = true;
            g_state.boneAnimation = -1;
            PlayAnimation(
                unit, model, kAnimDownStart,
                1.0f, 0.0f, true);
            BeginPhase(
                AnimationPhase::DiveStart,
                kDownStartDurationMs);
            return;
        }
        else if (g_state.inDive && pitch > kDiveExitPitch &&
            (g_state.phase == AnimationPhase::Dive ||
             g_state.phase == AnimationPhase::DiveStart))
        {
            g_state.boneAnimation = -1;
            PlayAnimation(
                unit, model, kAnimDownStart, -1.0f,
                static_cast<float>(kDownStartDurationMs) / 1000.0f,
                true);
            BeginPhase(
                AnimationPhase::DiveExit,
                kDownStartDurationMs);
        }

        if (g_state.phase == AnimationPhase::DiveStart)
        {
            PlayAnimation(unit, model, kAnimDownStart);
            if (now < g_state.phaseUntilMs)
            {
                return;
            }
            g_state.boneAnimation = -1;
            PlayAnimation(
                unit, model, kAnimDown,
                1.0f, 0.0f, true);
            BeginPhase(AnimationPhase::Dive, 0);
            return;
        }

        if (g_state.phase == AnimationPhase::Dive)
        {
            PlayAnimation(unit, model, kAnimDown);
            return;
        }

        if (g_state.phase == AnimationPhase::DiveExit)
        {
            PlayAnimation(
                unit, model, kAnimDownStart, -1.0f,
                static_cast<float>(kDownStartDurationMs) / 1000.0f);
            if (now < g_state.phaseUntilMs)
                return;
            g_state.inDive = false;
            g_state.boneAnimation = -1;
            PlayAnimation(
                unit, model, kAnimForwardGlide,
                1.0f, 0.0f, true);
            BeginPhase(AnimationPhase::Cruise, 0);
            return;
        }

        PlayAnimation(
            unit, model, CruiseAnimation(), 1.0f, 0.0f);
        if (g_state.phase != AnimationPhase::Cruise)
            BeginPhase(AnimationPhase::Cruise, 0);
    }

    void ResetState(bool keepConfiguration = true)
    {
        const bool classicVertical = g_state.classicVertical;
        const float turnRate = g_state.turnRate;
        ReleaseForcedForward();
        ReleaseAnimation();
        g_state = {};
        if (keepConfiguration)
        {
            g_state.classicVertical = classicVertical;
            g_state.turnRate = turnRate;
        }
        g_state.flightRate = 1.0f;
        g_state.coastYardsPerSec = 2.5f * kBaseFlightYards;
        g_state.lastBrakeSent = -1;
    }

    void SetMode(bool active)
    {
        const bool transientAbilityDrop = AbilityPhase();
        if (!active && g_state.active &&
            (GetTickCount() < g_state.takeoffGraceUntilMs ||
             transientAbilityDrop))
        {
            void* unit = ActivePlayer();
            if (unit && OnAdvFlyMount(unit))
            {
                // KnockbackFrom briefly makes the server's mount candidate
                // test false during Skyward and Whirling Surge. MODE 1
                // follows in the same movement transition; tearing down here
                // interrupts the active flap/roll and restarts glide ownership.
                WLOG_INFO(
                    "skyriding: ignored transient MODE 0 during %s",
                    transientAbilityDrop
                        ? PhaseName(g_state.phase) : "takeoff grace");
                return;
            }
        }

        if (g_state.active == active)
        {
            if (!active)
            {
                g_state.grounded = false;
                g_state.groundLockUntilMs = 0;
            }
            return;
        }

        const bool classicVertical = g_state.classicVertical;
        const float turnRate = g_state.turnRate;
        ResetState(false);
        g_state.classicVertical = classicVertical;
        g_state.turnRate = turnRate;
        g_state.active = active;
        if (active)
        {
            void* unit = ActivePlayer();
            g_state.previousFlying = IsFlying(unit);
            g_state.previousFalling = IsFalling(unit);
        }
        WLOG_INFO(
            "skyriding: native mode %s", active ? "enabled" : "disabled");
    }

    bool SendCommand(std::string_view body)
    {
        if (body.empty() || body.size() > kMaxCommandBytes)
            return false;
        std::vector<uint8_t> payload(sizeof(uint32_t) + body.size());
        const uint32_t length = static_cast<uint32_t>(body.size());
        std::memcpy(payload.data(), &length, sizeof(length));
        std::memcpy(
            payload.data() + sizeof(length), body.data(), body.size());
        return network::Send(opcodes::CmsgSkyriding, payload);
    }

    struct Tokens
    {
        std::array<std::string_view, 8> values{};
        size_t count = 0;
    };

    Tokens Split(std::string_view body)
    {
        Tokens result;
        size_t begin = 0;
        while (result.count < result.values.size())
        {
            const size_t end = body.find('\t', begin);
            result.values[result.count++] = body.substr(
                begin, end == std::string_view::npos
                    ? body.size() - begin : end - begin);
            if (end == std::string_view::npos) break;
            begin = end + 1;
        }
        return result;
    }

    float ParseFloat(std::string_view value, float fallback = 0.0f)
    {
        if (value.empty() || value.size() > 32) return fallback;
        char buffer[33]{};
        std::memcpy(buffer, value.data(), value.size());
        char* end = nullptr;
        const float parsed = std::strtof(buffer, &end);
        return end && *end == '\0' && std::isfinite(parsed)
            ? parsed : fallback;
    }

    uint32_t ParseUnsigned(
        std::string_view value, uint32_t fallback = 0)
    {
        if (value.empty() || value.size() > 10) return fallback;
        char buffer[11]{};
        std::memcpy(buffer, value.data(), value.size());
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(buffer, &end, 10);
        return end && *end == '\0'
            ? static_cast<uint32_t>(parsed) : fallback;
    }

    void ApplyGroundLockMessage(uint32_t durationMs)
    {
        if (!durationMs)
        {
            g_state.grounded = false;
            g_state.groundLockUntilMs = 0;
            g_state.takeoffGraceUntilMs =
                GetTickCount() + kTakeoffGraceMs;
            g_state.haveLastPosition = false;
            g_state.settleFrames = 0;
            g_state.diveLandFrames = 0;
            return;
        }

        ForceGroundFromServer();
        g_state.groundLockUntilMs =
            GetTickCount() + std::max(
                durationMs, uint32_t{kDefaultGroundLockMs});
    }

    void HandleCommand(std::string_view body)
    {
        const size_t commandBytes = std::min(
            body.size(), g_state.lastCommand.size() - 1);
        std::memcpy(
            g_state.lastCommand.data(), body.data(), commandBytes);
        g_state.lastCommand[commandBytes] = '\0';

        const Tokens tokens = Split(body);
        if (!tokens.count || tokens.values[0].empty()) return;
        const std::string_view command = tokens.values[0];

        if (command == "MODE" && tokens.count >= 2)
            SetMode(ParseUnsigned(tokens.values[1]) != 0);
        else if (command == "VERT" && tokens.count >= 2)
            g_state.classicVertical =
                ParseUnsigned(tokens.values[1]) != 0;
        else if (command == "TURN" && tokens.count >= 2)
            g_state.turnRate = std::clamp(
                ParseFloat(tokens.values[1], 0.75f), 0.05f, 1.0f);
        else if (command == "RATE" && tokens.count >= 2)
        {
            g_state.flightRate = ParseFloat(tokens.values[1]);
            if (g_state.grounded)
            {
                g_state.flightRate = 0.0f;
                g_state.stalled = false;
            }
            else if (g_state.flightRate <= -1.0f)
            {
                g_state.aerialHaltUntilMs = GetTickCount() + 4000;
                g_state.stalled = false;
            }
            else
                g_state.stalled = g_state.flightRate < 0.0f;
        }
        else if (command == "SPD" && tokens.count >= 5)
        {
            const float speed = ParseFloat(tokens.values[4]);
            if (speed > 0.0f && !g_state.grounded)
                g_state.coastYardsPerSec =
                    std::clamp(speed, 2.0f, 120.0f);
            g_state.stalled =
                tokens.values[3] == "stall";
        }
        else if (command == "ANIM" && tokens.count >= 2)
        {
            if (!g_state.active) SetMode(true);
            g_state.grounded = false;
            g_state.haveLastPosition = false;
            g_state.settleFrames = 0;
            g_state.diveLandFrames = 0;
            BeginFlap(static_cast<int>(
                ParseUnsigned(tokens.values[1])));
        }
        else if (command == "LAND")
            ForceGroundFromServer();
        else if (command == "GLOCK" && tokens.count >= 2)
            ApplyGroundLockMessage(
                ParseUnsigned(tokens.values[1]));

        if (command != "RATE" && command != "SPD" &&
            command != "CHG" && command != "AUX")
        {
            WLOG_INFO(
                "skyriding: rx %.*s",
                static_cast<int>(body.size()), body.data());
        }
    }

    void __cdecl OnPacket(const uint8_t* data, uint32_t size, void*)
    {
        const std::span<const uint8_t> payload(data, size);
        if (payload.size() < sizeof(uint32_t) ||
            payload.size() > sizeof(uint32_t) + kMaxCommandBytes)
        {
            WLOG_WARN(
                "skyriding: rejected packet bytes=%zu", payload.size());
            return;
        }
        uint32_t length = 0;
        std::memcpy(&length, payload.data(), sizeof(length));
        if (!length || length > kMaxCommandBytes ||
            payload.size() != sizeof(length) + length)
        {
            WLOG_WARN(
                "skyriding: rejected packet length=%u bytes=%zu",
                length, payload.size());
            return;
        }
        HandleCommand(std::string_view(
            reinterpret_cast<const char*>(
                payload.data() + sizeof(length)), length));
    }

    struct MoveAddImpulsePacket
    {
        uint64_t moverGuid = 0;
        uint32_t sequence = 0;
        std::array<float, 3> direction{};
    };
    static_assert(sizeof(MoveAddImpulsePacket) == 24);

    bool IsSkywardImpulse(
        const MoveAddImpulsePacket& packet) noexcept
    {
        const float horizontal = std::hypot(
            packet.direction[0], packet.direction[1]);
        const bool skywardPhase =
            g_state.phase == AnimationPhase::SkywardFirst ||
            g_state.phase == AnimationPhase::SkywardSecond;
        // The phase is the primary discriminator. The constrained fallback
        // keeps compatibility with a server that emitted the impulse just
        // before ANIM without mistaking Surge/Whirling for Skyward.
        return packet.direction[2] > 0.0f &&
            (skywardPhase ||
             (packet.direction[2] >= 20.0f && horizontal <= 16.0f));
    }

    void BeginSkywardLaunch(
        void* unit, const MoveAddImpulsePacket& packet)
    {
        if (!unit) return;

        const DWORD now = GetTickCount();
        const float durationSec =
            static_cast<float>(kSkywardLaunchDurationMs) / 1000.0f;
        const float headingLength = std::hypot(
            packet.direction[0], packet.direction[1]);

        g_state.impulseActive = false;
        g_state.impulseVelocity = {};
        g_state.launchActive = true;
        g_state.launchStartedMs = now;
        g_state.launchDurationMs = kSkywardLaunchDurationMs;
        g_state.launchProgress = 0.0f;
        // The server raises MOVE_FLIGHT by a small amount before this packet.
        // Keep that native speed instead of integrating packet velocity or
        // writing coordinates. The packet's XY vector only freezes heading.
        g_state.launchForwardSpeed =
            std::max(2.0f, g_state.coastYardsPerSec) +
            kSkywardForwardBonusYardsPerSec;
        g_state.launchForwardYards =
            g_state.launchForwardSpeed * durationSec;

        __try
        {
            float* position = UnitPosition(unit);
            std::copy_n(position, 3, g_state.launchOrigin.begin());
            if (headingLength > 0.1f)
            {
                g_state.launchHeading[0] =
                    packet.direction[0] / headingLength;
                g_state.launchHeading[1] =
                    packet.direction[1] / headingLength;
            }
            else
            {
                const float facing = Facing(unit);
                g_state.launchHeading[0] = std::cos(facing);
                g_state.launchHeading[1] = std::sin(facing);
            }

            g_state.launchPitchStart = std::clamp(
                std::max(Pitch(unit), kSkywardMinPitch),
                kSkywardMinPitch, kSkywardMaxPitch);
            // The eased pitch below spends roughly half of the launch near
            // this angle and then blends to level glide.
            g_state.launchRiseYards =
                g_state.launchForwardYards *
                std::sin(g_state.launchPitchStart) * 0.62f;

            uint32_t& flags = MovementFlags(unit);
            flags &= ~(kMoveBackward | kMovePendingStop | kMoveAscending |
                kMoveDescending | kMoveFalling | kMoveFallingFar);
            flags |= kMoveForward | kMoveFlying | kMoveDisableGravity;
            Facing(unit) = std::atan2(
                g_state.launchHeading[1], g_state.launchHeading[0]);
            Pitch(unit) = g_state.launchPitchStart;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_state.launchActive = false;
            return;
        }

        const bool forwardHeld =
            (GetAsyncKeyState('W') & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
        if (!forwardHeld)
        {
            SetForwardControl(true);
            g_state.forcedForward = true;
        }
        WLOG_INFO(
            "skyriding: fixed Skyward launch seq=%u heading=(%.3f,%.3f) "
            "speed=%.2f predicted=(%.2f forward, %.2f rise) duration=%u",
            packet.sequence,
            g_state.launchHeading[0], g_state.launchHeading[1],
            g_state.launchForwardSpeed,
            g_state.launchForwardYards, g_state.launchRiseYards,
            g_state.launchDurationMs);
    }

    void __cdecl OnMoveAddImpulse(
        const uint8_t* data, uint32_t size, void*)
    {
        const std::span<const uint8_t> payload(data, size);
        if (payload.size() != sizeof(MoveAddImpulsePacket))
        {
            WLOG_WARN(
                "skyriding: rejected MOVE_ADD_IMPULSE bytes=%zu",
                payload.size());
            return;
        }

        MoveAddImpulsePacket packet;
        std::memcpy(&packet, payload.data(), sizeof(packet));
        if (!packet.moverGuid ||
            packet.moverGuid != world::ActivePlayerGuid() ||
            !std::ranges::all_of(packet.direction,
                [](float value) { return std::isfinite(value); }))
        {
            WLOG_WARN(
                "skyriding: rejected MOVE_ADD_IMPULSE guid=%llu seq=%u",
                static_cast<unsigned long long>(packet.moverGuid),
                packet.sequence);
            return;
        }

        void* unit = ActivePlayer();
        if (!unit || !OnAdvFlyMount(unit)) return;

        g_state.active = true;
        g_state.grounded = false;
        g_state.groundLockUntilMs = 0;
        g_state.haveFacing = false;
        g_state.haveLastPosition = false;
        g_state.settleFrames = 0;
        g_state.diveLandFrames = 0;
        g_state.takeoffGraceUntilMs =
            GetTickCount() + kTakeoffGraceMs;
        g_state.impulseActive = true;
        g_state.impulseSequence = packet.sequence;
        g_state.impulseStartedMs = GetTickCount();
        const bool skyward = IsSkywardImpulse(packet);
        if (skyward)
            BeginSkywardLaunch(unit, packet);
        else
        {
            for (size_t i = 0; i < packet.direction.size(); ++i)
                g_state.impulseVelocity[i] += packet.direction[i];
        }

        __try
        {
            uint32_t& flags = MovementFlags(unit);
            flags &= ~(kMoveFalling | kMoveFallingFar |
                kMoveAscending | kMoveDescending |
                kMoveBackward | kMovePendingStop);
            flags |= kMoveFlying | kMoveDisableGravity;
            if (!skyward)
                flags |= kMoveForward;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_state.impulseActive = false;
            return;
        }

        std::array<uint8_t, sizeof(uint64_t) + sizeof(uint32_t)> ack{};
        std::memcpy(
            ack.data(), &packet.moverGuid, sizeof(packet.moverGuid));
        std::memcpy(
            ack.data() + sizeof(packet.moverGuid),
            &packet.sequence, sizeof(packet.sequence));
        network::Send(
            opcodes::CmsgMoveAddImpulseAck,
            std::span<const uint8_t>(ack));
        WLOG_INFO(
            "skyriding: MOVE_ADD_IMPULSE seq=%u xyz=(%.2f,%.2f,%.2f)",
            packet.sequence, packet.direction[0],
            packet.direction[1], packet.direction[2]);
    }

    void ApplyMoveImpulse(void* unit, float dt)
    {
        if (!unit || dt <= 0.0f)
            return;

        const DWORD now = GetTickCount();
        if (g_state.launchActive)
        {
            const DWORD elapsed =
                now - g_state.launchStartedMs;
            const float t = std::clamp(
                static_cast<float>(elapsed) /
                    static_cast<float>(std::max<DWORD>(
                        1, g_state.launchDurationMs)),
                0.0f, 1.0f);
            // Preserve native flight movement and its ordinary movement
            // packets. Lock the cast-facing and ease upward pitch to level
            // flight; never write UnitPosition here.
            const float pitchProgress = t <= 0.28f
                ? 1.0f
                : 1.0f - std::clamp(
                    (t - 0.28f) / 0.72f, 0.0f, 1.0f);
            const float smoothPitch =
                pitchProgress * pitchProgress *
                (3.0f - 2.0f * pitchProgress);
            __try
            {
                Facing(unit) = std::atan2(
                    g_state.launchHeading[1],
                    g_state.launchHeading[0]);
                Pitch(unit) =
                    g_state.launchPitchStart * smoothPitch;
                uint32_t& flags = MovementFlags(unit);
                flags &= ~(kMoveBackward | kMovePendingStop | kMoveAscending |
                    kMoveDescending | kMoveFalling | kMoveFallingFar);
                flags |= kMoveForward | kMoveFlying | kMoveDisableGravity;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_state.launchActive = false;
                return;
            }

            g_state.launchProgress = t;
            if (t >= 1.0f)
            {
                g_state.launchActive = false;
                __try
                {
                    Pitch(unit) = 0.0f;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
                g_state.haveFacing = false;
                WLOG_INFO(
                    "skyriding: fixed Skyward launch complete seq=%u",
                    g_state.impulseSequence);
            }
            return;
        }

        if (!g_state.impulseActive)
            return;

        if (now - g_state.impulseStartedMs >
            kImpulseStateDurationMs)
        {
            g_state.impulseActive = false;
            g_state.impulseVelocity = {};
            return;
        }

        // Surge and Whirling already update MOVE_FLIGHT server-side. Retain
        // their vector briefly for diagnostics, but let native forward flight
        // move and report the player instead of integrating coordinates here.
    }

    void RestoreAirborneFlight(void* unit)
    {
        if (!unit || IsFalling(unit))
            return;

        __try
        {
            uint32_t& flags = MovementFlags(unit);
            flags &= ~(kMoveBackward | kMovePendingStop |
                kMoveAscending | kMoveDescending);
            flags |= kMoveFlying | kMoveDisableGravity | kMoveForward;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }

        const bool forwardHeld =
            (GetAsyncKeyState('W') & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
        if (!forwardHeld)
        {
            SetForwardControl(true);
            g_state.forcedForward = true;
        }
    }

    void Tick(float dt)
    {
        void* unit = ActivePlayer();
        if (!unit)
        {
            ReleaseForcedForward();
            ReleaseAnimation();
            g_state.haveLastCollisionPosition = false;
            return;
        }

        ApplyMovementGate(unit);
        if (!g_state.active || !OnAdvFlyMount(unit))
        {
            ReleaseForcedForward();
            ReleaseAnimation();
            g_state.haveFacing = false;
            g_state.haveLastCollisionPosition = false;
            g_state.lastPhysicsMs = 0;
            return;
        }

        const bool flying = IsFlying(unit);
        const bool falling = IsFalling(unit);
        const DWORD now = GetTickCount();
        if (falling)
            g_state.lastFallingMs = now;
        const bool recentFall = g_state.lastFallingMs &&
            now - g_state.lastFallingMs < kRecentFlyMs;
        const bool holdGround = g_state.grounded &&
            g_state.groundLockUntilMs &&
            now < g_state.groundLockUntilMs;

        if (flying && !g_state.previousFlying)
        {
            g_state.grounded = false;
            g_state.groundLockUntilMs = 0;
        }

        if (!flying)
            ReleaseForcedForward();

        if (holdGround && !flying && !falling && !recentFall)
        {
            ReleaseAnimation();
            g_state.previousFlying = false;
            g_state.previousFalling = false;
            return;
        }
        if (flying || falling || recentFall)
        {
            g_state.grounded = false;
            g_state.groundLockUntilMs = 0;
        }

        if (!flying && !falling)
        {
            g_state.haveFacing = false;
            g_state.haveLastCollisionPosition = false;
            g_state.lastPhysicsMs = 0;
            ReleaseAnimation();
            g_state.previousFlying = false;
            g_state.previousFalling = false;
            return;
        }

        if (flying)
        {
            ApplyWorldCollision(unit);
            ApplyCoast(unit);
            ApplyStallDescent(unit, dt);
            ApplySurgeLift(unit, dt);
            ApplyTurnInertia(unit);
        }

        void* model = AnimationModel(unit);
        if (!model)
        {
            g_state.previousFlying = flying;
            g_state.previousFalling = falling;
            return;
        }

        float pitch = 0.0f;
        __try
        {
            pitch = Pitch(unit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        if (flying && !g_state.previousFlying &&
            !AbilityPhase() &&
            (falling || recentFall || pitch < kDiveEnterPitch))
        {
            g_state.inDive = true;
            g_state.boneAnimation = -1;
            PlayAnimation(
                unit, model, kAnimDownStart,
                1.0f, 0.0f, true);
            BeginPhase(
                AnimationPhase::DiveStart,
                kDownStartDurationMs);
        }

        if (flying || (falling && AbilityPhase()))
            TickAnimation(unit, model);
        else if (falling && recentFall)
        {
            if (!g_state.inDive)
            {
                g_state.inDive = true;
                g_state.boneAnimation = -1;
                PlayAnimation(
                    unit, model, kAnimDownStart,
                    1.0f, 0.0f, true);
                BeginPhase(
                    AnimationPhase::DiveStart,
                    kDownStartDurationMs);
            }
            TickAnimation(unit, model);
        }
        else
            ReleaseAnimation();

        g_state.previousFlying = flying;
        g_state.previousFalling = falling;
    }

    void OnUpdate(void*, const void* raw)
    {
        const auto* args =
            static_cast<const ev::UpdateArgs*>(raw);
        const float dt = args && std::isfinite(args->dt)
            ? std::clamp(args->dt, 0.0f, 0.1f)
            : 0.016f;
        Tick(dt);
        if (!g_state.active) return;
        const int braking =
            !g_state.grounded && g_state.braking ? 1 : 0;
        if (braking != g_state.lastBrakeSent)
        {
            g_state.lastBrakeSent = braking;
            SendCommand(braking ? "BRK\t1" : "BRK\t0");
        }
    }

    void OnInput(void*, const void* raw)
    {
        const auto* args = static_cast<const ev::InputArgs*>(raw);
        if (!args || !args->handled || !g_state.active ||
            g_state.classicVertical ||
            (args->message != WM_KEYDOWN &&
             args->message != WM_SYSKEYDOWN))
            return;
        const uintptr_t key = args->wparam;
        if (key != VK_SPACE && key != 'X' && key != 'x') return;

        void* unit = ActivePlayer();
        if (!unit || (!OnAdvFlyMount(unit) && !IsFlying(unit)))
            return;

        if (key == VK_SPACE)
        {
            const bool repeated =
                (args->lparam & (uintptr_t{1} << 30)) != 0;
            if (!repeated)
            {
                const DWORD now = GetTickCount();
                if (g_state.lastSpaceMs &&
                    now - g_state.lastSpaceMs <=
                        kDoubleJumpWindowMs)
                {
                    g_state.lastSpaceMs = 0;
                    *args->handled = true;
                    ApplyMovementGate(unit);
                    SendCommand("TAKEOFF");
                    return;
                }
                g_state.lastSpaceMs = now;
            }
        }

        *args->handled = true;
        ApplyMovementGate(unit);
    }

    void OnWorldEnter(void*, const void*)
    {
        ResetState();
        SendCommand("RESYNC\t1");
    }

    void OnWorldLeave(void*, const void*)
    {
        ResetState();
    }

    bool Install()
    {
        bool ok = true;
        ok &= network::RegisterClientOpcode(
            opcodes::CmsgSkyriding, "CMSG_WXL_SKYRIDING");
        ok &= network::RegisterServerOpcode(
            opcodes::SmsgSkyriding, "SMSG_WXL_SKYRIDING",
            &OnPacket, nullptr);
        ok &= network::RegisterClientOpcode(
            opcodes::CmsgMoveAddImpulseAck,
            "CMSG_MOVE_ADD_IMPULSE_ACK");
        ok &= network::RegisterServerOpcode(
            opcodes::SmsgMoveAddImpulse,
            "SMSG_MOVE_ADD_IMPULSE",
            &OnMoveAddImpulse, nullptr);
        if (!g_subscribed)
        {
            wxl_skyriding::g_api->Subscribe(
                uint32_t(ev::Event::OnUpdate), &OnUpdate, nullptr);
            wxl_skyriding::g_api->Subscribe(
                uint32_t(ev::Event::OnInput), &OnInput, nullptr);
            wxl_skyriding::g_api->Subscribe(
                uint32_t(ev::Event::OnWorldEnter), &OnWorldEnter, nullptr);
            wxl_skyriding::g_api->Subscribe(
                uint32_t(ev::Event::OnWorldLeave), &OnWorldLeave, nullptr);
            g_subscribed = true;
        }
        wxl_skyriding::Animation()->SetResolveOverride(
            &ResolveAnimationOverride);
        if (ok)
            WLOG_INFO(
                "skyriding: native opcode controller and AdvFly owner installed");
        return ok;
    }
}

namespace wxl_skyriding
{
    Diagnostics GetDiagnostics() noexcept
    {
        Diagnostics diagnostics;
        diagnostics.active = g_state.active;
        diagnostics.grounded = g_state.grounded;
        diagnostics.braking = g_state.braking;
        diagnostics.stalled = g_state.stalled;
        diagnostics.forcedForward = g_state.forcedForward;
        diagnostics.impulseActive = g_state.impulseActive;
        diagnostics.launchActive = g_state.launchActive;
        diagnostics.impulseSequence = g_state.impulseSequence;
        diagnostics.launchDurationMs = g_state.launchDurationMs;
        diagnostics.flightRate = g_state.flightRate;
        diagnostics.coastYardsPerSec = g_state.coastYardsPerSec;
        diagnostics.launchProgress = g_state.launchProgress;
        diagnostics.launchForwardSpeed =
            g_state.launchForwardSpeed;
        diagnostics.launchForwardYards = g_state.launchForwardYards;
        diagnostics.launchRiseYards = g_state.launchRiseYards;
        diagnostics.impulseVelocity = g_state.impulseVelocity;
        diagnostics.launchHeading = g_state.launchHeading;
        diagnostics.forcedAnimation = g_state.forcedAnimation;
        diagnostics.boneAnimation = g_state.boneAnimation;
        diagnostics.cruiseAnimation = g_state.cruiseAnimation;
        diagnostics.phase = PhaseName(g_state.phase);
        diagnostics.lastCommand = g_state.lastCommand.data();
        if (g_state.launchActive)
            diagnostics.launchElapsedMs =
                GetTickCount() - g_state.launchStartedMs;

        void* unit = ActivePlayer();
        diagnostics.playerAvailable = unit != nullptr;
        if (!unit)
            return diagnostics;

        diagnostics.flying = IsFlying(unit);
        diagnostics.falling = IsFalling(unit);
        __try
        {
            diagnostics.movementFlags = MovementFlags(unit);
            diagnostics.facing = Facing(unit);
            diagnostics.pitch = Pitch(unit);
            std::copy_n(
                UnitPosition(unit), diagnostics.position.size(),
                diagnostics.position.begin());
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            diagnostics.playerAvailable = false;
        }
        return diagnostics;
    }
}

namespace wxl_skyriding
{
    bool InstallSkyriding()
    {
        return ::Install();
    }
}
