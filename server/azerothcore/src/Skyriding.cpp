/*
 * mod-skyriding — Horizon skyriding physics + vigor + Surge/Skyward.
 * Standalone AzerothCore companion for the WarcraftXL skyriding modules.
 */

#include "AllSpellScript.h"
#include "Chat.h"
#include "Config.h"
#include "GridTerrainData.h"
#include "Log.h"
#include "MotionMaster.h"
#include "MovementHandlerScript.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellChargeMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr char const* ADDON_PREFIX = "HORIZON_SKY";
    constexpr uint32 SPELL_FLIGHT_CHARGES = 98052;
    constexpr uint32 SPELL_WHIRLING_SURGE = 361584;
    constexpr uint32 SPELL_SKYRIDING_BASICS = 376777;
    constexpr uint32 SPELL_SKYRIDING_CHARGES = 383359;
    constexpr uint32 SPELL_SURGE_FORWARD = 372608;
    constexpr uint32 SPELL_SKYWARD_ASCENT = 372610;
    constexpr uint32 SPELL_LIFT_OFF = 383363;
    constexpr uint32 SPELL_THRILL_OF_THE_SKIES = 383366;
    constexpr uint32 SPELL_AERIAL_HALT = 403092;
    constexpr uint32 SPELL_FLIGHT_STYLE_SKYRIDING = 404464;
    constexpr uint32 SPELL_FLIGHT_STYLE_STEADY = 404468;
    constexpr uint32 SPELL_VIGOR = 372773;
    constexpr uint32 SPELL_CHANGE_FLIGHT_STYLE = 404471;
    constexpr uint32 SPELL_SECOND_WIND = 425782;
    constexpr uint32 SKYRIDING_CHARGE_CATEGORY = 2391;
    constexpr uint32 SPELL_PARACHUTE_BUFF = 44795;
    constexpr uint32 SPELL_SAFE_FALL = 29950;
    constexpr uint32 SPELL_DRUID_FLIGHT_FORM = 33943;
    constexpr uint32 SPELL_DRUID_SWIFT_FLIGHT_FORM = 40120;
    constexpr uint32 REENTRY_PARA_DELAY_MS = 500;
    constexpr uint32 REENTRY_SAFE_FALL_MS = 30000;
    constexpr uint32 REENTRY_PARA_DURATION_MS = 10000;
    constexpr float REENTRY_AIR_HEIGHT = 5.0f;
    constexpr uint32 RECONNECT_STATE_TTL_MS = 10u * 60u * 1000u;

    bool sEnabled = true;
    bool sRequireFlightChargesAura = false;
    bool sAutoLearnAbilities = true;
    uint32 sVigorMax = 6;
    uint32 sVigorRechargeMs = 10000;
    uint32 sThrillRechargeMs = 5000;
    uint32 sWhirlingCooldownMs = 30000;
    uint32 sAerialHaltCooldownMs = 10000;
    uint32 sAerialHaltDurationMs = 4000;
    uint32 sSecondWindMaxCharges = 3;
    uint32 sSecondWindRechargeMs = 180000;
    uint32 sRateIntervalMs = 100;
    float sTurnRate = 0.75f;
    bool sClassicVertical = false;
    bool sNativePackets = true;
    bool sDevAddonMessages = false;
    std::unordered_set<uint32> sAllowedMountDisplayIds;

    float sBaseFlightRate = 2.5f;   // 250% at takeoff / fall-into-flight
    float sMaxFlightRate = 6.5f;    // live skyriding peak
    float sMinFlightRate = 0.25f;   // floor while stalled (still coasts forward)
    float sStallThreshold = 1.0f;   // below this → wings can't hold → sink
    float sDivePitch = -0.12f;
    float sClimbPitch = 0.12f;
    float sDiveAccelPerSec = 0.55f; // slow climb to peak (~full dive: ~7s 2.5→6.5)
    float sHorizDecelPerSec = 0.35f;
    float sClimbDecelPerSec = 0.95f;
    float sBrakeDecelPerSec = 1.8f;
    float sSkywardSpeedXY = 11.0f;  // fixed forward component captured at cast
    float sSkywardSpeedZ = 42.0f;   // base upward component
    float sSurgeBoostRate = 1.5f;
    float sSkywardBoostRate = 0.35f; // small addition to current forward momentum
    float sThrillFlightRate = 4.5f;
    float sAerialHaltRate = 0.65f;
    uint32 sSpdIntervalMs = 400;
    uint32 sSkywardLandGraceMs = 3500; // cover knockback + complete flap/apex
    uint32 sGroundLockMs = 2000;       // positive legacy GLOCK payload; latch has no timeout


    struct SkyridingState
    {
        uint32 vigor = 6;
        uint32 vigorMax = 6;
        uint32 rechargeLeftMs = 0;
        float flightRate = 3.0f;
        bool active = false;
        bool modeSynced = false;
        bool wasFlying = false;
        bool braking = false;
        bool wasAirborne = false;   // left terrain this flight
        bool landSent = false;      // LAND addon already fired for this touchdown
        uint32 landGraceUntilMs = 0; // takeoff window — ignore near-ground LAND
        bool grounded = false;      // latched after landing; cleared only by takeoff/dismount
        uint32 lastTakeoffMs = 0; // server throttle for double-Space Lift Off
        uint32 nextImpulseSequence = 1;
        uint32 lastImpulseAck = 0;
        uint32 whirlingCooldownMs = 0;
        uint32 aerialHaltCooldownMs = 0;
        uint32 aerialHaltLeftMs = 0;
        uint32 secondWindCharges = 3;
        uint32 secondWindMaxCharges = 3;
        uint32 secondWindRechargeLeftMs = 0;
        bool thrillActive = false;
        uint32 appliedRechargeRatePermille = 0;
        bool lastSentThrill = false;
        uint32 lastSentWhirlingSec = 0xFFFFFFFF;
        uint32 lastSentAerialSec = 0xFFFFFFFF;
        uint32 lastSentSecondCharges = 0xFFFFFFFF;
        uint32 lastSentSecondRechargeSec = 0xFFFFFFFF;
        bool auxSynced = false;
        uint32 spdAccMs = 0;
        uint32 rateAccMs = 0;
        uint32 lastSentVigor = 0;
        uint32 lastSentVigorMax = 0;
        uint32 lastSentRecharge = 0;
        bool lastSentActive = false;
        bool chargesSynced = false;
        float lastRateNorm = 2.0f;
        uint32 handshakeUntilMs = 0;
        uint32 nextHandshakeMs = 0;
        uint32 reentryParaDelayMs = 0;
        bool reentryHandled = false;
        char const* band = "idle";
    };

    struct SkyridingReconnectState
    {
        uint32 mountSpellId = 0;
        uint32 mountDisplayId = 0;
        uint32 savedAtMs = 0;
        bool airborne = false;
    };

    std::unordered_map<ObjectGuid, SkyridingState> sStates;
    std::unordered_map<ObjectGuid, SkyridingReconnectState> sReconnectStates;

    void LoadConfig()
    {
        sEnabled = sConfigMgr->GetOption<bool>("Skyriding.Enable", true);
        sRequireFlightChargesAura = sConfigMgr->GetOption<bool>(
            "Skyriding.RequireFlightChargesAura", false);
        sAutoLearnAbilities = sConfigMgr->GetOption<bool>(
            "Skyriding.AutoLearnAbilities", true);
        sVigorMax = std::max<uint32>(1,
            sConfigMgr->GetOption<uint32>("Skyriding.VigorMax", 6));
        sVigorRechargeMs = std::max<uint32>(100,
            sConfigMgr->GetOption<uint32>("Skyriding.VigorRechargeMs", 10000));
        sThrillRechargeMs = std::max<uint32>(100,
            sConfigMgr->GetOption<uint32>("Skyriding.ThrillRechargeMs", 5000));
        sWhirlingCooldownMs = std::max<uint32>(1000,
            sConfigMgr->GetOption<uint32>("Skyriding.WhirlingSurgeCooldownMs", 30000));
        sAerialHaltCooldownMs = std::max<uint32>(1000,
            sConfigMgr->GetOption<uint32>("Skyriding.AerialHaltCooldownMs", 10000));
        sAerialHaltDurationMs = std::max<uint32>(100,
            sConfigMgr->GetOption<uint32>("Skyriding.AerialHaltDurationMs", 4000));
        sSecondWindMaxCharges = std::max<uint32>(1,
            sConfigMgr->GetOption<uint32>("Skyriding.SecondWindMaxCharges", 3));
        sSecondWindRechargeMs = std::max<uint32>(1000,
            sConfigMgr->GetOption<uint32>("Skyriding.SecondWindRechargeMs", 180000));
        sRateIntervalMs = std::max<uint32>(50,
            sConfigMgr->GetOption<uint32>("Skyriding.RateIntervalMs", 100));
        sTurnRate = sConfigMgr->GetOption<float>("Skyriding.TurnRate", 0.75f);
        sClassicVertical = sConfigMgr->GetOption<bool>("Skyriding.ClassicVertical", false);
        sNativePackets = sConfigMgr->GetOption<bool>("Skyriding.NativePackets", true);
        sDevAddonMessages = sConfigMgr->GetOption<bool>(
            "Skyriding.DevAddonMessages", false);

        std::string displayIds = sConfigMgr->GetOption<std::string>(
            "Skyriding.AllowedMountDisplayIds",
            "17699 17700 17701 19608 24891 38636 41458 41459 43684 "
            "100540 102809 102814 102864 118055");
        std::replace(displayIds.begin(), displayIds.end(), ',', ' ');
        sAllowedMountDisplayIds.clear();
        std::istringstream values(displayIds);
        for (uint32 displayId = 0; values >> displayId;)
            if (displayId != 0)
                sAllowedMountDisplayIds.insert(displayId);

        sBaseFlightRate = sConfigMgr->GetOption<float>("Skyriding.BaseFlightRate", 2.5f);
        sMaxFlightRate = sConfigMgr->GetOption<float>("Skyriding.MaxFlightRate", 6.5f);
        sMinFlightRate = sConfigMgr->GetOption<float>("Skyriding.MinFlightRate", 0.25f);
        sStallThreshold = sConfigMgr->GetOption<float>("Skyriding.StallThreshold", 1.0f);
        sDivePitch = sConfigMgr->GetOption<float>("Skyriding.DivePitch", -0.12f);
        sClimbPitch = sConfigMgr->GetOption<float>("Skyriding.ClimbPitch", 0.12f);
        sDiveAccelPerSec = sConfigMgr->GetOption<float>("Skyriding.DiveAccelPerSec", 0.55f);
        sHorizDecelPerSec = sConfigMgr->GetOption<float>("Skyriding.HorizDecelPerSec", 0.35f);
        sClimbDecelPerSec = sConfigMgr->GetOption<float>("Skyriding.ClimbDecelPerSec", 0.95f);
        sBrakeDecelPerSec = sConfigMgr->GetOption<float>("Skyriding.BrakeDecelPerSec", 1.8f);
        sSkywardSpeedXY = sConfigMgr->GetOption<float>("Skyriding.SkywardSpeedXY", 11.0f);
        sSkywardSpeedZ = sConfigMgr->GetOption<float>("Skyriding.SkywardSpeedZ", 42.0f);
        sSurgeBoostRate = sConfigMgr->GetOption<float>("Skyriding.SurgeBoostRate", 1.5f);
        sSkywardBoostRate = sConfigMgr->GetOption<float>("Skyriding.SkywardBoostRate", 0.35f);
        sThrillFlightRate = sConfigMgr->GetOption<float>("Skyriding.ThrillFlightRate", 4.5f);
        sAerialHaltRate = sConfigMgr->GetOption<float>("Skyriding.AerialHaltRate", 0.65f);
        sSpdIntervalMs = std::max<uint32>(100,
            sConfigMgr->GetOption<uint32>("Skyriding.SpdIntervalMs", 400));
        sSkywardLandGraceMs = std::max<uint32>(100,
            sConfigMgr->GetOption<uint32>("Skyriding.SkywardLandGraceMs", 3500));
        sGroundLockMs = std::max<uint32>(100,
            sConfigMgr->GetOption<uint32>("Skyriding.GroundLockMs", 2000));
    }

    void SendAddon(Player* player, std::string const& body)
    {
        if (!player || !player->GetSession())
            return;

        if (sNativePackets)
        {
            // WXL packet strings are uint32 length + raw bytes (not the
            // null-terminated string encoding used by ordinary WorldPacket
            // operator<<). Keep both directions on the Client Extensions ABI.
            WorldPacket data(SMSG_WXL_SKYRIDING, sizeof(uint32) + body.size());
            data << uint32(body.size());
            if (!body.empty())
                data.append(reinterpret_cast<uint8 const*>(body.data()), body.size());
            player->SendDirectMessage(&data);
        }

        // Optional mirror for the old bar/debug addon. Gameplay no longer
        // depends on addon chat when the native packet bridge is enabled.
        if (sDevAddonMessages)
        {
            std::string const msg = std::string(ADDON_PREFIX) + "\t" + body;
            WorldPacket data;
            ChatHandler::BuildChatPacket(
                data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, msg);
            player->SendDirectMessage(&data);
        }
    }

    SkyridingState& GetState(Player* player)
    {
        ObjectGuid const guid = player->GetGUID();
        auto it = sStates.find(guid);
        if (it == sStates.end())
        {
            SkyridingState st;
            st.vigor = sVigorMax;
            st.vigorMax = sVigorMax;
            st.secondWindCharges = sSecondWindMaxCharges;
            st.secondWindMaxCharges = sSecondWindMaxCharges;
            st.flightRate = sBaseFlightRate;
            it = sStates.emplace(guid, st).first;
        }
        return it->second;
    }

    bool UsesCoreVigor()
    {
        return sSpellChargeMgr->IsChargeSpell(SPELL_SURGE_FORWARD)
            && sSpellChargeMgr->IsChargeSpell(SPELL_SKYWARD_ASCENT);
    }

    bool UsesCoreSecondWind()
    {
        return sSpellChargeMgr->IsChargeSpell(SPELL_SECOND_WIND);
    }

    bool RefreshCoreVigor(Player* player, SkyridingState& st)
    {
        if (!UsesCoreVigor())
            return false;

        std::optional<Acore::SpellCharges::ChargeSnapshot> snapshot =
            sSpellChargeMgr->GetSnapshot(player, SPELL_SURGE_FORWARD);
        if (!snapshot)
            return false;

        st.vigor = snapshot->CurrentCharges;
        st.vigorMax = snapshot->MaxCharges;
        st.rechargeLeftMs = snapshot->RechargeRemainingMs;
        return true;
    }

    bool RefreshCoreSecondWind(Player* player, SkyridingState& st)
    {
        if (!UsesCoreSecondWind())
            return false;

        std::optional<Acore::SpellCharges::ChargeSnapshot> snapshot =
            sSpellChargeMgr->GetSnapshot(player, SPELL_SECOND_WIND);
        if (!snapshot)
            return false;

        st.secondWindCharges = snapshot->CurrentCharges;
        st.secondWindMaxCharges = snapshot->MaxCharges;
        st.secondWindRechargeLeftMs = snapshot->RechargeRemainingMs;
        return true;
    }

    bool HasFlightCharges(Player* player)
    {
        Aura* aura = player->GetAura(SPELL_FLIGHT_CHARGES);
        return aura && aura->GetStackAmount() > 0;
    }

    void EnsureFlightAuras(Player* player)
    {
        if (!player)
            return;

        // v1.1 originally stored the selected style as learned passives. Retail
        // owns this state with mutually-exclusive auras, so migrate any existing
        // learned marker once and let the aura contract drive all later checks.
        bool const learnedSteady =
            player->HasSpell(SPELL_FLIGHT_STYLE_STEADY);
        bool const learnedSkyriding =
            player->HasSpell(SPELL_FLIGHT_STYLE_SKYRIDING);
        if (learnedSteady || learnedSkyriding)
        {
            player->removeSpell(
                SPELL_FLIGHT_STYLE_STEADY, false, false);
            player->removeSpell(
                SPELL_FLIGHT_STYLE_SKYRIDING, false, false);
            player->RemoveAurasDueToSpell(
                learnedSteady
                    ? SPELL_FLIGHT_STYLE_SKYRIDING
                    : SPELL_FLIGHT_STYLE_STEADY);
            if (!player->HasAura(
                    learnedSteady
                        ? SPELL_FLIGHT_STYLE_STEADY
                        : SPELL_FLIGHT_STYLE_SKYRIDING))
            {
                player->AddAura(
                    learnedSteady
                        ? SPELL_FLIGHT_STYLE_STEADY
                        : SPELL_FLIGHT_STYLE_SKYRIDING,
                    player);
            }
        }

        bool const hasSkyriding =
            player->HasAura(SPELL_FLIGHT_STYLE_SKYRIDING);
        bool const hasSteady =
            player->HasAura(SPELL_FLIGHT_STYLE_STEADY);
        if (hasSkyriding && hasSteady)
            player->RemoveAurasDueToSpell(
                SPELL_FLIGHT_STYLE_STEADY);
        else if (!hasSkyriding && !hasSteady)
            player->AddAura(
                SPELL_FLIGHT_STYLE_SKYRIDING, player);

        bool const active = player->IsMounted()
            && player->CanFly()
            && player->HasAura(SPELL_FLIGHT_STYLE_SKYRIDING);
        if (active)
        {
            if (!player->HasAura(SPELL_VIGOR))
                player->AddAura(SPELL_VIGOR, player);
        }
        else
            player->RemoveAurasDueToSpell(SPELL_VIGOR);
    }

    bool IsSkyridingCandidate(Player* player)
    {
        if (!sEnabled || !player)
            return false;
        if (!player->HasAura(SPELL_FLIGHT_STYLE_SKYRIDING)
            || player->HasAura(SPELL_FLIGHT_STYLE_STEADY)
            || !player->HasAura(SPELL_VIGOR))
            return false;
        if (!player->IsMounted() || !player->CanFly())
            return false;
        if (!sAllowedMountDisplayIds.empty()
            && sAllowedMountDisplayIds.find(player->GetMountID()) == sAllowedMountDisplayIds.end())
            return false;
        if (sRequireFlightChargesAura && !HasFlightCharges(player))
            return false;
        return true;
    }

    void SyncCharges(Player* player, SkyridingState& st, bool force = false)
    {
        bool const wantActive = IsSkyridingCandidate(player);
        bool const changed = force
            || !st.chargesSynced
            || st.lastSentVigor != st.vigor
            || st.lastSentVigorMax != st.vigorMax
            || st.lastSentRecharge != st.rechargeLeftMs
            || st.lastSentActive != wantActive;

        if (!changed)
            return;

        st.lastSentVigor = st.vigor;
        st.lastSentVigorMax = st.vigorMax;
        st.lastSentRecharge = st.rechargeLeftMs;
        st.lastSentActive = wantActive;
        st.chargesSynced = true;

        SendAddon(player,
            "CHG\t" + std::to_string(st.vigor)
            + "\t" + std::to_string(st.vigorMax)
            + "\t" + std::to_string(st.rechargeLeftMs)
            + "\t" + (wantActive ? "1" : "0"));
    }

    void SyncMode(Player* player, SkyridingState& st, bool modeOn)
    {
        if (st.modeSynced && st.active == modeOn)
            return;

        st.active = modeOn;
        st.modeSynced = true;
        // MODE on while mounted+charges (ground OR air) so WXL gates Space before takeoff.
        SendAddon(player, std::string("MODE\t") + (modeOn ? "1" : "0"));
        SendAddon(player, std::string("VERT\t") + (sClassicVertical ? "1" : "0"));
        SendAddon(player, "TURN\t" + std::to_string(sTurnRate));
    }

    void BeginClientHandshake(SkyridingState& st, uint32 durationMs = 6000)
    {
        uint32 const now = getMSTime();
        st.handshakeUntilMs = now + durationMs;
        st.nextHandshakeMs = now;
        st.modeSynced = false;
    }

    void SyncAuxAbilities(Player* player, SkyridingState& st, bool force = false)
    {
        uint32 const whirlingSec = (st.whirlingCooldownMs + 999) / 1000;
        uint32 const aerialSec = (st.aerialHaltCooldownMs + 999) / 1000;
        uint32 const secondRechargeSec = (st.secondWindRechargeLeftMs + 999) / 1000;
        if (!force && st.auxSynced
            && st.lastSentThrill == st.thrillActive
            && st.lastSentWhirlingSec == whirlingSec
            && st.lastSentAerialSec == aerialSec
            && st.lastSentSecondCharges == st.secondWindCharges
            && st.lastSentSecondRechargeSec == secondRechargeSec)
            return;

        st.auxSynced = true;
        st.lastSentThrill = st.thrillActive;
        st.lastSentWhirlingSec = whirlingSec;
        st.lastSentAerialSec = aerialSec;
        st.lastSentSecondCharges = st.secondWindCharges;
        st.lastSentSecondRechargeSec = secondRechargeSec;
        SendAddon(player,
            "AUX\t" + std::to_string(st.whirlingCooldownMs)
            + "\t" + std::to_string(st.aerialHaltCooldownMs)
            + "\t" + std::to_string(st.secondWindCharges)
            + "\t" + std::to_string(st.secondWindRechargeLeftMs)
            + "\t" + (st.thrillActive ? "1" : "0"));
    }

    void ApplyTurnRate(Player* player, bool enable)
    {
        if (!player)
            return;
        if (enable)
            player->SetSpeed(MOVE_TURN_RATE, std::clamp(sTurnRate, 0.05f, 1.0f), true);
        else
            player->SetSpeed(MOVE_TURN_RATE, 1.0f, true);
    }

    void SyncRate(Player* player, SkyridingState& st, uint32 diff = 0,
        bool force = false)
    {
        st.rateAccMs += diff;
        if (!force && st.rateAccMs < sRateIntervalMs)
            return;

        // Normalized 0..1 for client anim bands (glide / slow / stall). Negative = stall.
        float span = std::max(0.01f, sMaxFlightRate - sMinFlightRate);
        float norm = (st.flightRate - sMinFlightRate) / span;
        if (norm < 0.f)
            norm = 0.f;
        if (norm > 1.f)
            norm = 1.f;
        if (st.flightRate < sStallThreshold)
            norm = -std::max(0.01f, norm);

        if (!force && std::abs(norm - st.lastRateNorm) < 0.001f)
        {
            st.rateAccMs = 0;
            return;
        }

        st.rateAccMs = 0;
        st.lastRateNorm = norm;
        SendAddon(player, "RATE\t" + std::to_string(norm));
    }

    void ApplyTimedAura(Player* player, uint32 spellId, uint32 durationMs)
    {
        if (!player)
            return;
        if (Aura* aura = player->AddAura(spellId, player))
        {
            aura->SetDuration(int32(durationMs));
            aura->SetMaxDuration(int32(durationMs));
        }
    }

    bool IsAirborneAboveGround(Player* player, float minHeight)
    {
        if (!player || !player->GetMap())
            return false;
        float const z = player->GetPositionZ();
        float const ground = player->GetMap()->GetHeight(
            player->GetPhaseMask(), player->GetPositionX(), player->GetPositionY(),
            z + 5.0f, true, 500.0f);
        return ground > -50000.0f && (z - ground) > minHeight;
    }

    void ForceReentryDismountParachute(Player* player, SkyridingState& st)
    {
        // A valid mounted skyriding session needs no recovery. Character login
        // and map changes can preserve its mount aura and movement state; the
        // old unconditional fallback dismounted that healthy state and made a
        // reconnect look as though the server had forgotten the mount.
        if (player && IsSkyridingCandidate(player))
        {
            st.reentryHandled = true;
            st.reentryParaDelayMs = 0;
            st.wasFlying = player->IsFlying();
            st.wasAirborne = st.wasFlying ||
                IsAirborneAboveGround(player, REENTRY_AIR_HEIGHT);
            st.grounded = false;
            st.modeSynced = false;
            SyncMode(player, st, true);
            SyncRate(player, st, 0, true);
            return;
        }

        if (!player || player->IsGameMaster() || st.reentryHandled)
            return;

        bool const flying = player->IsFlying();
        bool const airborne = IsAirborneAboveGround(player, REENTRY_AIR_HEIGHT);
        if (!flying && !airborne)
            return;
        if (!flying && !player->IsMounted()
            && !player->HasAuraType(SPELL_AURA_MOUNTED))
            return;

        st.reentryHandled = true;
        if (player->HasAura(SPELL_DRUID_SWIFT_FLIGHT_FORM))
            player->RemoveAurasDueToSpell(SPELL_DRUID_SWIFT_FLIGHT_FORM);
        if (player->HasAura(SPELL_DRUID_FLIGHT_FORM))
            player->RemoveAurasDueToSpell(SPELL_DRUID_FLIGHT_FORM);

        player->Dismount();
        player->RemoveAurasByType(SPELL_AURA_MOUNTED);
        player->SetCanFly(false);
        ApplyTimedAura(player, SPELL_SAFE_FALL, REENTRY_SAFE_FALL_MS);
        if (flying || airborne)
            player->GetMotionMaster()->MoveFall();

        st.reentryParaDelayMs = REENTRY_PARA_DELAY_MS;
        st.flightRate = sBaseFlightRate;
        st.braking = false;
        st.wasFlying = false;
        st.wasAirborne = false;
        st.grounded = false;
        st.band = "idle";
        st.modeSynced = false;
        SyncMode(player, st, false);
        SyncRate(player, st, 0, true);
        ApplyTurnRate(player, false);
        LOG_INFO("module",
            "mod-skyriding: mid-air re-entry dismounted player {} and armed parachute",
            player->GetGUID().ToString());
    }

    void TickReentryParachute(Player* player, SkyridingState& st, uint32 diff)
    {
        if (!player || st.reentryParaDelayMs == 0 || diff == 0)
            return;
        if (st.reentryParaDelayMs > diff)
        {
            st.reentryParaDelayMs -= diff;
            return;
        }
        st.reentryParaDelayMs = 0;
        ApplyTimedAura(player, SPELL_PARACHUTE_BUFF, REENTRY_PARA_DURATION_MS);
        player->SetFeatherFall(true);
    }

    void TickReentryGroundClear(Player* player, SkyridingState& st)
    {
        if (!player || !st.reentryHandled || st.reentryParaDelayMs != 0)
            return;
        if (IsAirborneAboveGround(player, 1.5f))
            return;
        player->SetFeatherFall(false);
    }

    uint32 MountedAuraSpell(Player* player)
    {
        if (!player || !player->IsMounted())
            return 0;
        auto const& effects = player->GetAuraEffectsByType(
            SPELL_AURA_MOUNTED);
        return effects.empty() ? 0 : effects.front()->GetId();
    }

    void RememberReconnectState(Player* player)
    {
        if (!player)
            return;
        ObjectGuid const guid = player->GetGUID();
        uint32 const mountSpellId = MountedAuraSpell(player);
        if (!mountSpellId || !IsSkyridingCandidate(player))
        {
            sReconnectStates.erase(guid);
            return;
        }

        sReconnectStates[guid] = SkyridingReconnectState{
            mountSpellId,
            player->GetMountID(),
            getMSTime(),
            player->IsFlying() ||
                IsAirborneAboveGround(player, REENTRY_AIR_HEIGHT)
        };
    }

    bool RestoreReconnectState(Player* player)
    {
        if (!player)
            return false;
        auto const found = sReconnectStates.find(player->GetGUID());
        if (found == sReconnectStates.end())
            return false;

        SkyridingReconnectState const saved = found->second;
        sReconnectStates.erase(found);
        uint32 const now = getMSTime();
        if (!saved.airborne || !saved.mountSpellId ||
            !saved.mountDisplayId ||
            now - saved.savedAtMs > RECONNECT_STATE_TTL_MS ||
            !player->HasAura(SPELL_FLIGHT_STYLE_SKYRIDING) ||
            player->HasAura(SPELL_FLIGHT_STYLE_STEADY) ||
            (!sAllowedMountDisplayIds.empty() &&
             sAllowedMountDisplayIds.find(saved.mountDisplayId) ==
                 sAllowedMountDisplayIds.end()) ||
            !sSpellMgr->GetSpellInfo(saved.mountSpellId))
            return false;

        player->CastSpell(player, saved.mountSpellId, true);
        if (!player->IsMounted())
            return false;

        player->SetCanFly(true);
        player->AddUnitMovementFlag(
            MOVEMENTFLAG_CAN_FLY | MOVEMENTFLAG_FLYING);
        LOG_INFO("module",
            "mod-skyriding: restored reconnect mount spell {} display {} for {}",
            saved.mountSpellId, saved.mountDisplayId,
            player->GetGUID().ToString());
        return true;
    }

    void TickClientHandshake(Player* player, SkyridingState& st)
    {
        if (st.handshakeUntilMs == 0)
            return;
        uint32 const now = getMSTime();
        if (now >= st.handshakeUntilMs)
        {
            st.handshakeUntilMs = 0;
            return;
        }
        if (now < st.nextHandshakeMs)
            return;

        st.nextHandshakeMs = now + 1000;
        st.modeSynced = false;
        SyncRate(player, st, 0, true);
        SyncCharges(player, st, true);
        SyncAuxAbilities(player, st, true);
        ForceReentryDismountParachute(player, st);
    }

    void SyncSpeedDebug(Player* player, SkyridingState& st, uint32 diff, float pitch)
    {
        st.spdAccMs += diff;
        if (st.spdAccMs < sSpdIntervalMs)
            return;
        st.spdAccMs = 0;

        // SPD\trate\tpitch\tband\tyardsPerSec  — UI test overlay (~2–3 Hz)
        float const yardsPerSec = player->GetSpeed(MOVE_FLIGHT);
        char buf[96];
        snprintf(buf, sizeof(buf), "SPD\t%.2f\t%.2f\t%s\t%.1f",
            st.flightRate, pitch, st.band ? st.band : "?", yardsPerSec);
        SendAddon(player, buf);
    }

    void EnsureSpells(Player* player)
    {
        if (!sAutoLearnAbilities || !player)
            return;
        if (!player->HasSpell(SPELL_SURGE_FORWARD))
            player->learnSpell(SPELL_SURGE_FORWARD);
        if (!player->HasSpell(SPELL_SKYWARD_ASCENT))
            player->learnSpell(SPELL_SKYWARD_ASCENT);
        if (!player->HasSpell(SPELL_WHIRLING_SURGE))
            player->learnSpell(SPELL_WHIRLING_SURGE);
        if (!player->HasSpell(SPELL_AERIAL_HALT))
            player->learnSpell(SPELL_AERIAL_HALT);
        if (!player->HasSpell(SPELL_SECOND_WIND))
            player->learnSpell(SPELL_SECOND_WIND);
        if (!player->HasSpell(SPELL_LIFT_OFF))
            player->learnSpell(SPELL_LIFT_OFF);
        if (!player->HasSpell(SPELL_SKYRIDING_BASICS))
            player->learnSpell(SPELL_SKYRIDING_BASICS);
        if (!player->HasSpell(SPELL_SKYRIDING_CHARGES))
            player->learnSpell(SPELL_SKYRIDING_CHARGES);
        if (!player->HasSpell(SPELL_THRILL_OF_THE_SKIES))
            player->learnSpell(SPELL_THRILL_OF_THE_SKIES);
        if (!player->HasSpell(SPELL_CHANGE_FLIGHT_STYLE))
            player->learnSpell(SPELL_CHANGE_FLIGHT_STYLE);

        EnsureFlightAuras(player);
    }

    void TickVigor(Player* player, SkyridingState& st, uint32 diff)
    {
        st.thrillActive = player && player->IsFlying()
            && player->HasSpell(SPELL_THRILL_OF_THE_SKIES)
            && st.flightRate >= sThrillFlightRate;
        uint32 const rechargeMs = st.thrillActive ? sThrillRechargeMs : sVigorRechargeMs;

        if (UsesCoreVigor())
        {
            uint32 ratePermille = 1000;
            if (st.thrillActive)
            {
                uint64 ratio = (uint64(sVigorRechargeMs) * 1000 + sThrillRechargeMs - 1) / sThrillRechargeMs;
                ratePermille = uint32(std::clamp<uint64>(ratio, 1, 100000));
            }

            if (st.appliedRechargeRatePermille != ratePermille
                && sSpellChargeMgr->SetCategoryRechargeRate(player, SKYRIDING_CHARGE_CATEGORY, ratePermille))
                st.appliedRechargeRatePermille = ratePermille;
            RefreshCoreVigor(player, st);
            return;
        }

        st.vigorMax = sVigorMax;

        if (st.vigor >= sVigorMax)
        {
            st.rechargeLeftMs = 0;
            return;
        }

        if (st.rechargeLeftMs == 0)
            st.rechargeLeftMs = rechargeMs;
        else if (st.rechargeLeftMs > rechargeMs)
            st.rechargeLeftMs = rechargeMs;

        if (diff >= st.rechargeLeftMs)
        {
            uint32 left = diff - st.rechargeLeftMs;
            ++st.vigor;
            while (st.vigor < sVigorMax && left >= rechargeMs)
            {
                ++st.vigor;
                left -= rechargeMs;
            }
            st.rechargeLeftMs = (st.vigor >= sVigorMax) ? 0 : (rechargeMs - left);
        }
        else
            st.rechargeLeftMs -= diff;
    }

    void TickAuxAbilities(Player* player, SkyridingState& st, uint32 diff)
    {
        auto tick = [diff](uint32& remaining)
        {
            remaining = diff >= remaining ? 0 : remaining - diff;
        };
        tick(st.whirlingCooldownMs);
        tick(st.aerialHaltCooldownMs);
        tick(st.aerialHaltLeftMs);

        if (RefreshCoreSecondWind(player, st))
            return;

        st.secondWindMaxCharges = sSecondWindMaxCharges;

        if (st.secondWindCharges >= sSecondWindMaxCharges)
        {
            st.secondWindCharges = sSecondWindMaxCharges;
            st.secondWindRechargeLeftMs = 0;
            return;
        }
        if (st.secondWindRechargeLeftMs == 0)
            st.secondWindRechargeLeftMs = sSecondWindRechargeMs;
        if (diff >= st.secondWindRechargeLeftMs)
        {
            ++st.secondWindCharges;
            st.secondWindRechargeLeftMs = st.secondWindCharges >= sSecondWindMaxCharges
                ? 0 : sSecondWindRechargeMs;
        }
        else
            st.secondWindRechargeLeftMs -= diff;
    }

    void TickMomentum(Player* player, SkyridingState& st, uint32 diff)
    {
        float const dt = diff / 1000.f;
        float const pitch = player->m_movementInfo.pitch;

        if (st.aerialHaltLeftMs > 0)
        {
            st.flightRate = sAerialHaltRate;
            st.band = "halt";
            player->m_movementInfo.RemoveMovementFlag(MOVEMENTFLAG_BACKWARD);
            player->m_movementInfo.AddMovementFlag(MOVEMENTFLAG_FORWARD);
            player->SetSpeed(MOVE_FLIGHT, st.flightRate, true);
            SyncSpeedDebug(player, st, diff, pitch);
            return;
        }

        // Pitch-scaled curvature: deeper dive / climb = stronger effect (gentler overall).
        if (pitch <= sDivePitch)
        {
            float strength = std::min(1.5f, (-pitch) / 0.9f);
            st.flightRate += sDiveAccelPerSec * strength * dt;
            st.band = "dive";
        }
        else if (pitch >= sClimbPitch)
        {
            float strength = std::min(1.5f, pitch / 0.9f);
            st.flightRate -= sClimbDecelPerSec * strength * dt;
            st.band = "climb";
        }
        else
        {
            st.flightRate -= sHorizDecelPerSec * dt;
            st.band = "level";
        }

        // S = brake (extra decel). Never hover-stop — mount keeps coasting forward.
        if (st.braking)
        {
            st.flightRate -= sBrakeDecelPerSec * dt;
            st.band = "brake";
        }

        if (st.flightRate > sMaxFlightRate)
            st.flightRate = sMaxFlightRate;
        if (st.flightRate < sMinFlightRate)
            st.flightRate = sMinFlightRate;

        // Below 1.0 → stall / sink (WXL). Dive is the only way to rebuild speed.
        if (st.flightRate < sStallThreshold)
            st.band = "stall";

        player->m_movementInfo.RemoveMovementFlag(MOVEMENTFLAG_BACKWARD);
        player->m_movementInfo.AddMovementFlag(MOVEMENTFLAG_FORWARD);

        player->SetSpeed(MOVE_FLIGHT, st.flightRate, true);
        SyncRate(player, st, diff);
        SyncSpeedDebug(player, st, diff, pitch);
    }

    bool TryConsumeVigor(Player* player, SkyridingState& st)
    {
        if (st.vigor == 0)
            return false;
        --st.vigor;
        if (st.vigor < st.vigorMax && st.rechargeLeftMs == 0)
            st.rechargeLeftMs = st.thrillActive ? sThrillRechargeMs : sVigorRechargeMs;
        SyncCharges(player, st, true);
        return true;
    }

    bool InGroundLock(SkyridingState const& st)
    {
        return st.grounded;
    }

    void ArmGroundLock(Player* player, SkyridingState& st)
    {
        // Ground lock is a state transition.  Re-arming it from duplicate LAND
        // messages restarts the client latch and creates an addon/core feedback
        // loop, so an already grounded player is deliberately a no-op.
        if (st.grounded)
        {
            st.wasAirborne = false;
            st.wasFlying = false;
            return;
        }

        st.grounded = true;
        // A real ground latch ends the previous airborne cycle. Keeping this true
        // makes every later double-Space request look like a duplicate takeoff.
        st.wasAirborne = false;
        st.wasFlying = false;
        st.flightRate = 0.0f;
        st.braking = false;
        SendAddon(player, "GLOCK\t" + std::to_string(sGroundLockMs));
        SyncRate(player, st, 0, true);
    }

    void ReleaseGroundLock(Player* player, SkyridingState& st)
    {
        if (!st.grounded)
            return;
        st.grounded = false;
        SendAddon(player, "GLOCK\t0");
    }

    void ForceTouchdown(Player* player, SkyridingState& st)
    {
        if (!player || st.landSent)
            return;

        st.landSent = true;
        st.wasAirborne = false;
        st.wasFlying = false;
        st.grounded = false;
        st.braking = false;
        st.flightRate = sBaseFlightRate;

        SendAddon(player, "LAND\t1");
        SendAddon(player, "LAND\t1");
        player->m_movementInfo.RemoveMovementFlag(
            MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY
            | MOVEMENTFLAG_ASCENDING | MOVEMENTFLAG_DESCENDING
            | MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);
        player->SetDisableGravity(false);
        player->SendMovementFlagUpdate(true);
    }

    void TickTerrainTouchdown(Player* player, SkyridingState& st)
    {
        if (!player || !player->IsInWorld())
            return;

        float const x = player->GetPositionX();
        float const y = player->GetPositionY();
        float const z = player->GetPositionZ();
        float const ground = player->GetMapHeight(x, y, z);
        if (ground <= INVALID_HEIGHT)
            return;

        float const separation = z - ground;
        constexpr float kLandingSeparation = 2.25f;
        bool const falling = player->HasUnitMovementFlag(
            MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);

        if (falling)
        {
            if (separation > kLandingSeparation)
            {
                st.wasAirborne = true;
                st.landSent = false;
            }
            return;
        }

        if (separation > kLandingSeparation)
        {
            st.wasAirborne = true;
            st.landSent = false;
            return;
        }

        if (!st.wasAirborne ||
            getMSTime() < st.landGraceUntilMs)
            return;
        ForceTouchdown(player, st);
    }

    void ApplyGroundLockFlags(Player* player, bool clearFalling = true)
    {
        if (!player)
            return;

        player->m_movementInfo.RemoveMovementFlag(
            MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY
            | MOVEMENTFLAG_ASCENDING | MOVEMENTFLAG_DESCENDING);
        if (clearFalling)
            player->m_movementInfo.RemoveMovementFlag(
                MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);
        player->SetDisableGravity(false);
    }

    void StopLandingMomentum(Player* player, bool preserveVertical = false)
    {
        if (!player)
            return;

        player->m_movementInfo.RemoveMovementFlag(
            MOVEMENTFLAG_FORWARD | MOVEMENTFLAG_BACKWARD
            | MOVEMENTFLAG_STRAFE_LEFT | MOVEMENTFLAG_STRAFE_RIGHT
            | MOVEMENTFLAG_PENDING_STOP | MOVEMENTFLAG_PENDING_STRAFE_STOP
            | MOVEMENTFLAG_PENDING_FORWARD | MOVEMENTFLAG_PENDING_BACKWARD
            | MOVEMENTFLAG_PENDING_STRAFE_LEFT | MOVEMENTFLAG_PENDING_STRAFE_RIGHT);
        player->m_movementInfo.pitch = 0.0f;
        if (!preserveVertical)
        {
            player->m_movementInfo.fallTime = 0;
            player->m_movementInfo.jump.Reset();
        }
    }

    void BeginImpulseFlight(Player* player, SkyridingState& st)
    {
        if (!player)
            return;

        player->m_movementInfo.RemoveMovementFlag(
            MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR
            | MOVEMENTFLAG_ASCENDING | MOVEMENTFLAG_DESCENDING
            | MOVEMENTFLAG_BACKWARD | MOVEMENTFLAG_PENDING_STOP);
        player->m_movementInfo.AddMovementFlag(
            MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY
            | MOVEMENTFLAG_FORWARD);
        player->SetDisableGravity(true);
        player->SendMovementFlagUpdate(true);
        st.wasFlying = true;
        st.wasAirborne = true;
        st.landSent = false;
    }

    void SendMoveAddImpulse(
        Player* player, SkyridingState& st,
        float x, float y, float z)
    {
        if (!player)
            return;

        uint32 const sequence = st.nextImpulseSequence++;
        if (st.nextImpulseSequence == 0)
            st.nextImpulseSequence = 1;

        WorldPacket data(
            SMSG_MOVE_ADD_IMPULSE,
            sizeof(uint64) + sizeof(uint32) + sizeof(float) * 3);
        data << uint64(player->GetGUID().GetRawValue());
        data << sequence;
        data << x << y << z;
        player->SendDirectMessage(&data);
        LOG_DEBUG("module",
            "mod-skyriding: MOVE_ADD_IMPULSE seq={} xyz=({:.2f},{:.2f},{:.2f}) for {}",
            sequence, x, y, z, player->GetGUID().ToString());
    }

    void ImpulseForward(Player* player, float speedXY, float speedZ)
    {
        float const orientation = player->GetOrientation();
        float const pitch = player->m_movementInfo.pitch;
        float const sourceX = player->GetPositionX()
            - std::cos(orientation) * std::cos(pitch);
        float const sourceY = player->GetPositionY()
            - std::sin(orientation) * std::cos(pitch);
        player->KnockbackFrom(
            sourceX, sourceY, speedXY, speedZ);
    }

    void ImpulseUp(Player* player, float speedZ)
    {
        float const x = player->GetPositionX();
        float const y = player->GetPositionY() - 0.01f;
        player->KnockbackFrom(x, y, 0.01f, speedZ);
    }

    void ApplySkywardAscent(Player* player, SkyridingState& st)
    {
        ImpulseUp(player, sSkywardSpeedZ);
        st.flightRate = std::max(st.flightRate, sBaseFlightRate);
        player->SetSpeed(MOVE_FLIGHT, st.flightRate, true);
        st.landGraceUntilMs = getMSTime() + 400;
        st.landSent = false;
        SendAddon(player, "ANIM\t1");
        SyncRate(player, st, 0, true);
    }

    void HandleSkyridingAbility(Player* player, uint32 spellId)
    {
        // Candidate = mounted flying mount with charges.
        // Skyward: same everywhere — forward/upward arc + FlapUp queue (WXL).
        // Surge requires already airborne.
        if (!IsSkyridingCandidate(player))
            return;

        SkyridingState& st = GetState(player);
        if ((spellId == SPELL_SURGE_FORWARD || spellId == SPELL_WHIRLING_SURGE
            || spellId == SPELL_AERIAL_HALT) && !player->IsFlying())
            return;
        if (spellId == SPELL_SURGE_FORWARD || spellId == SPELL_SKYWARD_ASCENT)
        {
            // Successful configured casts are consumed once by SpellChargeMgr. Keep the local
            // pool only as a compatibility fallback when no core definition was loaded.
            if (!UsesCoreVigor() && !TryConsumeVigor(player, st))
                return;
        }

        if (spellId == SPELL_SURGE_FORWARD)
        {
            st.flightRate = std::min(sMaxFlightRate, st.flightRate + sSurgeBoostRate);
            player->SetSpeed(MOVE_FLIGHT, st.flightRate, true);
            SendAddon(player, "ANIM\t0");
        }
        else if (spellId == SPELL_SKYWARD_ASCENT)
            ApplySkywardAscent(player, st);
        else if (spellId == SPELL_WHIRLING_SURGE)
        {
            ImpulseForward(player, 60.0f, 0.0f);
            st.whirlingCooldownMs = sWhirlingCooldownMs;
            st.flightRate = sMaxFlightRate;
            player->SetSpeed(MOVE_FLIGHT, st.flightRate, true);
            SendAddon(player, "ANIM\t2");
            SyncRate(player, st, 0, true);
            SyncAuxAbilities(player, st, true);
        }
        else if (spellId == SPELL_AERIAL_HALT)
        {
            st.aerialHaltCooldownMs = sAerialHaltCooldownMs;
            st.aerialHaltLeftMs = sAerialHaltDurationMs;
            st.flightRate = sAerialHaltRate;
            st.band = "halt";
            player->SetSpeed(MOVE_FLIGHT, st.flightRate, true);
            // RATE -2 is the WXL sentinel for four seconds of weightless halt.
            SendAddon(player, "RATE\t-2");
            SyncAuxAbilities(player, st, true);
        }
        else if (spellId == SPELL_SECOND_WIND)
        {
            if (UsesCoreSecondWind())
            {
                // SpellChargeMgr consumes category 2195 and its successful-cast bridge restores
                // one charge to category 2391. Mirrors refresh on the next player update.
                return;
            }
            if (st.secondWindCharges == 0 || st.vigor >= st.vigorMax)
                return;
            --st.secondWindCharges;
            if (st.secondWindRechargeLeftMs == 0)
                st.secondWindRechargeLeftMs = sSecondWindRechargeMs;
            ++st.vigor;
            if (st.vigor >= st.vigorMax)
                st.rechargeLeftMs = 0;
            SyncCharges(player, st, true);
            SyncAuxAbilities(player, st, true);
        }

        if (spellId == SPELL_SURGE_FORWARD)
            SyncRate(player, st, 0, true);
    }

    void ToggleFlightStyle(Player* player)
    {
        if (!player)
            return;

        SkyridingState& st = GetState(player);
        bool const wasSkyriding =
            player->HasAura(SPELL_FLIGHT_STYLE_SKYRIDING)
            && !player->HasAura(SPELL_FLIGHT_STYLE_STEADY);
        player->RemoveAurasDueToSpell(
            SPELL_FLIGHT_STYLE_SKYRIDING);
        player->RemoveAurasDueToSpell(
            SPELL_FLIGHT_STYLE_STEADY);
        if (wasSkyriding)
        {
            player->AddAura(
                SPELL_FLIGHT_STYLE_STEADY, player);
            player->RemoveAurasDueToSpell(SPELL_VIGOR);
            SendAddon(player, "STYLE\tSTEADY");
        }
        else
        {
            player->AddAura(
                SPELL_FLIGHT_STYLE_SKYRIDING, player);
            if (player->IsMounted() && player->CanFly())
                player->AddAura(SPELL_VIGOR, player);
            SendAddon(player, "STYLE\tSKYRIDING");
        }

        SyncMode(player, st, false);
        ApplyTurnRate(player, false);
        if (player->IsMounted())
        {
            player->Dismount();
            player->RemoveAurasByType(SPELL_AURA_MOUNTED);
        }
    }
}

