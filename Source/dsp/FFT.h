// DrumSpread DSP core - real FFT (radix-2 complex FFT of size N/2 + real post-processing).
#pragma once

#include "DspCommon.h"
#include <vector>

namespace ds
{
class RealFFT
{
public:
    /** n must be a power of two >= 8. Allocates. */
    void prepare (int n);
    int size() const noexcept { return N; }

    /** Forward transform of N real samples into N/2+1 complex bins (unscaled). */
    void forward (const float* in, float* re, float* im) noexcept;
    /** Inverse transform of N/2+1 bins into N real samples (scaled by 1/N, so
        inverse(forward(x)) == x). */
    void inverse (const float* re, const float* im, float* out) noexcept;

private:
    void complexFFT (float* re, float* im, bool inverseDir) noexcept;

    int N = 0, M = 0, logM = 0;
    std::vector<float> twr, twi;         // per-stage twiddles (stage len: offset len/2-1 .. len-2)
    std::vector<float> wr, wi;           // e^{-j 2 pi k / N}, k = 0..M
    std::vector<int> bitrev;
    std::vector<float> zr, zi;           // work buffers
};

} // namespace ds
