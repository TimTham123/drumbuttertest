// DrumSpread DSP core - LAYER: triggered sub oscillator (+growl) and triggered noise layer.
#pragma once

#include "ModuleSlot.h"
#include "Oversampler.h"
#include <atomic>

namespace ds
{
class LayerModule
{
public:
    static constexpr bool kSendTarget = false;
    static int maxLatency (double) { return 0; }

    void prepare (double sampleRate);
    void configure (int osFactor, int warpMode);
    void reset();
    int latency() const noexcept { return 0; }
    void control (const float* p, const ModCtx& ctx) noexcept;
    void process (const float* inL, const float* inR, float* outL, float* outR, const float* w, int n) noexcept;

    /** Trigger counters (incremented on every trigger; read by UI LEDs and tests). */
    uint32_t subTriggerCount() const noexcept { return subTrigs; }
    uint32_t noiseTriggerCount() const noexcept { return noiseTrigs; }
    /** Sample index (since reset) of the most recent triggers - for tests. */
    int64_t lastSubTriggerSample() const noexcept { return lastSubAt; }
    int64_t lastNoiseTriggerSample() const noexcept { return lastNoiseAt; }

    /** Optional trigger log (test harness only; not used on the audio thread by the plugin). */
    void setTriggerLog (std::vector<int64_t>* subLog, std::vector<int64_t>* noiseLog) noexcept
    {
        subLogPtr = subLog;
        noiseLogPtr = noiseLog;
    }

private:
    struct SubVoice
    {
        bool active = false;
        double phase = 0.0;  // radians
        float t = 0.0f;      // seconds since trigger
        float amp = 0.0f;    // velocity * level
        float env = 0.0f;
        float fade = 1.0f;   // retrigger fade-out multiplier
        float pitchEnv = 1.0f;
        float atkPos = 0.0f;
        bool releasing = false;
    };
    struct NoiseVoice
    {
        float env = 0.0f;
        float vel = 0.0f;
        float attack = 0.0f;
        bool kill = false;
    };

    void triggerSub (float phase0, float velocity) noexcept;
    float noiseSample (int type, int ch) noexcept;

    double sr = 48000.0;
    int osFactor = 1;
    Oversampler decim;
    OnsetTrigger subTrig, noiseTrig;
    SubVoice voices[2];
    int nextVoice = 0;
    std::vector<float> subOs; // oversampled sub buffer (kChunk * kMaxOS)
    float subBase[kChunk] {};

    // sub controls
    float subFreq = 52.0f, subDrop = 7.0f, subDecay = 0.22f, growl = 0.0f;
    float subDecayCoef = 0.0f, subLevel = 0.25f;
    float pendingVelocity = 0.0f;
    int pendingVoice = -1;
    int noisePending = 0, noiseDecisionSamples = 384, noiseVetoBefore = 144;
    int64_t noisePendingAt = 0;
    float noiseKillCoef = 0.99f;

    // noise
    Rng rng[2] { Rng (0x1234567u), Rng (0x7654321u) };
    float pink[2][7] {};
    float crackleHp[2] {};
    SvfCoeffs bandC;
    SvfState bandS[2];
    OnePoleLP toneLp[2][kNumNoiseTypes];
    float toneMixLow = 0.5f, toneMixHigh = 0.5f;
    NoiseVoice nv;
    float noiseDecayCoef = 0.0f, noiseLevel = 0.06f, noiseAttackInc = 0.0f;
    ModeFader typeFader;
    float typeFadeLen = 960.0f;

    ChunkSmoother subLevelSm, noiseLevelSm, toneSm, growlSm;
    Ramp subLevelRamp, noiseLevelRamp;
    uint32_t subTrigs = 0, noiseTrigs = 0;
    int64_t sampleCount = 0, lastSubAt = -1, lastNoiseAt = -1;
    std::vector<int64_t>* subLogPtr = nullptr;
    std::vector<int64_t>* noiseLogPtr = nullptr;
    bool first = true;
};

} // namespace ds
