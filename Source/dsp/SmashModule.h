// DrumSpread DSP core - SMASH: waveshaper (5 curves, drive, bias) -> soft/hard clipper.
#pragma once

#include "ModuleSlot.h"
#include "Oversampler.h"

namespace ds
{
class SmashModule
{
public:
    static constexpr bool kSendTarget = false;
    static int maxLatency (double) { return Oversampler::latencyFor (kMaxOS); }

    void prepare (double sampleRate);
    void configure (int osFactor, int warpMode);
    void reset();
    int latency() const noexcept { return Oversampler::latencyFor (os.factor()); }
    void control (const float* p, const ModCtx& ctx) noexcept;
    void process (const float* inL, const float* inR, float* outL, float* outR, const float* w, int n) noexcept;

    static float curve (int c, float u) noexcept;

private:
    float shapeAndClip (int mode, float x, float g, float bias, float comp, float ceil) const noexcept;
    float computeComp (int c, float g, float bias) const noexcept;

    double sr = 48000.0;
    Oversampler os;
    ModeFader fader; // mode = curve*2 + clipMode
    float fadeLen = 1000.0f;
    ChunkSmoother driveSm, biasSm, ceilSm, trimSm;
    LinRamp driveRamp, biasRamp, ceilRamp, compCur, compPrev;
    Ramp trimRamp;
    DcBlocker dc[2];
    bool first = true;
};

} // namespace ds
