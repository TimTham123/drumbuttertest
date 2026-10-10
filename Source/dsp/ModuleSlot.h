// DrumSpread DSP core - ModuleSlot: shared per-module controls around any module.
//
//   out = delay_L(x) + g * w * (proc(band) - delay_L(band))
//
// band = LP2(HP2(x)) (complementary-by-subtraction focus split), g = mix *
// intensity-blend * bypass-ramp, w = Target weighting (or 1 for send-mode modules).
#pragma once

#include "DelayLine.h"
#include "Detectors.h"
#include "Filters.h"
#include "Params.h"

namespace ds
{
/** Context passed to modules at each control tick. */
struct ModCtx
{
    float push = 0.0f;  // intensity "push" b in [0,1] (intensity above 100%)
    int osFactor = 1;
};

/** Variable-length linear ramp (for oversampled-rate ramps). */
struct LinRamp
{
    float value = 0.0f, inc = 0.0f, end = 0.0f;
    void snap (float v) noexcept { value = end = v; inc = 0.0f; }
    void rampTo (float to, int len) noexcept
    {
        value = end;
        inc = (to - end) / (float) std::max (1, len);
        end = to;
    }
    inline float next() noexcept { value += inc; return value; }
};

/** Crossfade state for switching between two discrete modes. */
struct ModeFader
{
    int current = 0, previous = 0;
    float t = 1.0f, inc = 0.0f;
    void snap (int m) noexcept { current = previous = m; t = 1.0f; inc = 0.0f; }
    bool active() const noexcept { return t < 1.0f; }
    /** Returns true if a new crossfade started. */
    bool request (int m, float lengthSamples) noexcept
    {
        if (m == current || active())
            return false;
        previous = current;
        current = m;
        t = 0.0f;
        inc = 1.0f / std::max (1.0f, lengthSamples);
        return true;
    }
    inline float next() noexcept
    {
        t = std::min (1.0f, t + inc);
        return t;
    }
};

template <class Mod>
class ModuleSlot
{
public:
    Mod mod;

    void prepare (double sampleRate, int moduleIndex)
    {
        sr = sampleRate;
        moduleId = moduleIndex;
        mod.prepare (sr);
        const int maxL = Mod::maxLatency (sr);
        for (int ch = 0; ch < 2; ++ch)
        {
            dx[ch].prepare (maxL);
            dband[ch].prepare (maxL);
        }
        dw.prepare (maxL);
        det.prepare ((float) sr);
        const float chunkRate = (float) sr / (float) kChunk;
        mixSm.setup (chunkRate, 0.015f, 0.0f);
        loSm.setup (chunkRate, 0.02f, std::log (20.0f));
        hiSm.setup (chunkRate, 0.02f, std::log (20000.0f));
        bypStep = (float) kChunk / (0.02f * (float) sr);
        configure (1, 0);
        reset();
    }

    /** Applies a latency-affecting configuration (no allocation). Module state is
        kept where it remains valid (the module decides); the compensation delays
        keep their history and simply read at the new length. */
    void configure (int osFactor, int warpMode)
    {
        mod.configure (osFactor, warpMode);
        L = mod.latency();
        for (int ch = 0; ch < 2; ++ch)
        {
            dx[ch].setDelay (L);
            dband[ch].setDelay (L);
        }
        dw.setDelay (L);
    }

    void reset()
    {
        mod.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            dx[ch].reset();
            dband[ch].reset();
        }
        dw.reset();
        hp.reset();
        lp.reset();
        det.reset();
        gRamp.snap (gRamp.end);
        wRamp.snap (1.0f);
        firstControl = true;
    }

    int latency() const noexcept { return L; }
    bool isRunning() const noexcept { return ! skipMod; }

