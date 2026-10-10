// DrumSpread DSP core - WARP: independent pitch & formant shift (phase vocoder,
// transient phase reset, cepstral formant envelope). Latency = N - 1 samples.
#pragma once

#include "FFT.h"
#include "ModuleSlot.h"

namespace ds
{
class WarpModule
{
public:
    static constexpr bool kSendTarget = false;
    static int frameSize (double sr, int mode) { return (mode == kWarpLoFi ? 256 : 1024) * srScale (sr); }
    static int latencyFor (double sr, int mode) { return frameSize (sr, mode) - 1; }
    static int maxLatency (double sr) { return latencyFor (sr, kWarpHQ); }

    void prepare (double sampleRate);
    void configure (int osFactor, int warpMode);
    void reset();
    int latency() const noexcept { return N - 1; }
    void control (const float* p, const ModCtx& ctx) noexcept;
    void process (const float* inL, const float* inR, float* outL, float* outR, const float* w, int n) noexcept;

    int transientFrames() const noexcept { return transientCount; }

private:
    void processFrame() noexcept;
    void analyse (int ch) noexcept;
    void remap (int ch) noexcept;                          // pitch remap + formant correction
    void synthesise (int ch, bool resetPhase, float gain) noexcept; // phases, IFFT, OLA
    void computeEnvelope (const float* mag, float* env) noexcept;

    double sr = 48000.0;
    int mode = kWarpHQ;
    int N = 1024, H = 256, nb = 513, mask = 1023;
    int nMaxAlloc = 1024;
    RealFFT fft[2]; // [0] = HQ size, [1] = Lo-Fi size
    int fftIdx = 0;

    std::vector<float> win;
    std::vector<float> inRing[2], outAcc[2];
    int pos = 0, hopCount = 0;

    // per-channel spectral state
    std::vector<float> prevPhase[2], synPhase[2];
    std::vector<float> mag[2], phs[2], frq[2];
    // work
    std::vector<float> frame, re, im, cep, envA, lift, avgMag;
    std::vector<float> outMag[2], outFrq[2];
    std::vector<int> srcBin[2];
    float energyGain = 1.0f; // frame-wise loudness matching (smoothed across frames)
    // slow time-domain loudness match of the PV output against the (aligned) input:
    // compensates the overlap-add coherence loss of decorrelated frames (~3 dB)
    float agcIn = 0.0f, agcOut = 0.0f, agcGain = 1.0f, agcA = 0.0f, agcGA = 0.0f;
    float prevEnergy = 0.0f;
    bool envValid = false;
    int framesSinceTransient = 100;
    int transientCount = 0;

    // controls
    ChunkSmoother pitchSm, formSm;
    float P = 1.0f, F = 1.0f;
    bool wantPV = false, pvRunning = false, resetNext = true;
    int warm = 0;
    float pvGain = 0.0f, pvStep = 0.001f;
    bool first = true;
    bool initialised = false;
};

} // namespace ds
