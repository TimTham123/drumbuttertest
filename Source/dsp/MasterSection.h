// DrumSpread DSP core - MASTER: transient shaper, 3-band EQ, HPF/LPF, Air.
#pragma once

#include "Detectors.h"
#include "Filters.h"
#include "Params.h"

namespace ds
{
class MasterSection
{
public:
    void prepare (double sampleRate);
    void reset();
    /** k = intensity / 100 in [0, 2]. */
    void control (const float* p, float k) noexcept;
    void process (float* L, float* R, int n) noexcept;

private:
    double sr = 48000.0;
    EnvFollower aFast, aSlow, sFast, sSlow;
    Ramp tsGain;
    StereoSvf low, mid, high, hpf, lpf, airShelf, exHp, exHp2;
    bool hpfOn = false, lpfOn = false;
    // stages that are exactly neutral are skipped (state reset when they re-engage)
    bool lowOn = false, midOn = false, highOn = false, airOn = false, tsOn = false;
    ChunkSmoother lowG, midG, highG, lowF, midF, highF, hpfF, lpfF, air, atk, sus;
    Ramp exMix;
    bool first = true;
};

} // namespace ds
