#include "WarpModule.h"

namespace ds
{
namespace
{
    inline float princarg (float x) noexcept
    {
        return x - kTwoPi * std::floor ((x + kPi) * (1.0f / kTwoPi));
    }
} // namespace

void WarpModule::prepare (double sampleRate)
{
    sr = sampleRate;
    const int nHQ = frameSize (sr, kWarpHQ);
    const int nLF = frameSize (sr, kWarpLoFi);
    nMaxAlloc = nHQ;
    fft[0].prepare (nHQ);
    fft[1].prepare (nLF);
    const size_t nmax = (size_t) nHQ;
    const size_t bmax = nmax / 2 + 1;
    win.assign (nmax, 0.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        inRing[ch].assign (nmax, 0.0f);
        outAcc[ch].assign (nmax, 0.0f);
        prevPhase[ch].assign (bmax, 0.0f);
        synPhase[ch].assign (bmax, 0.0f);
        mag[ch].assign (bmax, 0.0f);
        phs[ch].assign (bmax, 0.0f);
        frq[ch].assign (bmax, 0.0f);
        outMag[ch].assign (bmax, 0.0f);
        outFrq[ch].assign (bmax, 0.0f);
        srcBin[ch].assign (bmax, 0);
    }
    frame.assign (nmax, 0.0f);
    re.assign (bmax, 0.0f);
    im.assign (bmax, 0.0f);
    avgMag.assign (bmax, 0.0f);
    cep.assign (nmax, 0.0f);
    envA.assign (bmax, 0.0f);
    lift.assign (nmax, 0.0f);
    const float chunkRate = (float) sr / (float) kChunk;
    pitchSm.setup (chunkRate, 0.03f, 0.0f);
    formSm.setup (chunkRate, 0.03f, 0.0f);
    pvStep = 1.0f / (0.02f * (float) sr);
    agcA = tauCoef (0.15f, (float) sr);
    agcGA = tauCoef (0.05f, (float) sr);
    initialised = false;
    configure (1, kWarpHQ);
}

void WarpModule::configure (int, int warpMode)
{
    const int newMode = warpMode == kWarpLoFi ? kWarpLoFi : kWarpHQ;
    if (initialised && newMode == mode)
        return; // oversampling changes do not affect WARP
    initialised = true;
    mode = newMode;
    N = frameSize (sr, mode);
    H = N / 4;
    nb = N / 2 + 1;
    mask = N - 1;
    fftIdx = mode == kWarpLoFi ? 1 : 0;
    for (int j = 0; j < N; ++j)
        win[(size_t) j] = 0.5f - 0.5f * std::cos (kTwoPi * (float) j / (float) N);
    // cepstral lifter: keep quefrencies below nc with a raised-cosine taper
    const int nc = std::max (8, N / 8);
    const int taper = std::max (2, nc / 4);
    for (int q = 0; q < N; ++q)
    {
        const int qq = std::min (q, N - q);
        float l = 0.0f;
        if (qq < nc - taper)
            l = 1.0f;
        else if (qq < nc)
            l = 0.5f + 0.5f * std::cos (kPi * (float) (qq - (nc - taper)) / (float) taper);
        lift[(size_t) q] = l;
    }
    reset();
}

void WarpModule::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (inRing[ch].begin(), inRing[ch].end(), 0.0f);
        std::fill (outAcc[ch].begin(), outAcc[ch].end(), 0.0f);
        std::fill (prevPhase[ch].begin(), prevPhase[ch].end(), 0.0f);
        std::fill (synPhase[ch].begin(), synPhase[ch].end(), 0.0f);
    }
    pos = 0;
    hopCount = 0;
    prevEnergy = 0.0f;
    energyGain = 1.0f;
    agcIn = agcOut = 0.0f;
    agcGain = 1.0f;
    framesSinceTransient = 100;
    pvRunning = false;
    pvGain = 0.0f;
    warm = 0;
    resetNext = true;
    first = true;
}

void WarpModule::control (const float* p, const ModCtx&) noexcept
{
    const float pitch = p[kWarpPitch];
    const float form = p[kWarpFormant] + (p[kWarpLink] > 0.5f ? pitch : 0.0f);
    pitchSm.target = pitch;
    formSm.target = form;
    if (first)
    {
        pitchSm.snap (pitch);
        formSm.snap (form);
    }
    const float ps = pitchSm.step();
    const float fs = formSm.step();
    P = std::pow (2.0f, ps / 12.0f);
    F = std::pow (2.0f, fs / 12.0f);
    wantPV = std::fabs (pitch) > 0.004f || std::fabs (form) > 0.004f || std::fabs (ps) > 0.004f
             || std::fabs (fs) > 0.004f;
    first = false;
}

