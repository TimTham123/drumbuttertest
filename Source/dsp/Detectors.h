// DrumSpread DSP core - envelope followers, transient (target) detector, onset trigger.
#pragma once

#include "DspCommon.h"
#include "DelayLine.h"
#include "Filters.h"

namespace ds
{
/** Attack/release one-pole peak follower (time constants in seconds). */
struct EnvFollower
{
    float aAtt = 0.0f, aRel = 0.0f, y = 0.0f;
    void setTimes (float attackSec, float releaseSec, float sr) noexcept
    {
        aAtt = tauCoef (attackSec, sr);
        aRel = tauCoef (releaseSec, sr);
    }
    void reset() noexcept { y = 0.0f; }
    inline float process (float x) noexcept
    {
        const float a = x > y ? aAtt : aRel;
        y = x + a * (y - x);
        y = flushTiny (y);
        return y;
    }
};

/** Dual-envelope transient detector (fast vs. slow attack, shared release).
    Envelopes run per sample; the transientness t in [0,1] is evaluated per
    control chunk (it involves a log), and ramped per sample by the caller. */
class TransientDetector
{
public:
    void prepare (float sr) noexcept
    {
        fast.setTimes (0.0005f, 0.040f, sr);
        slow.setTimes (0.025f, 0.040f, sr);
        smoothA = tauCoef (0.001f, sr / (float) kChunk);
        reset();
    }
    void reset() noexcept
    {
        fast.reset();
        slow.reset();
        t = 0.0f;
    }
    inline void push (float absx) noexcept
    {
        fast.process (absx);
        slow.process (absx);
    }
    /** Evaluate at a chunk boundary. */
    float evaluate() noexcept
    {
        float raw = 0.0f;
        if (fast.y > 1.0e-6f)
        {
            const float ratio = fast.y / std::max (slow.y, 1.0e-9f);
            raw = clampf (20.0f * std::log10 (std::max (ratio, 1.0f)) / 9.0f, 0.0f, 1.0f);
        }
        t = raw + smoothA * (t - raw);
        return t;
    }
    float value() const noexcept { return t; }

    /** Target weighting: bias in [-1 (transient), +1 (sustain)]. */
    static float weight (float tr, float bias) noexcept
    {
        if (bias < 0.0f)
            return 1.0f + bias * (1.0f - tr); // bias=-1 -> tr
        return 1.0f - bias * tr;              // bias=+1 -> 1-tr
    }

private:
    EnvFollower fast, slow;
    float smoothA = 0.0f, t = 0.0f;
};

/** Onset trigger for the LAYER module. Band filter (4th-order Linkwitz-Riley) ->
    power -> fast power envelope. Fires when the envelope (a) exceeds an absolute
    threshold, (b) has risen by riseDb within the last 10 ms (onset sharpness: slow
    beating between overlapping low notes does not qualify), and for the low band
    (c) dominates the full-band power and reaches a level relative to a decaying
    memory of recent kick peaks (rejects snare bodies). It re-arms only after the
    envelope has fallen 3 dB below the triggering peak and a hold-off has elapsed,
    which rejects re-triggers inside noisy decays. Velocity is captured over 6 ms. */
class OnsetTrigger
{
public:
    enum class Band
    {
        Low,
        High
    };

    void prepare (float sampleRate, Band b) noexcept
    {
        sr = sampleRate;
        band = b;
        if (band == Band::Low)
        {
            fc = 150.0f;
            filt.lowpass (fc, 0.70710678f, sr);
            useHp = true; // 60 Hz HP: attenuates the previous kick's tail / sustained bass
            hpC.highpass (60.0f, 0.70710678f, sr);
            fast.setTimes (0.0015f, 0.020f, sr);
            slow.setTimes (0.020f, 0.060f, sr); // longer release: the fast/slow ratio collapses in decays
            full.setTimes (0.0015f, 0.020f, sr);
            bgCoef = std::exp (-1.0f / (0.3f * sr)); // 300 ms background average
            peakDecay = std::exp (-1.0f / (1.5f * sr)); // ~1.5 s memory
            holdoffSamples = (int) (0.070f * sr);
        }
        else
        {
            fc = 2000.0f;
            filt.highpass (fc, 0.70710678f, sr);
            fast.setTimes (0.001f, 0.015f, sr);
            slow.setTimes (0.012f, 0.030f, sr);
            full.setTimes (0.001f, 0.015f, sr);
            holdoffSamples = (int) (0.040f * sr);
        }
        captureSamples = (int) (0.006f * sr);
        riseDelay = std::max (1, (int) (0.010f * sr));
        hist.prepare (riseDelay + 4);
        setSensitivity (0.5f);
        reset();
    }
    void reset() noexcept
    {
        st1.reset();
        st2.reset();
        st3.reset();
        fast.reset();
        slow.reset();
        full.reset();
        hist.reset();
        peakMem = 0.0f;
        bg = 0.0f;
        holdCounter = 0;
        captureCounter = 0;
        peak = 0.0f;
        armed = true;
        lastFiltered = 0.0f;
    }
    /** sens in [0,1]. */
    void setSensitivity (float sens) noexcept
    {
        sens = clampf (sens, 0.0f, 1.0f);
        absThreshDb = -12.0f - 42.0f * sens;
        absThreshPow = dbToGain (2.0f * absThreshDb);
        // power rise vs. the slow envelope: low band 6..0 dB (3 dB at 50%), high band 7.5..1.5 dB (4.5 dB at 50%)
        const float riseDb = band == Band::Low ? 6.0f - 6.0f * sens : 7.5f - 6.0f * sens;
        riseRatio = std::pow (10.0f, riseDb / 10.0f);
        // low band: level relative to a slowly decaying memory of recent kick peaks
        relThresh = std::pow (10.0f, -(4.0f + 8.0f * sens) / 10.0f); // -4 .. -12 dB (-8 dB at 50%)
    }