class SkyridingWorldScript : public WorldScript
{
public:
    SkyridingWorldScript() : WorldScript("SkyridingWorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LoadConfig();
    }

    void OnStartup() override
    {
        LoadConfig();
        LOG_INFO("module",
            "mod-skyriding: enabled={} vigorMax={} rechargeMs={} mounts={} chargesAura={}",
            sEnabled, sVigorMax, sVigorRechargeMs,
            sAllowedMountDisplayIds.size(), sRequireFlightChargesAura);

        std::array<uint32, 13> const requiredSpells = {
            SPELL_WHIRLING_SURGE, SPELL_SURGE_FORWARD, SPELL_SKYWARD_ASCENT,
            SPELL_SKYRIDING_BASICS, SPELL_SKYRIDING_CHARGES, SPELL_LIFT_OFF,
            SPELL_THRILL_OF_THE_SKIES, SPELL_AERIAL_HALT,
            SPELL_FLIGHT_STYLE_SKYRIDING, SPELL_FLIGHT_STYLE_STEADY,
            SPELL_CHANGE_FLIGHT_STYLE, SPELL_SECOND_WIND, SPELL_VIGOR };
        for (uint32 spellId : requiredSpells)
        {
            if (!sSpellMgr->GetSpellInfo(spellId))
                LOG_ERROR("module", "mod-skyriding: Spell.dbc/spell_dbc is missing spell {}", spellId);
        }

        // Retail flight mode and Vigor are runtime state. Recreate them from
        // the selected mode on login instead of persisting stale mounted state.
        for (uint32 spellId : {
            SPELL_FLIGHT_STYLE_SKYRIDING,
            SPELL_FLIGHT_STYLE_STEADY,
            SPELL_VIGOR })
        {
            if (SpellInfo const* info =
                sSpellMgr->GetSpellInfo(spellId))
            {
                const_cast<SpellInfo*>(info)->AttributesCu |=
                    SPELL_ATTR0_CU_AURA_CANNOT_BE_SAVED;
            }
        }

        if (sRequireFlightChargesAura
            && !sSpellMgr->GetSpellInfo(SPELL_FLIGHT_CHARGES))
        {
            LOG_ERROR("module",
                "mod-skyriding: RequireFlightChargesAura is enabled but spell {} is missing",
                SPELL_FLIGHT_CHARGES);
        }
    }
};

