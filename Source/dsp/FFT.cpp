#include "FFT.h"

namespace ds
{
void RealFFT::prepare (int n)
{
    N = n;
    M = n / 2;
    logM = 0;
    while ((1 << logM) < M)
        ++logM;
    // per-stage twiddles: stage 'len' uses entries [len/2 - 1, len - 1) -> e^{-j 2 pi k / len}
    twr.assign ((size_t) std::max (1, M), 0.0f);
    twi.assign ((size_t) std::max (1, M), 0.0f);
    for (int len = 2; len <= M; len <<= 1)
        for (int k = 0; k < len / 2; ++k)
        {
            twr[(size_t) (len / 2 - 1 + k)] = (float) std::cos (2.0 * kPiD * k / len);
            twi[(size_t) (len / 2 - 1 + k)] = (float) std::sin (2.0 * kPiD * k / len);
        }
    wr.resize ((size_t) (M + 1));
    wi.resize ((size_t) (M + 1));
    for (int k = 0; k <= M; ++k)
    {
        wr[(size_t) k] = (float) std::cos (2.0 * kPiD * k / N);
        wi[(size_t) k] = (float) -std::sin (2.0 * kPiD * k / N);
    }
    bitrev.resize ((size_t) M);
    for (int i = 0; i < M; ++i)
    {
        int r = 0;
        for (int b = 0; b < logM; ++b)
            if (i & (1 << b))
                r |= 1 << (logM - 1 - b);
        bitrev[(size_t) i] = r;
    }
    zr.assign ((size_t) M, 0.0f);
    zi.assign ((size_t) M, 0.0f);
}

void RealFFT::complexFFT (float* re, float* im, bool inverseDir) noexcept
{
    for (int i = 0; i < M; ++i)
    {
        const int j = bitrev[(size_t) i];
        if (i < j)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }
    // stage len = 2 (twiddle 1)
    for (int i = 0; i + 1 < M; i += 2)
    {
        const float ur = re[i], ui = im[i], vr = re[i + 1], vi = im[i + 1];
        re[i] = ur + vr;
        im[i] = ui + vi;
        re[i + 1] = ur - vr;
        im[i + 1] = ui - vi;
    }
    // stage len = 4 (twiddles 1 and -/+j)
    if (M >= 4)
    {
        for (int i = 0; i < M; i += 4)
        {
            {
                const float ur = re[i], ui = im[i], vr = re[i + 2], vi = im[i + 2];
                re[i] = ur + vr;
                im[i] = ui + vi;
                re[i + 2] = ur - vr;
                im[i + 2] = ui - vi;
            }
            {
                const float ur = re[i + 1], ui = im[i + 1];
                // v = x[b] * (-j) forward, * (+j) inverse
                const float vr = inverseDir ? -im[i + 3] : im[i + 3];
                const float vi = inverseDir ? re[i + 3] : -re[i + 3];
                re[i + 1] = ur + vr;
                im[i + 1] = ui + vi;
                re[i + 3] = ur - vr;
                im[i + 3] = ui - vi;
            }
        }
    }
    const float sgn = inverseDir ? 1.0f : -1.0f;
    for (int len = 8; len <= M; len <<= 1)
    {
        const int half = len >> 1;
        const float* cr = twr.data() + (half - 1);
        const float* ci = twi.data() + (half - 1);
        for (int i = 0; i < M; i += len)
        {
            float* ar = re + i;
            float* ai = im + i;
            float* br = re + i + half;
            float* bi = im + i + half;
            for (int k = 0; k < half; ++k)
            {
                const float c = cr[k];
                const float s = sgn * ci[k];
                const float vr = br[k] * c - bi[k] * s;
                const float vi = br[k] * s + bi[k] * c;
                br[k] = ar[k] - vr;
                bi[k] = ai[k] - vi;
                ar[k] += vr;
                ai[k] += vi;
            }
        }
    }
}

void RealFFT::forward (const float* in, float* re, float* im) noexcept
{
    float* r = zr.data();
    float* i_ = zi.data();
    for (int n = 0; n < M; ++n)
    {
        r[n] = in[2 * n];
        i_[n] = in[2 * n + 1];
    }
    complexFFT (r, i_, false);
    for (int k = 0; k <= M; ++k)
    {
        const int ka = k % M;
        const int kb = (M - k) % M;
        const float ar = r[ka], ai = i_[ka];
        const float br = r[kb], bi = i_[kb];
        const float er = 0.5f * (ar + br), ei = 0.5f * (ai - bi);
        const float orr = 0.5f * (ai + bi), oi = -0.5f * (ar - br);
        const float c = wr[(size_t) k], s = wi[(size_t) k];
        re[k] = er + (c * orr - s * oi);
        im[k] = ei + (c * oi + s * orr);
    }
}

void RealFFT::inverse (const float* re, const float* im, float* out) noexcept
{
    float* r = zr.data();
    float* i_ = zi.data();
    for (int k = 0; k < M; ++k)
    {
        const float xr = re[k], xi = im[k];
        const float yr = re[M - k], yi = -im[M - k]; // conj(X[M-k])
        const float er = 0.5f * (xr + yr), ei = 0.5f * (xi + yi);
        const float dr = 0.5f * (xr - yr), di = 0.5f * (xi - yi);
        // O = d * conj(W^k)
        const float c = wr[(size_t) k], s = -wi[(size_t) k];
        const float orr = dr * c - di * s;
        const float oi = dr * s + di * c;
        // Z = E + j*O
        r[k] = er - oi;
        i_[k] = ei + orr;
    }
    complexFFT (r, i_, true);
    const float scale = 1.0f / (float) M;
    for (int n = 0; n < M; ++n)
    {
        out[2 * n] = r[n] * scale;
        out[2 * n + 1] = i_[n] * scale;
    }
}

} // namespace ds