    /** Process one (mono) sample; returns true on a trigger. */
    inline bool process (float x) noexcept
    {
        float f = st2.process (filt, st1.process (filt, x));
        if (useHp)
            f = st3.process (hpC, f);
        lastFiltered = f;
        const float pw = f * f;
        fast.process (pw);
        slow.process (pw);
        full.process (x * x);
        const float past = hist.read (riseDelay - 1);
        hist.write (fast.y);
        peakMem = std::max (fast.y, peakMem * peakDecay);
        bg = pw + bgCoef * (bg - pw);
        bool fired = false;
        if (holdCounter > 0)
            --holdCounter;
        if (! armed && fast.y < 0.5f * peak)
            armed = true;
        // kick-like events must dominate the spectrum in the low band (rejects snare bodies)
        // (and must stand 3 dB above the 300 ms background, which rejects sustained bass notes)
        const bool dominant = band == Band::High
                              || (fast.y > 0.1f * full.y && fast.y > relThresh * peakMem && fast.y > 2.0f * bg);
        const float floorPow = 1.0e-3f * absThreshPow;
        (void) past;
        (void) floorPow;
        if (armed && holdCounter == 0 && dominant && fast.y > absThreshPow && fast.y > riseRatio * slow.y)
        {
            fired = true;
            armed = false;
            holdCounter = holdoffSamples;
            captureCounter = captureSamples;
            peak = fast.y;
        }
        if (captureCounter > 0)
            --captureCounter;
        peak = std::max (peak, fast.y);
        return fired;
    }
    bool capturing() const noexcept { return captureCounter > 0; }
    /** Velocity in [0.3, 1] from the captured peak. */
    float velocity() const noexcept
    {
        const float db = 10.0f * std::log10 (std::max (peak, 1.0e-12f)) - absThreshDb;
        return clampf (db / 18.0f, 0.3f, 1.0f);
    }
    float filtered() const noexcept { return lastFiltered; }
    /** Phase lag (radians, positive = lag) of the detection filter at frequency f. */
    float phaseLag (float f) const noexcept
    {
        const float r = f / fc;
        const float one = std::atan2 (1.41421356f * r, 1.0f - r * r); // 2nd-order Butterworth LP lag
        if (band != Band::Low)
            return 2.0f * (one - kPi);
        const float rh = f / 60.0f; // 2nd-order Butterworth HP at 60 Hz leads by (pi - LP lag)
        const float hpLead = kPi - std::atan2 (1.41421356f * rh, 1.0f - rh * rh);
        return 2.0f * one - hpLead;
    }

private:
    float sr = 48000.0f, fc = 120.0f;
    Band band = Band::Low;
    SvfCoeffs filt, hpC;
    SvfState st1, st2, st3;
    bool useHp = false;
    EnvFollower fast, slow, full;
    float absThreshPow = 1.0e-4f, absThreshDb = -40.0f, riseRatio = 4.0f;
    float relThresh = 0.06f, peakMem = 0.0f, peakDecay = 0.9999f, bg = 0.0f, bgCoef = 0.9999f;
    int holdoffSamples = 0, holdCounter = 0, captureSamples = 0, captureCounter = 0;
    float peak = 0.0f, lastFiltered = 0.0f;
    bool armed = true;
    DelayBuffer hist; // fast-envelope history for the 10 ms rise test
    int riseDelay = 480;
};

} // namespace ds
