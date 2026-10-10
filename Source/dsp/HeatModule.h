// DrumSpread DSP core - HEAT: six saturation modes, oversampled.
#pragma once

#include "ModuleSlot.h"
#include "Oversampler.h"

namespace ds
{
class HeatModule
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

    /** Static (memoryless) transfer curve of a mode, used for level compensation
        and by the test harness. */
    static float staticCurve (int mode, float u) noexcept;

private:
    struct ChState
    {
        SvfState tPre, tDe, tBump; // tape
        float tLag = 0.0f;
        float env = 0.0f;          // tube
        float fenv = 0.0f;         // fuzz
        double phi = 0.0, eState = 0.0, ePrev = 0.0; // transformer
        SvfState dPre, dDe;        // diode
        float held = 0.0f, phase = 1.0f; // crush
    };

    float runMode (int mode, ChState& s, float x, float drive, float comp) noexcept;
    float computeComp (int mode, float drive) const noexcept;

    double sr = 48000.0, osr = 96000.0;
    Oversampler os;
    ChState st[2];
    ModeFader fader;
    float fadeLen = 1000.0f;

    // OS-rate coefficients
    SvfCoeffs tapePre, tapeDe, tapeBump, diodePre, diodeDe;
    float envAtt = 0.0f, envRel = 0.0f, fuzzEnvA = 0.0f;
    double trA = 0.0; // transformer integrator pole

    // smoothed controls
    ChunkSmoother driveSm, toneSm, trimSm;
    LinRamp driveRamp; // linear gain, OS-rate ramp
    float compCur[kNumHeatModes] {}, compPrev[kNumHeatModes] {};
    LinRamp compRampCur, compRampPrev;
    float holdLen = 1.0f; // crush hold length in OS samples
    float bitsLevels = 32768.0f;
    int pendingMode = 0;
    bool first = true;

    // base-rate post
    TiltEq tilt[2];
    DcBlocker dc[2];
    Ramp trimRamp;
    float lastTilt = 0.0f;
};

} // namespace ds