class SkyridingPlayerScript : public PlayerScript
{
public:
    SkyridingPlayerScript() : PlayerScript("SkyridingPlayerScript") { }

    void OnPlayerLogin(Player* player) override
    {
        if (!sEnabled || !player)
            return;

        EnsureSpells(player);
        bool const restoredReconnect = RestoreReconnectState(player);
        SkyridingState& st = GetState(player);
        st.vigor = sVigorMax;
        st.vigorMax = sVigorMax;
        st.rechargeLeftMs = 0;
        st.flightRate = sBaseFlightRate;
        st.secondWindCharges = sSecondWindMaxCharges;
        st.secondWindMaxCharges = sSecondWindMaxCharges;
        st.secondWindRechargeLeftMs = 0;
        st.whirlingCooldownMs = 0;
        st.aerialHaltCooldownMs = 0;
        st.aerialHaltLeftMs = 0;
        st.thrillActive = false;
        st.appliedRechargeRatePermille = 0;
        st.auxSynced = false;
        st.modeSynced = false;
        st.chargesSynced = false;
        st.rateAccMs = 0;
        st.lastRateNorm = 2.0f;
        TickVigor(player, st, 0);
        TickAuxAbilities(player, st, 0);
        bool const active = IsSkyridingCandidate(player);
        SyncCharges(player, st, true);
        SyncMode(player, st, active);
        SyncAuxAbilities(player, st, true);
        st.wasFlying = player->IsFlying();
        st.wasAirborne = st.wasFlying;
        st.landSent = false;
        st.landGraceUntilMs = 0;
        st.grounded = false;
        st.reentryParaDelayMs = 0;
        st.reentryHandled = false;
        BeginClientHandshake(st);
        if (restoredReconnect)
        {
            st.reentryHandled = true;
            st.wasFlying = true;
            st.wasAirborne = true;
            st.flightRate = sBaseFlightRate;
            SyncRate(player, st, 0, true);
        }
        else
            ForceReentryDismountParachute(player, st);
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;
        RememberReconnectState(player);
        sStates.erase(player->GetGUID());
    }

