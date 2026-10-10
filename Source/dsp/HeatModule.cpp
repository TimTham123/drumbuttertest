#include "HeatModule.h"

namespace ds
{
namespace
{
    constexpr float kRefLevel = 0.5f; // -6 dBFS reference for level compensation

    inline float sat3 (float v) noexcept { return v / std::cbrt (1.0f + std::fabs (v * v * v)); }
    inline float tubeCurve (float v) noexcept { return v >= 0.0f ? fastTanh (v) : fastTanh (0.6f * v) / 0.6f; }
    inline float fuzzCurve (float v) noexcept { return v >= 0.0f ? 0.6f * fastTanh (v / 0.6f) : fastTanh (v); }
    inline float diodeCurve (float v) noexcept
    {
        return v >= 0.0f ? 0.35f * std::asinh (v / 0.35f) : 0.7f * std::asinh (v / 0.7f);
    }
    inline float tapeCurve (float v) noexcept
    {
        const float t = fastTanh (v);
        return t + 0.04f * t * t;
    }
    constexpr float kTubeBias = 0.15f;
} // namespace

float HeatModule::staticCurve (int mode, float u) noexcept
{
    switch (mode)
    {
        case kHeatTape: return tapeCurve (u);
        case kHeatTube: return tubeCurve (u + kTubeBias) - tubeCurve (kTubeBias);
        case kHeatTransformer: return sat3 (0.7f * (sat3 (1.5f * u) / 1.5f)) / 0.7f;
        case kHeatDiode: return diodeCurve (u);
        case kHeatFuzz: return fuzzCurve (3.0f * u) - fuzzCurve (0.0f);
        case kHeatCrush:
        default: return u;
    }
}

float HeatModule::computeComp (int mode, float g) const noexcept
{
    if (mode == kHeatCrush)
        return 1.0f;
    const float a = std::fabs (staticCurve (mode, g * kRefLevel));
    const float b = std::fabs (staticCurve (mode, -g * kRefLevel));
    const float m = 0.5f * (a + b);
    return m > 1.0e-6f ? kRefLevel / m : 1.0f;
}

void HeatModule::prepare (double sampleRate)
{
    sr = sampleRate;
    os.prepare (kChunk);
    const float chunkRate = (float) sr / (float) kChunk;
    driveSm.setup (chunkRate, 0.02f, 6.0f);
    toneSm.setup (chunkRate, 0.02f, 0.0f);
    trimSm.setup (chunkRate, 0.02f, 0.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        tilt[ch].setup (800.0f, (float) sr);
        dc[ch].setup (5.0f, (float) sr);
    }
    configure (1, 0);
    reset();
}

void HeatModule::configure (int osFactor, int)
{
    os.setFactor (osFactor);
    osr = sr * os.factor();
    const float fs = (float) osr;
    tapePre.highShelf (3000.0f, 0.7071f, 6.0f, fs);
    tapeDe.highShelf (3000.0f, 0.7071f, -6.0f, fs);
    tapeBump.bell (70.0f, 1.0f, 1.5f, fs);
    diodePre.highShelf (1200.0f, 0.7071f, 6.0f, fs);
    diodeDe.highShelf (1200.0f, 0.7071f, -6.0f, fs);
    envAtt = tauCoef (0.005f, fs);
    envRel = tauCoef (0.050f, fs);
    fuzzEnvA = tauCoef (0.030f, fs);
    trA = std::exp (-2.0 * kPiD * 40.0 / osr);
    fadeLen = 0.025f * fs;
    // oversampled-rate filter states are not valid at a new rate
    for (auto& c : st)
        c = ChState {};
}

void HeatModule::reset()
{
    os.reset();
    for (int ch = 0; ch < 2; ++ch)
    {
        st[ch] = ChState {};
        tilt[ch].reset();
        dc[ch].reset();
    }
    first = true;
}

void HeatModule::control (const float* p, const ModCtx& ctx) noexcept
{
    const int mode = (int) p[kHeatMode];
    const float driveDb = p[kHeatDrive] + 6.0f * ctx.push;
    const float tone = p[kHeatTone] * 0.01f;
    const float trimDb = p[kHeatTrim];
    driveSm.target = driveDb;
    toneSm.target = (mode == kHeatCrush) ? 0.0f : tone;
    trimSm.target = trimDb;
    const int len = kChunk * os.factor();

    if (first)
    {
        fader.snap (mode);
        driveSm.snap (driveDb);
        toneSm.snap (toneSm.target);
        trimSm.snap (trimDb);
        const float g = dbToGain (driveDb);
        driveRamp.snap (g);
        compRampCur.snap (computeComp (mode, g));
        compRampPrev.snap (compRampCur.end);
        trimRamp.snap (dbToGain (trimDb));
    }
    else if (fader.request (mode, fadeLen))
    {
        // reset only the incoming mode's state fields
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& s = st[ch];
            switch (mode)
            {
                case kHeatTape: s.tPre.reset(); s.tDe.reset(); s.tBump.reset(); s.tLag = 0.0f; break;
                case kHeatTube: s.env = 0.0f; break;
                case kHeatTransformer: s.phi = 0.0; s.eState = 0.0; s.ePrev = 0.0; break;
                case kHeatDiode: s.dPre.reset(); s.dDe.reset(); break;
                case kHeatFuzz: s.fenv = 0.0f; break;
                case kHeatCrush: s.held = 0.0f; s.phase = 1.0f; break;
                default: break;
            }
        }
        compRampPrev.snap (compRampCur.end);
    }

