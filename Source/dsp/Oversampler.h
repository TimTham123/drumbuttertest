// DrumSpread DSP core - linear-phase polyphase halfband FIR oversampling (1x/2x/4x).
#pragma once

#include "DspCommon.h"
#include <vector>

namespace ds
{
/** One halfband FIR stage (length N = 4K+3, Kaiser-windowed sinc). Polyphase:
    one branch of the halfband is a pure delay, so only M = (N+1)/2 taps are
    computed per output pair. */
class HalfbandStage
{
public:
    /** Linear history buffers: [M-1 previous inputs | up to kMaxBlock new inputs]. */
    static constexpr int kMaxBlock = 64;
    struct UpState
    {
        std::vector<float> hist;
        int idx = 0;
    };
    struct DownState
    {
        std::vector<float> histE, histO;
        int idx = 0;
    };

    void design (int numTaps, float beta);
    void initUp (UpState& s) const { s.hist.assign ((size_t) (M + kMaxBlock), 0.0f); s.idx = 0; }
    void initDown (DownState& s) const
    {
        s.histE.assign ((size_t) (M + kMaxBlock), 0.0f);
        s.histO.assign ((size_t) (M + kMaxBlock), 0.0f);
        s.idx = 0;
    }
    static void resetUp (UpState& s) noexcept { std::fill (s.hist.begin(), s.hist.end(), 0.0f); s.idx = 0; }
    static void resetDown (DownState& s) noexcept
    {
        std::fill (s.histE.begin(), s.histE.end(), 0.0f);
        std::fill (s.histO.begin(), s.histO.end(), 0.0f);
        s.idx = 0;
    }

    /** n input samples -> 2n output samples. */
    void up (UpState& s, const float* in, float* out, int n) const noexcept;
    /** 2*nOut input samples -> nOut output samples. */
    void down (DownState& s, const float* in, float* out, int nOut) const noexcept;

    int numTaps() const noexcept { return N; }
    /** Group delay of one up- or down-sampling pass, in samples at the higher rate. */
    int groupDelayHi() const noexcept { return (N - 1) / 2; }
    const std::vector<float>& impulse() const noexcept { return h; }

private:
    int N = 0, M = 0, K = 0;
    std::vector<float> h;        // full prototype (for analysis)
    std::vector<float> tapsUp;   // 2*h[2m]
    std::vector<float> tapsDown; // h[2m]
};

/** Stereo oversampler with selectable factor. Process in chunks of up to
    maxBase base-rate samples. */
class Oversampler
{
public:
    void prepare (int maxBaseSamples);
    void setFactor (int f) noexcept; // 1, 2 or 4 - resets filter state
    int factor() const noexcept { return fac; }
    void reset() noexcept;

    /** Round-trip (up + process + down) latency in base-rate samples. */
    static int latencyFor (int f) noexcept { return f == 2 ? 25 : (f == 4 ? 32 : 0); }
    int latency() const noexcept { return latencyFor (fac); }
    /** Group delay of the decimation-only path (synthesized signals), base samples. */
    static float decimDelayFor (int f) noexcept { return f == 2 ? 12.5f : (f == 4 ? 15.75f : 0.0f); }

    /** Upsamples n base samples (stereo) into the internal OS buffers. */
    void upsample (const float* inL, const float* inR, int n) noexcept;
    float* buffer (int ch) noexcept { return osBuf[(size_t) ch].data(); }
    /** Downsamples the internal OS buffers (n*factor samples) to n base samples. */
    void downsample (float* outL, float* outR, int n) noexcept;
    /** Decimates a mono OS-rate signal (n*factor samples, from 'in') to n samples. Uses
        an independent state, so it can be used alongside up/downsample. */
    void decimateMono (const float* in, float* out, int n) noexcept;

    static const HalfbandStage& stageA();
    static const HalfbandStage& stageB();

private:
    int fac = 1, maxBase = 0;
    HalfbandStage::UpState upA[2], upB[2];
    HalfbandStage::DownState dnA[2], dnB[2], monoA, monoB;
    std::vector<float> osBuf[2], midBuf[2], monoMid;
    float pad[2][2] {}; // 2-sample delay at 4x to make the 4x latency an integer
};

} // namespace ds