    void OnPlayerMapChanged(Player* player) override
    {
        if (!sEnabled || !player)
            return;

        SkyridingState& st = GetState(player);
        st.wasFlying = player->IsFlying();
        st.wasAirborne = st.wasFlying;
        st.landGraceUntilMs = 0;
        st.grounded = false;
        st.reentryHandled = false;
        BeginClientHandshake(st);
        ForceReentryDismountParachute(player, st);
        SyncRate(player, st, 0, true);
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (!sEnabled || !player || !player->IsInWorld())
            return;

        EnsureFlightAuras(player);
        SkyridingState& st = GetState(player);
        TickAuxAbilities(player, st, diff);
        TickVigor(player, st, diff);
        SyncCharges(player, st);
        SyncAuxAbilities(player, st);
        TickClientHandshake(player, st);
        TickReentryParachute(player, st, diff);
        TickReentryGroundClear(player, st);

        bool const candidate = IsSkyridingCandidate(player);
        bool const flying = player->IsFlying();

        if (!candidate)
            st.grounded = false;

        if (candidate)
            EnsureSpells(player);

        // Mode ON on ground+air so client gates Space before takeoff; takeoff = Skyward.
        SyncMode(player, st, candidate);
        ApplyTurnRate(player, candidate && flying);

        if (candidate && !sClassicVertical)
        {
            player->m_movementInfo.RemoveMovementFlag(
                MOVEMENTFLAG_ASCENDING | MOVEMENTFLAG_DESCENDING);
            // Ground Space tries ASC+FLY in one frame. Preserve fall-into-fly.
            bool const falling = player->HasUnitMovementFlag(
                MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);
            if (!flying && !st.wasFlying && !falling)
                player->m_movementInfo.RemoveMovementFlag(
                    MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY);
        }

        if (candidate && flying)
        {
            TickMomentum(player, st, diff);
            TickTerrainTouchdown(player, st);
        }
        else if (st.wasFlying && !flying)
        {
            st.flightRate = sBaseFlightRate;
            st.braking = false;
            ApplyTurnRate(player, false);
            TickTerrainTouchdown(player, st);
        }
        else if (candidate)
            TickTerrainTouchdown(player, st);

        st.wasFlying = flying;
    }