void WarpModule::computeEnvelope (const float* m, float* env) noexcept
{
    // log magnitude -> real cepstrum -> lifter -> smoothed log envelope
    for (int k = 0; k < nb; ++k)
    {
        re[(size_t) k] = fastLog (m[k] + 1.0e-7f);
        im[(size_t) k] = 0.0f;
    }
    auto& f = fft[fftIdx];
    f.inverse (re.data(), im.data(), cep.data());
    for (int q = 0; q < N; ++q)
        cep[(size_t) q] *= lift[(size_t) q];
    f.forward (cep.data(), re.data(), im.data());
    for (int k = 0; k < nb; ++k)
        env[k] = re[(size_t) k];
}

void WarpModule::analyse (int ch) noexcept
{
    // windowed frame (oldest sample first), zero-phase rotation by N/2
    const float* ring = inRing[ch].data();
    const int half = N / 2;
    for (int j = 0; j < N; ++j)
    {
        const float x = ring[(pos + 1 + j) & mask] * win[(size_t) j]; // pos = index of newest sample
        frame[(size_t) ((j + half) & mask)] = x;
    }
    auto& f = fft[fftIdx];
    f.forward (frame.data(), re.data(), im.data());
    float* mg = mag[ch].data();
    float* ph = phs[ch].data();
    float* fr = frq[ch].data();
    float* pp = prevPhase[ch].data();
    const float binW = kTwoPi / (float) N;
    for (int k = 0; k < nb; ++k)
    {
        const float r = re[(size_t) k], i = im[(size_t) k];
        mg[k] = std::sqrt (r * r + i * i);
        const float p = fastAtan2 (i, r);
        ph[k] = p;
        const float expected = binW * (float) k * (float) H;
        const float dev = princarg (p - pp[k] - expected);
        fr[k] = binW * (float) k + dev / (float) H;
        pp[k] = p;
    }
}

void WarpModule::remap (int ch) noexcept
{
    const float* mg = mag[ch].data();
    const float* fr = frq[ch].data();
    float* om = outMag[ch].data();
    float* of = outFrq[ch].data();
    int* sb = srcBin[ch].data();
    for (int k = 0; k < nb; ++k)
    {
        om[k] = 0.0f;
        of[k] = 0.0f;
        sb[k] = -1;
    }
    // bin remapping (pitch)
    for (int k = 0; k < nb; ++k)
    {
        const int j = (int) ((float) k * P + 0.5f);
        if (j >= nb)
            continue;
        om[j] += mg[k];
        if (sb[j] < 0 || mg[k] > mg[sb[j]])
        {
            sb[j] = k;
            of[j] = fr[k] * P;
        }
    }
    // formant correction env(j/F) / env(j/P) (envelope shared by both channels)
    if (envValid)
    {
        const float* env = envA.data();
        auto envAt = [env, this] (float b) {
            if (b <= 0.0f)
                return env[0];
            const int i0 = (int) b;
            if (i0 >= nb - 1)
                return env[nb - 1];
            const float t = b - (float) i0;
            return env[i0] + (env[i0 + 1] - env[i0]) * t;
        };
        const float invF = 1.0f / F, invP = 1.0f / P;
        for (int j = 0; j < nb; ++j)
        {
            if (om[j] == 0.0f)
                continue;
            const float d = clampf (envAt ((float) j * invF) - envAt ((float) j * invP), -4.6f, 4.6f);
            om[j] *= fastExp (d);
        }
    }
}

void WarpModule::synthesise (int ch, bool resetPhase, float gain) noexcept
{
    const float* ph = phs[ch].data();
    const float* om = outMag[ch].data();
    const float* of = outFrq[ch].data();
    const int* sb = srcBin[ch].data();
    float* sp = synPhase[ch].data();
    const float h = (float) H;
    for (int j = 0; j < nb; ++j)
    {
        if (resetPhase)
            sp[j] = sb[j] >= 0 ? ph[sb[j]] : 0.0f;
        else
            sp[j] = princarg (sp[j] + h * of[j]);
        float sn, cs;
        fastSinCos (sp[j], sn, cs);
        const float m = om[j] * gain;
        re[(size_t) j] = m * cs;
        im[(size_t) j] = m * sn;
    }
    auto& f = fft[fftIdx];
    f.inverse (re.data(), im.data(), frame.data());
    // undo rotation, window, overlap-add starting at the current output position
    const int half = N / 2;
    const float norm = 1.0f / 1.5f;
    float* acc = outAcc[ch].data();
    for (int j = 0; j < N; ++j)
    {
        const float y = frame[(size_t) ((j + half) & mask)] * win[(size_t) j] * norm;
        acc[(pos + j) & mask] += y;
    }
}

