// DrumSpread DSP core - filters (TPT state-variable filter after A. Simper / V. Zavalishin).
#pragma once

#include "DspCommon.h"

namespace ds
{
/** Coefficients for a TPT SVF with mixing gains (m0*in + m1*band + m2*low). */
struct SvfCoeffs
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.41421356f;
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;

    static float prewarp (float freq, float sr) noexcept
    {
        const float f = clampf (freq, 1.0f, 0.49f * sr);
        return std::tan (kPi * f / sr);
    }
    void setG (float g, float q) noexcept
    {
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void lowpass (float f, float q, float sr) noexcept { setG (prewarp (f, sr), q); m0 = 0; m1 = 0; m2 = 1; }
    void highpass (float f, float q, float sr) noexcept { setG (prewarp (f, sr), q); m0 = 1; m1 = -k; m2 = -1; }
    void bandpass (float f, float q, float sr) noexcept { setG (prewarp (f, sr), q); m0 = 0; m1 = 1; m2 = 0; }
    /** Constant-peak-gain bandpass (peak = 1). */
    void bandpassNorm (float f, float q, float sr) noexcept { setG (prewarp (f, sr), q); m0 = 0; m1 = k; m2 = 0; }
    void bell (float f, float q, float gainDb, float sr) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        setG (prewarp (f, sr), q * A);
        m0 = 1; m1 = k * (A * A - 1.0f); m2 = 0;
    }
    void lowShelf (float f, float q, float gainDb, float sr) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        setG (prewarp (f, sr) / std::sqrt (A), q);
        m0 = 1; m1 = k * (A - 1.0f); m2 = A * A - 1.0f;
    }
    void highShelf (float f, float q, float gainDb, float sr) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        setG (prewarp (f, sr) * std::sqrt (A), q);
        m0 = A * A; m1 = k * (1.0f - A) * A; m2 = 1.0f - A * A;
    }
    void passthrough() noexcept { setG (0.1f, 0.7071f); m0 = 1; m1 = 0; m2 = 0; }
};

/** SVF state for one channel. */
struct SvfState
{
    float ic1 = 0.0f, ic2 = 0.0f;
    void reset() noexcept { ic1 = ic2 = 0.0f; }
    inline float process (const SvfCoeffs& c, float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }
    /** Returns low, band, high simultaneously (ignores m gains). */
    inline void processLBH (const SvfCoeffs& c, float v0, float& low, float& band, float& high) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
        high = v0 - c.k * v1 - v2;
    }
};

/** Stereo SVF convenience wrapper. */
struct StereoSvf
{
    SvfCoeffs c;
    SvfState s[2];
    void reset() noexcept { s[0].reset(); s[1].reset(); }
    inline float process (int ch, float x) noexcept { return s[ch].process (c, x); }
};

/** One-pole lowpass (TPT form, exact cutoff via prewarp). */
struct OnePoleLP
{
    float g = 0.5f, z = 0.0f;
    void setCutoff (float f, float sr) noexcept
    {
        const float w = std::tan (kPi * clampf (f, 1.0f, 0.49f * sr) / sr);
        g = w / (1.0f + w);
    }
    void reset() noexcept { z = 0.0f; }
    inline float process (float x) noexcept
    {
        const float v = (x - z) * g;
        const float y = v + z;
        z = y + v;
        return y;
    }
};

/** DC blocker: y = x - x1 + R*y1. */
struct DcBlocker
{
    float R = 0.9995f, x1 = 0.0f, y1 = 0.0f;
    void setup (float cutoffHz, float sr) noexcept { R = std::exp (-kTwoPi * cutoffHz / sr); }
    void reset() noexcept { x1 = y1 = 0.0f; }
    inline float process (float x) noexcept
    {
        const float y = x - x1 + R * y1;
        x1 = x;
        y1 = flushTiny (y);
        return y1;
    }
};

/** Tilt EQ: one-pole crossover around a pivot, low and high gains. */
struct TiltEq
{
    OnePoleLP lp;
    float gLow = 1.0f, gHigh = 1.0f;
    void setup (float pivotHz, float sr) noexcept { lp.setCutoff (pivotHz, sr); }
    /** tilt in [-1,1]; +1 => +maxDb highs / -maxDb lows. */
    void setTilt (float tilt, float maxDb) noexcept
    {
        gHigh = dbToGain (tilt * maxDb);
        gLow = dbToGain (-tilt * maxDb);
    }
    void reset() noexcept { lp.reset(); }
    inline float process (float x) noexcept
    {
        const float lo = lp.process (x);
        return lo * gLow + (x - lo) * gHigh;
    }
};

} // namespace ds