    const float d = driveSm.step();
    const float g = dbToGain (d);
    driveRamp.rampTo (g, len);
    compRampCur.rampTo (computeComp (fader.current, g), len);
    compRampPrev.rampTo (computeComp (fader.previous, g), len);

    // crush settings (drive -> bit depth, tone -> sample-and-hold factor)
    const float bits = std::max (2.0f, 16.0f - 14.0f * clampf (d / 36.0f, 0.0f, 1.0f));
    bitsLevels = std::pow (2.0f, bits - 1.0f);
    const float tn = clampf ((tone + 1.0f) * 0.5f, 0.0f, 1.0f);
    holdLen = (1.0f + 31.0f * tn * tn) * (float) os.factor();

    const float t = toneSm.step();
    if (t != lastTilt || first)
    {
        for (int ch = 0; ch < 2; ++ch)
            tilt[ch].setTilt (t, 6.0f);
        lastTilt = t;
    }
    trimRamp.rampTo (dbToGain (trimSm.step()));
    first = false;
}

float HeatModule::runMode (int mode, ChState& s, float x, float g, float comp) noexcept
{
    switch (mode)
    {
        case kHeatTape:
        {
            const float pre = s.tPre.process (tapePre, x) * g;
            const float sat = tapeCurve (pre);
            // level-dependent HF loss: blend toward a fixed 8 kHz one-pole as level rises
            const float lagAmt = std::fabs (pre) / (1.0f + std::fabs (pre));
            const float wc = std::min (0.9f, kTwoPi * 8000.0f / (float) osr);
            s.tLag += (wc / (1.0f + wc)) * (sat - s.tLag);
            s.tLag = flushTiny (s.tLag);
            const float y = sat + lagAmt * (s.tLag - sat);
            const float de = s.tDe.process (tapeDe, y);
            return s.tBump.process (tapeBump, de) * comp;
        }
        case kHeatTube:
        {
            const float u = g * x;
            const float a = std::fabs (u) > s.env ? envAtt : envRel;
            s.env = flushTiny (std::fabs (u) + a * (s.env - std::fabs (u)));
            const float bias = kTubeBias - 0.25f * std::min (s.env, 2.0f);
            return (tubeCurve (u + bias) - tubeCurve (bias)) * comp;
        }
        case kHeatTransformer:
        {
            const double u = (double) (g * x);
            s.phi = trA * s.phi + (1.0 - trA) * u;
            const float phiF = (float) s.phi;
            const double e = (double) (sat3 (1.5f * phiF) / 1.5f) - s.phi;
            const double de = (e - trA * s.ePrev) / (1.0 - trA);
            s.ePrev = e;
            const float y = (float) (u + std::max (-4.0, std::min (4.0, de)));
            return sat3 (0.7f * y) / 0.7f * comp;
        }
        case kHeatDiode:
        {
            const float v = s.dPre.process (diodePre, x) * g;
            return s.dDe.process (diodeDe, diodeCurve (v)) * comp;
        }
        case kHeatFuzz:
        {
            const float ax = std::fabs (x * g);
            s.fenv = flushTiny (ax + fuzzEnvA * (s.fenv - ax));
            const float bias = 0.35f * (1.0f - clampf (s.fenv * 4.0f, 0.0f, 1.0f));
            const float y = fuzzCurve (3.0f * g * x + bias) - fuzzCurve (bias);
            const float gate = clampf ((s.fenv - 0.002f) / 0.02f, 0.0f, 1.0f);
            return y * (0.15f + 0.85f * gate) * comp;
        }
        case kHeatCrush:
        default:
        {
            s.phase += 1.0f / holdLen;
            if (s.phase >= 1.0f)
            {
                s.phase -= std::floor (s.phase);
                s.held = x;
            }
            return std::round (s.held * bitsLevels) / bitsLevels;
        }
    }
}

void HeatModule::process (const float* inL, const float* inR, float* outL, float* outR, const float*, int n) noexcept
{
    os.upsample (inL, inR, n);
    const int N = n * os.factor();
    float* b[2] = { os.buffer (0), os.buffer (1) };
    for (int i = 0; i < N; ++i)
    {
        const float g = driveRamp.next();
        const float cc = compRampCur.next();
        const float cp = compRampPrev.next();
        if (fader.active())
        {
            const float t = fader.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = b[ch][i];
                const float yc = runMode (fader.current, st[ch], x, g, cc);
                const float yp = runMode (fader.previous, st[ch], x, g, cp);
                b[ch][i] = yp + (yc - yp) * t;
            }
        }
        else
        {
            for (int ch = 0; ch < 2; ++ch)
                b[ch][i] = runMode (fader.current, st[ch], b[ch][i], g, cc);
        }
    }
    os.downsample (outL, outR, n);
    for (int i = 0; i < n; ++i)
    {
        const float tr = trimRamp.next();
        outL[i] = tilt[0].process (dc[0].process (outL[i])) * tr;
        outR[i] = tilt[1].process (dc[1].process (outR[i])) * tr;
    }
}

} // namespace ds