    static void HandleClientCommand(Player* player, std::string const& body)
    {
        if (!sEnabled || !player || body.empty() || body.size() > 128)
            return;
        // BRK\t0|1 — S held while skyriding (client cannot hover-stop).
        if (body.rfind("BRK\t", 0) == 0)
        {
            SkyridingState& st = GetState(player);
            st.braking = (body.size() > 4 && body[4] == '1');
        }
        else if (body.rfind("WALL\t", 0) == 0)
        {
            SkyridingState& st = GetState(player);
            if (!IsSkyridingCandidate(player)
                || getMSTime() < st.landGraceUntilMs || !player->IsFlying())
                return;

            st.flightRate = sMinFlightRate;
            st.braking = false;
            st.band = "stall";
            player->SetSpeed(MOVE_FLIGHT, st.flightRate, true);
            SyncRate(player, st, 0, true);
        }
        else if (body.rfind("LAND\t", 0) == 0)
        {
            SkyridingState& st = GetState(player);
            bool const candidate = IsSkyridingCandidate(player);
            if (!candidate)
                return;
            ForceTouchdown(player, st);
        }
        else if (body == "TAKEOFF")
        {
            // Retail Lift Off: the second Space casts the actual Skyward Ascent
            // spell. This shares the same vigor/cooldown checks as the action bar.
            SkyridingState& st = GetState(player);
            bool const candidate = IsSkyridingCandidate(player);
            uint32 const now = getMSTime();
            bool const throttled = st.lastTakeoffMs != 0
                && getMSTimeDiff(st.lastTakeoffMs, now) < 750;
            LOG_INFO("module",
                "mod-skyriding [TAKEOFF TRACE]: request player={} candidate={} flying={} "
                "falling={} grounded={} wasAirborne={} throttled={} flags=0x{:08X}",
                player->GetGUID().ToString(), candidate, player->IsFlying(),
                player->HasUnitMovementFlag(
                    MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR),
                st.grounded, st.wasAirborne, throttled,
                player->m_movementInfo.GetMovementFlags());
            if (!candidate)
            {
                LOG_INFO("module",
                    "mod-skyriding [TAKEOFF TRACE]: rejected player={} reason=not candidate",
                    player->GetGUID().ToString());
                return;
            }
            if (throttled)
            {
                LOG_INFO("module",
                    "mod-skyriding [TAKEOFF TRACE]: rejected player={} reason=throttle",
                    player->GetGUID().ToString());
                return;
            }

            SpellCastResult const result =
                player->CastSpell(player, SPELL_SKYWARD_ASCENT, false);
            if (result == SPELL_CAST_OK)
            {
                st.lastTakeoffMs = now;
                LOG_INFO("module",
                    "mod-skyriding [TAKEOFF TRACE]: accepted player={} spell={} rate={:.2f}",
                    player->GetGUID().ToString(), SPELL_SKYWARD_ASCENT,
                    st.flightRate);
            }
            else
            {
                LOG_INFO("module",
                    "mod-skyriding [TAKEOFF TRACE]: rejected player={} "
                    "reason=spell cast result={}",
                    player->GetGUID().ToString(), uint32(result));
            }
        }
        else if (body.rfind("RESYNC\t", 0) == 0)
        {
            SkyridingState& st = GetState(player);
            st.wasFlying = player->IsFlying();
            st.wasAirborne = st.wasFlying;
            st.reentryHandled = false;
            BeginClientHandshake(st, 3000);
            SyncCharges(player, st, true);
            SyncAuxAbilities(player, st, true);
            SyncMode(player, st, IsSkyridingCandidate(player));
            ForceReentryDismountParachute(player, st);
            SyncRate(player, st, 0, true);
        }
    }

