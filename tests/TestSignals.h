// DrumSpread offline verification harness - synthesized test signals.
#pragma once

#include "TestUtil.h"

namespace tu
{
struct DrumLoop
{
    Stereo audio;
    std::vector<int64_t> kicks, snares, hats; // onset sample indices (ground truth)
};

/** Synthesized kick/snare/hat loop (16th grid with velocity variation and light
    humanisation). bassLine adds a sustained synth bass as a detector confounder. */
DrumLoop makeDrumLoop (double sr, double bpm, int bars, uint32_t seed, bool bassLine = false, float level = 1.0f);

Stereo sine (double sr, double freq, double amp, size_t n);
Stereo multiSine (double sr, const std::vector<double>& freqs, double amp, size_t n);
Stereo noise (double sr, double amp, size_t n, uint32_t seed);
/** Harmonic pulse train (fundamental f0) through a resonant formant filter at fFormant. */
Stereo formantBuzz (double sr, double f0, double fFormant, size_t n);

} // namespace tu