void WarpModule::processFrame() noexcept
{
    analyse (0);
    analyse (1);
    // shared transient decision: HF-weighted energy rise
    float e = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* mg = mag[ch].data();
        for (int k = 1; k < nb; ++k)
            e += (float) k * mg[k] * mg[k];
    }
    e /= (float) nb;
    bool transient = false;
    if (framesSinceTransient >= 2 && e > 4.0f * prevEnergy && e > 1.0e-6f * (float) N)
        transient = true;
    prevEnergy = e;
    ++framesSinceTransient;
    if (transient)
    {
        framesSinceTransient = 0;
        ++transientCount;
    }
    const bool resetPhase = transient || resetNext;
    resetNext = false;
    envValid = std::fabs (F - P) > 1.0e-4f;
    if (envValid)
    {
        float* avg = avgMag.data();
        for (int k = 0; k < nb; ++k)
            avg[k] = 0.5f * (mag[0][(size_t) k] + mag[1][(size_t) k]);
        computeEnvelope (avg, envA.data());
    }
    remap (0);
    remap (1);
    // loudness matching: scale the shifted spectrum to the input frame energy (shared
    // by both channels, clamped to +-12 dB and smoothed over ~3 frames)
    double ein = 0.0, eout = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int k = 0; k < nb; ++k)
        {
            ein += (double) mag[ch][(size_t) k] * mag[ch][(size_t) k];
            eout += (double) outMag[ch][(size_t) k] * outMag[ch][(size_t) k];
        }
    const float gTarget = eout > 1.0e-20 ? clampf ((float) std::sqrt (ein / eout), 0.25f, 4.0f) : 1.0f;
    energyGain = resetPhase && framesSinceTransient > 50 ? gTarget : energyGain + 0.35f * (gTarget - energyGain);
    synthesise (0, resetPhase, energyGain);
    synthesise (1, resetPhase, energyGain);
}

void WarpModule::process (const float* inL, const float* inR, float* outL, float* outR, const float*, int n) noexcept
{
    const float* in[2] = { inL, inR };
    float* out[2] = { outL, outR };
    for (int i = 0; i < n; ++i)
    {
        pos = (pos + 1) & mask;
        inRing[0][(size_t) pos] = in[0][i];
        inRing[1][(size_t) pos] = in[1][i];

        // neutral / PV state machine
        if (wantPV)
        {
            if (! pvRunning)
            {
                pvRunning = true;
                warm = N;
                resetNext = true;
                std::fill (outAcc[0].begin(), outAcc[0].end(), 0.0f);
                std::fill (outAcc[1].begin(), outAcc[1].end(), 0.0f);
                hopCount = 0;
            }
            if (warm > 0)
                --warm;
            else
                pvGain = std::min (1.0f, pvGain + pvStep);
        }
        else if (pvRunning)
        {
            pvGain = std::max (0.0f, pvGain - pvStep);
            if (pvGain <= 0.0f)
                pvRunning = false;
        }

        if (pvRunning && ++hopCount >= H)
        {
            hopCount = 0;
            processFrame();
        }

        // neutral path: the oldest sample in the ring = input delayed by N-1
        const int oldest = (pos + 1) & mask;
        if (pvRunning)
        {
            const float d0 = inRing[0][(size_t) oldest], d1 = inRing[1][(size_t) oldest];
            const float p0 = outAcc[0][(size_t) pos], p1 = outAcc[1][(size_t) pos];
            outAcc[0][(size_t) pos] = 0.0f;
            outAcc[1][(size_t) pos] = 0.0f;
            const float pin = d0 * d0 + d1 * d1, pout = p0 * p0 + p1 * p1;
            agcIn = flushTiny (pin + agcA * (agcIn - pin));
            agcOut = flushTiny (pout + agcA * (agcOut - pout));
            if (agcIn > 1.0e-9f && agcOut > 1.0e-9f) // hold the gain through silence
            {
                const float gT = clampf (std::sqrt (agcIn / agcOut), 0.5f, 2.0f);
                agcGain = gT + agcGA * (agcGain - gT);
            }
            out[0][i] = d0 + (p0 * agcGain - d0) * pvGain;
            out[1][i] = d1 + (p1 * agcGain - d1) * pvGain;
        }
        else
        {
            out[0][i] = inRing[0][(size_t) oldest];
            out[1][i] = inRing[1][(size_t) oldest];
        }
    }
}

} // namespace ds