    void OnPlayerBeforeSendChatMessage(Player* player, uint32& /*type*/,
        uint32& lang, std::string& msg) override
    {
        // Native input always wins. Addon input exists only as a compatibility
        // fallback when the opcode bridge is explicitly disabled.
        if (!sDevAddonMessages || sNativePackets || lang != LANG_ADDON)
            return;
        size_t const tabPos = msg.find('\t');
        if (tabPos == std::string::npos || msg.substr(0, tabPos) != ADDON_PREFIX)
            return;
        HandleClientCommand(player, msg.substr(tabPos + 1));
    }
};

class SkyridingPacketScript final : public ServerScript
{
public:
    SkyridingPacketScript()
        : ServerScript("SkyridingPacketScript", {SERVERHOOK_CAN_PACKET_RECEIVE}) { }

    bool CanPacketReceive(WorldSession* session, WorldPacket& packet) override
    {
        if (packet.GetOpcode() == CMSG_MOVE_ADD_IMPULSE_ACK)
        {
            if (!session || !session->GetPlayer()
                || packet.size() != sizeof(uint64) + sizeof(uint32))
                return false;

            uint64 moverGuid = 0;
            uint32 sequence = 0;
            packet >> moverGuid >> sequence;
            Player* player = session->GetPlayer();
            if (moverGuid != player->GetGUID().GetRawValue()
                || sequence == 0)
                return false;

            SkyridingState& st = GetState(player);
            if (sequence >= st.lastImpulseAck)
                st.lastImpulseAck = sequence;
            LOG_DEBUG("module",
                "mod-skyriding: MOVE_ADD_IMPULSE_ACK seq={} for {}",
                sequence, player->GetGUID().ToString());
            return false;
        }

        if (packet.GetOpcode() != CMSG_WXL_SKYRIDING)
            return true;

        if (!sNativePackets || !session || !session->GetPlayer()
            || packet.size() < sizeof(uint32) || packet.size() > 132)
            return false;

        uint32 length = 0;
        packet >> length;
        if (length == 0 || length > 128 || packet.rpos() + length != packet.size())
            return false;
        std::string body;
        body.resize(length);
        packet.read(reinterpret_cast<uint8*>(&body[0]), length);
        SkyridingPlayerScript::HandleClientCommand(session->GetPlayer(), body);
        return false;
    }
};

