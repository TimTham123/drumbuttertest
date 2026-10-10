// DrumSpread DSP core - PRESS: six drum-oriented compressor characters, oversampled.
#pragma once

#include "ModuleSlot.h"
#include "Oversampler.h"

namespace ds
{
class PressModule
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

    /** Max gain reduction (dB, positive) since the last call. */
    float takeGrMeter() noexcept
    {
        const float g = grMeter;
        grMeter = 0.0f;
        return g;
    }

    /** Static gain computer (exposed for the test harness). Returns GR in dB >= 0. */
    static float gainComputer (float levelDb, float threshDb, float ratio, float kneeDb) noexcept;

    struct TypeSettings
    {
        float thresh = 0.0f, ratio = 2.0f, knee = 6.0f;
        float attackSec = 0.01f, releaseSec = 0.2f;
        bool autoRelease = false;
        float autoFastSec = 0.1f;
        float makeupDb = 0.0f;
        bool allButtons = false;
    };
    /** Maps the front-panel controls to a type's settings (exposed for tests). */
    static TypeSettings mapControls (int type, float amount01, float attack01, float release01, bool softKnee,
                                     bool allButtons, bool autoMakeup) noexcept;

private:
    struct TypeState
    {
        float gr = 0.0f, grFast = 0.0f, grSlow = 0.0f;
        float hold = 0.0f, mem = 0.0f, fbLevel = 0.0f;
        float aAtt = 0.0f, aRel = 0.0f, aRelSlow = 0.0f, aRelFast = 0.0f, mkLin = 1.0f;
        TypeSettings set;
    };

    void updateCoefs (TypeState& s) noexcept;
    /** Runs one type for one stereo OS sample; returns the output pair. */
    void runType (int type, TypeState& s, float xL, float xR, float scL, float scR, float& yL, float& yR) noexcept;
    float color (int type, const TypeState& s, float y) const noexcept;

    double sr = 48000.0, osr = 96000.0;
    Oversampler os;
    TypeState ts[kNumPressTypes];
    ModeFader fader;
    float fadeLen = 1000.0f;
    StereoSvf scHpf;
    bool scOn = true;
    ChunkSmoother amountSm, scSm;
    float holdA = 0.0f, memA = 0.0f;
    float grMeter = 0.0f;
    DcBlocker dc[2];
    bool first = true;
};

} // namespace ds
