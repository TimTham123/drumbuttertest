// DrumSpread DSP core - common utilities (no JUCE dependency).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__) || defined(__SSE__)
 #include <immintrin.h>
 #define DS_HAS_SSE 1
#else
 #define DS_HAS_SSE 0
#endif

namespace ds
{
constexpr double kPiD = 3.14159265358979323846;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

/** Control-rate chunk length in samples. All control updates happen at absolute
    multiples of this value so that rendering is block-size independent. */
constexpr int kChunk = 16;

/** Maximum supported oversampling factor. */
constexpr int kMaxOS = 4;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g, float floorDb = -200.0f) noexcept
{
    return g > 1.0e-10f ? std::max (floorDb, 20.0f * std::log10 (g)) : floorDb;
}
inline float clampf (float x, float lo, float hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf (float a, float b, float t) noexcept { return a + (b - a) * t; }

/** One-pole smoothing coefficient a for y += (1-a)(x-y), from a time constant. */
inline float tauCoef (float tauSeconds, float rate) noexcept
{
    if (tauSeconds <= 0.0f || rate <= 0.0f)
        return 0.0f;
    return std::exp (-1.0f / (tauSeconds * rate));
}

/** Flushes values that would become denormal in feedback paths. */
inline float flushTiny (float x) noexcept { return (std::fabs (x) < 1.0e-24f) ? 0.0f : x; }

inline bool isFiniteF (float x) noexcept { return std::isfinite (x); }

/** RAII guard enabling flush-to-zero / denormals-are-zero on x86. */
class DenormalGuard
{
public:
    DenormalGuard() noexcept
    {
#if DS_HAS_SSE
        saved = _mm_getcsr();
        _mm_setcsr (saved | 0x8040u);
#endif
    }
    ~DenormalGuard() noexcept
    {
#if DS_HAS_SSE
        _mm_setcsr (saved);
#endif
    }
    DenormalGuard (const DenormalGuard&) = delete;
    DenormalGuard& operator= (const DenormalGuard&) = delete;

private:
#if DS_HAS_SSE
    unsigned int saved = 0;
#endif
};

/** Small, fast, deterministic PRNG (xorshift32). */
struct Rng
{
    uint32_t s = 0x12345678u;
    explicit Rng (uint32_t seed = 0x12345678u) noexcept : s (seed ? seed : 0x9e3779b9u) {}
    inline uint32_t nextU() noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    /** Uniform in [-1, 1). */
    inline float bipolar() noexcept { return (float) (int32_t) nextU() * (1.0f / 2147483648.0f); }
    /** Uniform in [0, 1). */
    inline float unipolar() noexcept { return (float) (nextU() >> 8) * (1.0f / 16777216.0f); }
};

/** A parameter smoother stepped once per control chunk. Provides a per-sample
    linear ramp between consecutive chunk values via an increment. */
struct ChunkSmoother
{
    float target = 0.0f, current = 0.0f, a = 0.0f;

    void setup (float chunkRate, float tauSeconds, float initial) noexcept
    {
        a = tauCoef (tauSeconds, chunkRate);
        target = current = initial;
    }
    void snap (float v) noexcept { target = current = v; }
    /** Advance by one chunk; returns the new value. */
    float step() noexcept
    {
        current = target + (current - target) * a;
        if (std::fabs (current - target) < 1.0e-7f * (1.0f + std::fabs (target)))
            current = target;
        return current;
    }
};

/** Linear per-sample ramp across one chunk: set at chunk start, advanced per sample. */
struct Ramp
{
    float value = 0.0f, inc = 0.0f, end = 0.0f;
    void snap (float v) noexcept { value = end = v; inc = 0.0f; }
    /** Called at a chunk start: snaps to the previous chunk's end value (no drift)
        and ramps to 'to' over kChunk samples. */
    void rampTo (float to) noexcept
    {
        value = end;
        inc = (to - end) * (1.0f / (float) kChunk);
        end = to;
    }
    float next() noexcept { value += inc; return value; }
    bool isConstantZero() const noexcept { return value == 0.0f && end == 0.0f; }
};

/** Raised-cosine (equal-gain) crossfade curve, t in [0,1]. */
inline float raisedCos (float t) noexcept
{
    t = clampf (t, 0.0f, 1.0f);
    return 0.5f - 0.5f * std::cos (kPi * t);
}

/** tanh via a [7/6] Pade approximant (|error| < 2e-6 for |x| < 4.97, clamped beyond). */
inline float fastTanh (float x) noexcept
{
    if (x > 4.97f)
        return 1.0f;
    if (x < -4.97f)
        return -1.0f;
    const float x2 = x * x;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return num / den;
}

/** atan2 with a minimax polynomial (|error| < 2e-6 rad). */
inline float fastAtan2 (float y, float x) noexcept
{
    const float ax = std::fabs (x), ay = std::fabs (y);
    const float mx = std::max (ax, ay), mn = std::min (ax, ay);
    if (mx == 0.0f)
        return 0.0f;
    const float a = mn / mx;
    const float s = a * a;
    float r = a * (0.99997726f + s * (-0.33262347f + s * (0.19354346f + s * (-0.11643287f + s * (0.05265332f + s * -0.01172120f)))));
    if (ay > ax)
        r = 1.57079632679f - r;
    if (x < 0.0f)
        r = kPi - r;
    return y < 0.0f ? -r : r;
}

/** sin and cos for x in [-pi, pi] (|error| < 4e-6). */
inline void fastSinCos (float x, float& s, float& c) noexcept
{
    float sign = 1.0f;
    if (x > 1.57079632679f)
    {
        x = kPi - x;
        sign = -1.0f;
    }
    else if (x < -1.57079632679f)
    {
        x = -kPi - x;
        sign = -1.0f;
    }
    const float x2 = x * x;
    s = x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f + x2 * (1.0f / 362880.0f)))));
    c = sign * (1.0f + x2 * (-0.5f + x2 * (1.0f / 24.0f + x2 * (-1.0f / 720.0f + x2 * (1.0f / 40320.0f + x2 * (-1.0f / 3628800.0f))))));
}