class SkyridingMovementScript : public MovementHandlerScript
{
public:
    SkyridingMovementScript()
        : MovementHandlerScript("SkyridingMovementScript",
            { MOVEMENTHOOK_ON_PLAYER_MOVE })
    {
    }

    void OnPlayerMove(Player* player, MovementInfo /*movementInfo*/, uint32 /*opcode*/) override
    {
        if (!sEnabled || sClassicVertical || !player)
            return;
        if (!IsSkyridingCandidate(player))
            return;

        SkyridingState& st = GetState(player);
        bool const wasFlying = player->IsFlying();
        bool const falling = player->HasUnitMovementFlag(
            MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);
        TickTerrainTouchdown(player, st);
        player->m_movementInfo.RemoveMovementFlag(
            MOVEMENTFLAG_ASCENDING | MOVEMENTFLAG_DESCENDING);

        // Mirror client gate on every move packet (ground + air).
        // Allow fall-into-fly: only strip bogus FLYING on solid ground.
        if (!wasFlying && !falling)
            player->m_movementInfo.RemoveMovementFlag(
                MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY);
    }
};

class SkyridingSpellScript : public AllSpellScript
{
public:
    SkyridingSpellScript()
        : AllSpellScript("SkyridingSpellScript",
            { ALLSPELLHOOK_ON_SPELL_CHECK_CAST, ALLSPELLHOOK_ON_CAST })
    {
    }

