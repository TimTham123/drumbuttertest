// DrumSpread DSP core - ROOM: Room / Plate / Spring / Gated / Vintage ambience.
#pragma once

#include "DelayLine.h"
#include "ModuleSlot.h"

namespace ds
{
/** 8-line feedback delay network with Householder mixing and Jot absorption filters. */
class Fdn8
{
public:
    void prepare (float sampleRate, float maxSeconds);
    void reset();
    /** Per-chunk: decay (T60, s), HF T60 ratio, size scale. */
    void setDecay (float t60, float hfRatio, float sizeScale) noexcept;
    void process (float inL, float inR, float sizeScale, float& outL, float& outR) noexcept;
    float meanDelaySec (float sizeScale) const noexcept;

private:
    static constexpr int N = 8;
    float sr = 48000.0f;
    DelayBuffer line[N];
    AllpassDelay diff[2][2];
    float lp[N] {}, ca[N] {}, cb[N] {};
};

/** Dattorro figure-of-eight plate tank (JAES 1997), lengths scaled to the sample rate. */
class DattorroPlate
{
public:
    void prepare (float sampleRate);
    void reset();
    void setParams (float t60, float damping, float sizeScale) noexcept;
    void process (float inL, float inR, float sizeScale, float& outL, float& outR) noexcept;
    float loopSec (float sizeScale) const noexcept;

private:
    float sr = 48000.0f, k = 1.0f; // k = sr / 29761
    AllpassDelay inAp[4];
    AllpassDelay tankAp1[2], tankAp2[2];
    DelayBuffer d1[2], d2[2];
    float damp[2] {};
    float decay = 0.5f, dampCoef = 0.3f, decayDiff2 = 0.5f;
    double lfoPhase = 0.0;
    float lfoInc = 0.0f;
    float fb[2] {};
};

/** Two dispersive springs (allpass chains in feedback loops). */
class SpringTank
{
public:
    void prepare (float sampleRate);
    void reset();
    void setParams (float t60, float sizeScale, float tone) noexcept;
    void process (float inL, float inR, float& outL, float& outR) noexcept;
    float loopSec() const noexcept { return loopBaseSec; }

private:
    static constexpr int kStages = 96;
    float sr = 48000.0f;
    float apCoef[2] {};
    float apX[2][kStages] {}, apY[2][kStages] {};
    DelayBuffer loop[2];
    float loopDelay[2] {}, loopGain = 0.5f, loopBaseSec = 0.035f;
    OnePoleLP lp[2];
    SvfCoeffs hpC;
    SvfState hpS[2];
    Rng rng { 0xabcdefu };
    float modTarget[2] {}, modCur[2] {};
    int modCounter = 0;
};

/** 4-line chorused FDN with band-limited, quantised wet output ("vintage digital"). */
class VintageVerb
{
public:
    void prepare (float sampleRate);
    void reset();
    void setParams (float t60, float tone, float sizeScale) noexcept;
    void process (float inL, float inR, float sizeScale, float& outL, float& outR) noexcept;
    float meanDelaySec (float sizeScale) const noexcept;

private:
    static constexpr int N = 4;
    float sr = 48000.0f;
    DelayBuffer line[N];
    AllpassDelay diff[2];
    float lp[N] {}, ca[N] {}, cb[N] {};
    double lfo[N] {};
    float lfoInc[N] {};
    OnePoleLP bw[2][2];
};

class RoomModule
{
public:
    static constexpr bool kSendTarget = true;
    static int maxLatency (double) { return 0; }

    void prepare (double sampleRate);
    void configure (int osFactor, int warpMode);
    void reset();
    int latency() const noexcept { return 0; }
    void control (const float* p, const ModCtx& ctx) noexcept;
    void process (const float* inL, const float* inR, float* outL, float* outR, const float* w, int n) noexcept;

    /** Per-type wet normalisation constant (calibrated by the test harness). */
    static float typeGain (int type) noexcept;

private:
    void runType (int type, float inGain, float preL, float preR, float& wL, float& wR) noexcept;
    float typeNorm (int type) const noexcept;

    double sr = 48000.0;
    DelayBuffer pre[2];
    OnePoleLP bwLp[2];
    Fdn8 roomFdn, gateFdn;
    DattorroPlate plate;
    SpringTank spring;
    VintageVerb vintage;
    // type switch: the new engine's input fades in (50 ms) while the old engine's
    // input fades out and its tail rings on, fading out over a further 250 ms
    int curType = 0, prevType = 0;
    bool xfActive = false;
    int xfCount = 0, xfInLen = 2400, xfOutLen = 12000;

    ChunkSmoother sizeSm, decaySm, preSm, toneSm, widthSm, levelSm;
    Ramp sizeRamp, preRamp, widthRamp, levelRamp;
    float roomScale = 1.0f, plateScale = 1.0f, vintageScale = 1.0f;

    // gate
    EnvFollower gateKey;
    float gateGain = 0.0f, gateAtt = 0.0f, gateRel = 0.0f;
    int gateHold = 0, gateHoldSamples = 0;

    float norm[kNumRoomTypes] {};
    float decaySec = 0.8f;
    bool first = true;
};

} // namespace ds