/** 2^x for x in [-126, 126] (|relative error| < 3e-6): integer part via exponent bits,
    fraction via a 6th-order polynomial. */
inline float fastExp2 (float x) noexcept
{
    x = clampf (x, -126.0f, 126.0f);
    const float fl = std::floor (x);
    const float f = x - fl;
    const float p = 1.0f + f * (0.69314718f + f * (0.24022650f + f * (0.05550411f + f * (0.00961813f + f * (0.00133336f + f * 0.00015404f)))));
    int32_t bits = (int32_t) ((int) fl + 127) << 23;
    float scale;
    std::memcpy (&scale, &bits, sizeof (float));
    return p * scale;
}
inline float fastExp (float x) noexcept { return fastExp2 (x * 1.44269504f); }

/** log2(x) for x > 0 (|error| < 1e-5): exponent bits + polynomial on the mantissa. */
inline float fastLog2 (float x) noexcept
{
    int32_t bits;
    std::memcpy (&bits, &x, sizeof (float));
    const float e = (float) (((bits >> 23) & 0xff) - 127);
    bits = (bits & 0x007fffff) | 0x3f800000;
    float m;
    std::memcpy (&m, &bits, sizeof (float)); // m in [1, 2)
    // log2(m) via atanh series in t = (m-1)/(m+1)
    const float t = (m - 1.0f) / (m + 1.0f);
    const float t2 = t * t;
    const float l = t * (2.8853900f + t2 * (0.9617967f + t2 * (0.5770780f + t2 * (0.4121986f + t2 * 0.3205986f))));
    return e + l;
}
inline float fastLog (float x) noexcept { return fastLog2 (x) * 0.69314718f; }

/** Soft clipper: identity below kneeStart, then a C1-continuous tanh knee that
    approaches (and never exceeds) ceiling. */
inline float softClipKnee (float x, float ceiling, float kneeStart) noexcept
{
    const float a = std::fabs (x);
    if (a <= kneeStart)
        return x;
    const float span = ceiling - kneeStart;
    const float y = kneeStart + span * fastTanh ((a - kneeStart) / span);
    return x < 0.0f ? -y : y;
}

inline float hardClip (float x, float ceiling) noexcept { return clampf (x, -ceiling, ceiling); }

inline int nextPow2 (int v) noexcept
{
    int p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

/** Ratio used to scale STFT sizes etc. with sample rate: 1 (<=50k), 2 (<=100k), 4 above. */
inline int srScale (double sr) noexcept { return sr <= 50000.0 ? 1 : (sr <= 100000.0 ? 2 : 4); }

} // namespace ds
