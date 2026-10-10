// DrumSpread DSP core - delay lines. All memory is allocated in prepare().
#pragma once

#include "DspCommon.h"
#include <vector>

namespace ds
{
/** Circular buffer with power-of-two length. read(d) returns the sample written d
    writes ago (read(0) = most recent). */
class DelayBuffer
{
public:
    void prepare (int maxDelaySamples)
    {
        const int len = nextPow2 (std::max (4, maxDelaySamples + 2));
        buf.assign ((size_t) len, 0.0f);
        mask = len - 1;
        w = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    int capacity() const noexcept { return mask - 1; }

    inline void write (float x) noexcept
    {
        w = (w + 1) & mask;
        buf[(size_t) w] = x;
    }
    inline float read (int d) const noexcept { return buf[(size_t) ((w - d) & mask)]; }
    /** Linear interpolated fractional read, d >= 0. */
    inline float readLinear (float d) const noexcept
    {
        const int di = (int) d;
        const float fr = d - (float) di;
        const float a = buf[(size_t) ((w - di) & mask)];
        const float b = buf[(size_t) ((w - di - 1) & mask)];
        return a + (b - a) * fr;
    }
    /** Cubic (Hermite) interpolated read, d >= 1. */
    inline float readCubic (float d) const noexcept
    {
        const int di = (int) d;
        const float t = d - (float) di;
        const float xm1 = buf[(size_t) ((w - di + 1) & mask)];
        const float x0 = buf[(size_t) ((w - di) & mask)];
        const float x1 = buf[(size_t) ((w - di - 1) & mask)];
        const float x2 = buf[(size_t) ((w - di - 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

/** Integer latency-compensation delay with a configurable length. */
class FixedDelay
{
public:
    void prepare (int maxDelay) { line.prepare (std::max (1, maxDelay)); d = 0; }
    void setDelay (int samples) noexcept { d = std::max (0, std::min (samples, line.capacity())); }
    int delay() const noexcept { return d; }
    void reset() noexcept { line.reset(); }
    inline float process (float x) noexcept
    {
        if (d == 0)
            return x;
        line.write (x);
        return line.read (d);
    }

private:
    DelayBuffer line;
    int d = 0;
};

/** Schroeder allpass with integer delay: y = -g*x + x[n-D] + g*y[n-D]. */
class AllpassDelay
{
public:
    void prepare (int maxDelay) { line.prepare (maxDelay + 2); }
    void reset() noexcept { line.reset(); }
    void setDelay (int dd) noexcept { d = std::max (1, std::min (dd, line.capacity() - 1)); }
    inline float process (float x, float g) noexcept
    {
        const float delayed = line.read (d - 1);
        const float v = x + g * delayed;
        line.write (flushTiny (v));
        return delayed - g * v;
    }
    /** Modulated (fractional) delay version. */
    inline float processFrac (float x, float g, float dFrac) noexcept
    {
        const float delayed = line.readLinear (std::max (0.0f, dFrac - 1.0f));
        const float v = x + g * delayed;
        line.write (flushTiny (v));
        return delayed - g * v;
    }
    /** Read a tap inside the allpass delay memory. */
    inline float tap (int t) const noexcept { return line.read (t); }
    int delay() const noexcept { return d; }

private:
    DelayBuffer line;
    int d = 1;
};

} // namespace ds
