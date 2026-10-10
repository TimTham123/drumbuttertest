// DrumSpread DSP core - the complete processing engine.
#pragma once

#include "HeatModule.h"
#include "LayerModule.h"
#include "MasterSection.h"
#include "ModuleSlot.h"
#include "PressModule.h"
#include "RoomModule.h"
#include "SmashModule.h"
#include "Telemetry.h"
#include "WarpModule.h"

#include <array>

namespace ds
{
class Engine
{
public:
    Engine();

    /** Allocates everything. Not real-time safe. */
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Copies a full parameter array (kNumParams values, real units). Call at block start. */
    void setParameters (const float* values) noexcept;
    const float* parameters() const noexcept { return params; }

    /** Processes n samples. inR may be nullptr (mono input). Real-time safe. */
    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;

    /** Latency of the currently active configuration (base-rate samples). */
    int latencySamples() const noexcept { return activeLatency; }
    /** Latency that the given parameter set will result in. */
    static int latencyFor (double sampleRate, int osIndex, int warpMode);
    int latencyForParams (const float* p) const;

    Telemetry& telemetry() noexcept { return tele; }
    double sampleRate() const noexcept { return sr; }

    // --- test hooks ----------------------------------------------------------
    LayerModule& layerModule() noexcept { return layer.mod; }
    bool isCrossfadingOrder() const noexcept { return xfading; }
    std::array<int, 4> currentOrder() const noexcept { return order; }
    /** Disables the reorder crossfade (instant switch) - test harness only. */
    void setInstantReorderForTest (bool b) noexcept { instantReorder = b; }

private:
    struct Bank
    {
        ModuleSlot<WarpModule> warp;
        ModuleSlot<RoomModule> room;
        ModuleSlot<PressModule> press;
        ModuleSlot<HeatModule> heat;
    };

    void controlTick() noexcept;
    void processSegment (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;
    void processBank (Bank& b, const std::array<int, 4>& ord, float* L, float* R, int n) noexcept;
    void controlBank (Bank& b, float blend, const ModCtx& ctx) noexcept;
    void applyConfig (int os, int warpMode);
    int computeChainLatency() const noexcept;

    double sr = 48000.0;
    int maxBlock = 512;
    float params[kNumParams] {};

    ModuleSlot<LayerModule> layer;
    Bank banks[2];
    ModuleSlot<SmashModule> smash;
    MasterSection master;
    int active = 0;
    std::array<int, 4> order { 0, 2, 3, 1 }, nextOrder { 0, 2, 3, 1 };
    bool xfading = false, instantReorder = false;
    float xfPos = 0.0f, xfInc = 0.0f;
    int xfWarm = 0; // samples the new-order bank runs (silently) before the crossfade starts

    FixedDelay dryDelay[2];
    int activeOs = 2, activeWarpMode = 0, activeLatency = 0;

    enum CfgState
    {
        CfgIdle,
        CfgFadingOut,
        CfgHolding,
        CfgFadingIn
    };
    CfgState cfgState = CfgIdle;
    int cfgHold = 0;
    float cfgGain = 1.0f, cfgStep = 0.01f;
    Ramp cfgRamp;

    ChunkSmoother inGainSm, outGainSm, dryWetSm, ceilSm;
    Ramp inRamp, outRamp, dwRamp, ceilRamp, clipRamp, bypassRamp;
    float clipMix = 1.0f, bypassMix = 0.0f, toggleStep = 0.01f;

    int64_t sampleCounter = 0;
    bool firstTick = true;

    // scratch (kChunk)
    float bufL[kChunk] {}, bufR[kChunk] {}, xfL[kChunk] {}, xfR[kChunk] {};
    float meterIn[2] {}, meterOut[2] {}, meterDecay = 0.999f;

    Telemetry tele;
};

} // namespace ds
