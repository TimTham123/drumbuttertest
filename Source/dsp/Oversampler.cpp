#include "Oversampler.h"

namespace ds
{
static double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    const double q = x * x * 0.25;
    for (int k = 1; k < 64; ++k)
    {
        term *= q / ((double) k * (double) k);
        sum += term;
        if (term < 1.0e-12 * sum)
            break;
    }
    return sum;
}

void HalfbandStage::design (int numTaps, float beta)
{
    // N must be 4K+3 so that the centre tap index c = (N-1)/2 is odd.
    N = numTaps;
    K = (N - 3) / 4;
    M = (N + 1) / 2;
    const int c = (N - 1) / 2;
    h.assign ((size_t) N, 0.0f);
    std::vector<double> hd ((size_t) N, 0.0);
    const double i0b = besselI0 ((double) beta);
    double sumOff = 0.0;
    for (int n = 0; n < N; ++n)
    {
        const int d = n - c;
        if (d == 0)
        {
            hd[(size_t) n] = 0.5;
            continue;
        }
        if ((d & 1) == 0)
            continue; // exact zeros of the halfband
        const double x = 0.5 * (double) d;
        const double sinc = std::sin (kPiD * x) / (kPiD * x);
        const double r = (double) d / (double) c;
        const double w = besselI0 ((double) beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        hd[(size_t) n] = 0.5 * sinc * w;
        sumOff += hd[(size_t) n];
    }
    // normalise so the non-centre taps sum to exactly 0.5 (unity DC gain)
    for (int n = 0; n < N; ++n)
        if (n != c)
            hd[(size_t) n] *= 0.5 / sumOff;
    for (int n = 0; n < N; ++n)
        h[(size_t) n] = (float) hd[(size_t) n];

    tapsUp.assign ((size_t) M, 0.0f);
    tapsDown.assign ((size_t) M, 0.0f);
    for (int m = 0; m < M; ++m)
    {
        tapsUp[(size_t) m] = (float) (2.0 * hd[(size_t) (2 * m)]);
        tapsDown[(size_t) m] = (float) hd[(size_t) (2 * m)];
    }
}

// Both filters below use a linear history buffer [M-1 old samples | n new samples]
// and accumulate tap-major so the inner loop runs over output samples (vectorisable).
// Each output still sums its taps in the fixed order m = 0..M-1, so results do not
// depend on how the stream is split into calls.
void HalfbandStage::up (UpState& s, const float* in, float* out, int n) const noexcept
{
    float* hist = s.hist.data();
    const float* taps = tapsUp.data();
    const int base = M - 1;
    for (int done = 0; done < n;)
    {
        const int len = std::min (kMaxBlock, n - done);
        std::memcpy (hist + base, in + done, sizeof (float) * (size_t) len);
        float acc[kMaxBlock];
        for (int i = 0; i < len; ++i)
            acc[i] = 0.0f;
        for (int m = 0; m < M; ++m)
        {
            const float t = taps[m];
            const float* x = hist + base - m;
            for (int i = 0; i < len; ++i)
                acc[i] += t * x[i];
        }
        for (int i = 0; i < len; ++i)
        {
            out[2 * (done + i)] = acc[i];
            out[2 * (done + i) + 1] = hist[base + i - K]; // centre branch: pure delay of K samples
        }
        std::memmove (hist, hist + len, sizeof (float) * (size_t) base);
        done += len;
    }
}

void HalfbandStage::down (DownState& s, const float* in, float* out, int nOut) const noexcept
{
    float* he = s.histE.data();
    float* ho = s.histO.data();
    const float* taps = tapsDown.data();
    const int base = M - 1;
    for (int done = 0; done < nOut;)
    {
        const int len = std::min (kMaxBlock, nOut - done);
        for (int i = 0; i < len; ++i)
        {
            he[base + i] = in[2 * (done + i)];
            ho[base + i] = in[2 * (done + i) + 1];
        }
        float acc[kMaxBlock];
        for (int i = 0; i < len; ++i)
            acc[i] = 0.0f;
        for (int m = 0; m < M; ++m)
        {
            const float t = taps[m];
            const float* x = he + base - m;
            for (int i = 0; i < len; ++i)
                acc[i] += t * x[i];
        }
        for (int i = 0; i < len; ++i)
            out[done + i] = acc[i] + 0.5f * ho[base + i - (K + 1)];
        std::memmove (he, he + len, sizeof (float) * (size_t) base);
        std::memmove (ho, ho + len, sizeof (float) * (size_t) base);
        done += len;
    }
}

const HalfbandStage& Oversampler::stageA()
{
    static const HalfbandStage s = [] { HalfbandStage h; h.design (51, 7.86f); return h; }();
    return s;
}

const HalfbandStage& Oversampler::stageB()
{
    static const HalfbandStage s = [] { HalfbandStage h; h.design (27, 7.86f); return h; }();
    return s;
}

void Oversampler::prepare (int maxBaseSamples)
{
    maxBase = maxBaseSamples;
    const auto& A = stageA();
    const auto& B = stageB();
    for (int ch = 0; ch < 2; ++ch)
    {
        A.initUp (upA[ch]);
        B.initUp (upB[ch]);
        A.initDown (dnA[ch]);
        B.initDown (dnB[ch]);
        osBuf[ch].assign ((size_t) (maxBase * kMaxOS), 0.0f);
        midBuf[ch].assign ((size_t) (maxBase * 2), 0.0f);
    }
    A.initDown (monoA);
    B.initDown (monoB);
    monoMid.assign ((size_t) (maxBase * 2), 0.0f);
    reset();
}

void Oversampler::setFactor (int f) noexcept
{
    fac = (f >= 4) ? 4 : (f >= 2 ? 2 : 1);
    reset();
}

void Oversampler::reset() noexcept
{
    for (int ch = 0; ch < 2; ++ch)
    {
        HalfbandStage::resetUp (upA[ch]);
        HalfbandStage::resetUp (upB[ch]);
        HalfbandStage::resetDown (dnA[ch]);
        HalfbandStage::resetDown (dnB[ch]);
        pad[ch][0] = pad[ch][1] = 0.0f;
    }
    HalfbandStage::resetDown (monoA);
    HalfbandStage::resetDown (monoB);
}

void Oversampler::upsample (const float* inL, const float* inR, int n) noexcept
{
    const float* in[2] = { inL, inR };
    for (int ch = 0; ch < 2; ++ch)
    {
        float* os = osBuf[ch].data();
        if (fac == 1)
        {
            std::memcpy (os, in[ch], sizeof (float) * (size_t) n);
        }
        else if (fac == 2)
        {
            stageA().up (upA[ch], in[ch], os, n);
        }
        else
        {
            float* mid = midBuf[ch].data();
            stageA().up (upA[ch], in[ch], mid, n);
            stageB().up (upB[ch], mid, os, 2 * n);
            // two-sample delay at 4x: (N_B - 1)/4 = 6.5 base samples -> 7
            float p0 = pad[ch][0], p1 = pad[ch][1];
            for (int i = 0; i < 4 * n; ++i)
            {
                const float x = os[i];
                os[i] = p1;
                p1 = p0;
                p0 = x;
            }
            pad[ch][0] = p0;
            pad[ch][1] = p1;
        }
    }
}

void Oversampler::downsample (float* outL, float* outR, int n) noexcept
{
    float* out[2] = { outL, outR };
    for (int ch = 0; ch < 2; ++ch)
    {
        float* os = osBuf[ch].data();
        if (fac == 1)
            std::memcpy (out[ch], os, sizeof (float) * (size_t) n);
        else if (fac == 2)
            stageA().down (dnA[ch], os, out[ch], n);
        else
        {
            float* mid = midBuf[ch].data();
            stageB().down (dnB[ch], os, mid, 2 * n);
            stageA().down (dnA[ch], mid, out[ch], n);
        }
    }
}

void Oversampler::decimateMono (const float* in, float* out, int n) noexcept
{
    if (fac == 1)
        std::memcpy (out, in, sizeof (float) * (size_t) n);
    else if (fac == 2)
        stageA().down (monoA, in, out, n);
    else
    {
        stageB().down (monoB, in, monoMid.data(), 2 * n);
        stageA().down (monoA, monoMid.data(), out, n);
    }
}

} // namespace ds