    /** Called at every chunk boundary. blend = intensity blend a in [0,1]. */
    void control (const float* p, float blend, const ModCtx& ctx) noexcept
    {
        const float loHz = p[sharedParam (moduleId, kSfFocusLo)];
        const float hiHz = p[sharedParam (moduleId, kSfFocusHi)];
        hpOn = loHz > 20.5f;
        lpOn = hiHz < 19500.0f;
        loSm.target = std::log (clampf (loHz, 20.0f, 5000.0f));
        hiSm.target = std::log (clampf (hiHz, 200.0f, 20000.0f));
        const float mixT = clampf (p[sharedParam (moduleId, kSfMix)] * 0.01f * blend, 0.0f, 1.0f);
        mixSm.target = mixT;
        const bool bypassReq = p[sharedParam (moduleId, kSfBypass)] > 0.5f;

        if (firstControl)
        {
            loSm.snap (loSm.target);
            hiSm.snap (hiSm.target);
            mixSm.snap (mixT);
            bypGain = bypassReq ? 0.0f : 1.0f;
            state = bypassReq ? Bypassed : Active;
            gRamp.snap (mixT * bypGain);
        }

        const float lo = std::exp (loSm.step());
        const float hi = std::exp (hiSm.step());
        hp.c.highpass (lo, 0.70710678f, (float) sr);
        lp.c.lowpass (hi, 0.70710678f, (float) sr);

        // bypass state machine (see DESIGN.md 3.3)
        if (bypassReq)
        {
            if (state == Warming)
                state = Bypassed;
            bypGain = std::max (0.0f, bypGain - bypStep);
            if (bypGain <= 0.0f)
                state = Bypassed;
        }
        else
        {
            if (state == Bypassed)
            {
                if (! firstControl)
                    mod.reset();
                warmCount = L + kChunk;
                state = Warming;
            }
            if (state == Warming)
            {
                warmCount -= kChunk;
                if (warmCount <= 0)
                    state = Active;
            }
            if (state == Active)
                bypGain = std::min (1.0f, bypGain + bypStep);
        }

        const float gStart = gRamp.end;
        const float gTarget = mixSm.step() * bypGain;
        gRamp.rampTo (gTarget);
        skipMod = (state == Bypassed) && gStart == 0.0f && gTarget == 0.0f;
        gZero = gStart == 0.0f && gTarget == 0.0f;

        const float bias = clampf (p[sharedParam (moduleId, kSfTarget)] * 0.01f, -1.0f, 1.0f);
        const float tr = det.evaluate();
        wRamp.rampTo (TransientDetector::weight (tr, bias));

        if (! skipMod)
            mod.control (p, ctx);
        firstControl = false;
    }

    /** In-place stereo processing of n <= kChunk samples. */
    void process (float* L_, float* R_, int n) noexcept
    {
        float* io[2] = { L_, R_ };
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = io[ch][i];
                xb[ch][i] = x;
                const float h = hp.process (ch, x);
                const float b1 = hpOn ? h : x;
                const float l = lp.process (ch, b1);
                band[ch][i] = lpOn ? l : b1;
            }
            det.push (std::max (std::fabs (band[0][i]), std::fabs (band[1][i])));
            wb[i] = wRamp.next();
        }

        if (! skipMod)
            mod.process (band[0], band[1], proc[0], proc[1], wb, n);

        for (int i = 0; i < n; ++i)
        {
            const float g = gRamp.next();
            const float w = Mod::kSendTarget ? 1.0f : dw.process (wb[i]);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float d = dx[ch].process (xb[ch][i]);
                const float db = dband[ch].process (band[ch][i]);
                io[ch][i] = (skipMod || gZero) ? d : d + g * w * (proc[ch][i] - db);
            }
        }
    }

private:
    enum State
    {
        Active,
        Bypassed,
        Warming
    };

    double sr = 48000.0;
    int moduleId = 0;
    int L = 0;
    StereoSvf hp, lp;
    bool hpOn = false, lpOn = false;
    ChunkSmoother mixSm, loSm, hiSm;
    Ramp gRamp, wRamp;
    float bypGain = 1.0f, bypStep = 0.01f;
    State state = Active;
    int warmCount = 0;
    bool skipMod = false, gZero = false, firstControl = true;
    TransientDetector det;
    FixedDelay dx[2], dband[2], dw;
    float xb[2][kChunk] {}, band[2][kChunk] {}, proc[2][kChunk] {}, wb[kChunk] {};
};

} // namespace ds