    void OnSpellCheckCast(Spell* spell, bool /*strict*/, SpellCastResult& res) override
    {
        if (!sEnabled || !spell || res != SPELL_CAST_OK)
            return;

        SpellInfo const* info = spell->GetSpellInfo();
        if (!info)
            return;
        if (info->Id == SPELL_CHANGE_FLIGHT_STYLE)
        {
            Unit* caster = spell->GetCaster();
            Player* player = caster ? caster->ToPlayer() : nullptr;
            if (!player || player->IsFlying() || player->HasUnitMovementFlag(
                MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR))
                res = SPELL_FAILED_NOT_ON_GROUND;
            return;
        }

        if (info->Id != SPELL_SURGE_FORWARD && info->Id != SPELL_SKYWARD_ASCENT
            && info->Id != SPELL_WHIRLING_SURGE && info->Id != SPELL_AERIAL_HALT
            && info->Id != SPELL_SECOND_WIND)
            return;

        Unit* caster = spell->GetCaster();
        Player* player = caster ? caster->ToPlayer() : nullptr;
        if (!player || !IsSkyridingCandidate(player))
        {
            res = SPELL_FAILED_ONLY_MOUNTED;
            return;
        }

        if ((info->Id == SPELL_SURGE_FORWARD || info->Id == SPELL_WHIRLING_SURGE
            || info->Id == SPELL_AERIAL_HALT) && !player->IsFlying())
        {
            res = SPELL_FAILED_NOT_ON_GROUND;
            return;
        }

        SkyridingState& st = GetState(player);
        RefreshCoreVigor(player, st);
        RefreshCoreSecondWind(player, st);
        if ((info->Id == SPELL_SURGE_FORWARD || info->Id == SPELL_SKYWARD_ASCENT)
            && st.vigor == 0)
            res = SPELL_FAILED_DONT_REPORT;
        else if (info->Id == SPELL_WHIRLING_SURGE && st.whirlingCooldownMs != 0)
            res = SPELL_FAILED_NOT_READY;
        else if (info->Id == SPELL_AERIAL_HALT && st.aerialHaltCooldownMs != 0)
            res = SPELL_FAILED_NOT_READY;
        else if (info->Id == SPELL_SECOND_WIND
            && (st.secondWindCharges == 0 || st.vigor >= st.vigorMax))
            res = SPELL_FAILED_NOT_READY;
    }

    void OnSpellCast(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo,
        bool /*skipCheck*/) override
    {
        if (!sEnabled || !caster || !spellInfo)
            return;
        if (spellInfo->Id == SPELL_CHANGE_FLIGHT_STYLE)
        {
            if (Player* player = caster->ToPlayer())
                ToggleFlightStyle(player);
            return;
        }

        if (spellInfo->Id != SPELL_SURGE_FORWARD && spellInfo->Id != SPELL_SKYWARD_ASCENT
            && spellInfo->Id != SPELL_WHIRLING_SURGE && spellInfo->Id != SPELL_AERIAL_HALT
            && spellInfo->Id != SPELL_SECOND_WIND)
            return;

        Player* player = caster->ToPlayer();
        if (!player)
            return;

        HandleSkyridingAbility(player, spellInfo->Id);
    }
};

void AddSC_mod_skyriding()
{
    new SkyridingWorldScript();
    new SkyridingPlayerScript();
    new SkyridingPacketScript();
    new SkyridingMovementScript();
    new SkyridingSpellScript();
}
