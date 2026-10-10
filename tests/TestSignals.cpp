#include "TestSignals.h"

#include "DspCommon.h"
#include "Filters.h"

namespace tu
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;

    void addKick (Stereo& s, double sr, int64_t at, float vel)
    {
        const int len = (int) (0.45 * sr);
        double ph = 0.0;
        for (int i = 0; i < len && at + i < (int64_t) s.size(); ++i)
        {
            const double t = i / sr;
            const double f = 48.0 + 110.0 * std::exp (-t / 0.035);
            ph += 2.0 * kPi * f / sr;
            const double env = std::exp (-t / 0.18) * std::min (1.0, t / 0.001);
            const double click = std::exp (-t / 0.0015) * 0.35;
            const float v = (float) (vel * (0.85 * std::sin (ph) * env + click * std::sin (2 * kPi * 3200 * t)));
            s.L[(size_t) (at + i)] += v;
            s.R[(size_t) (at + i)] += v;
        }
    }

    void addSnare (Stereo& s, double sr, int64_t at, float vel, ds::Rng& rng)
    {
        const int len = (int) (0.35 * sr);
        ds::SvfCoeffs bp;
        bp.bandpassNorm (3500.0f, 0.9f, (float) sr);
        ds::SvfState st[2];
        for (int i = 0; i < len && at + i < (int64_t) s.size(); ++i)
        {
            const double t = i / sr;
            const double tone = 0.45 * std::sin (2 * kPi * 185.0 * t) * std::exp (-t / 0.06);
            const double nenv = std::exp (-t / 0.11) * std::min (1.0, t / 0.0007);
            const float nl = st[0].process (bp, rng.bipolar());
            const float nr = st[1].process (bp, rng.bipolar());
            const double ta = std::min (1.0, t / 0.001);
            s.L[(size_t) (at + i)] += (float) (vel * (tone * ta + 0.9 * nl * nenv));
            s.R[(size_t) (at + i)] += (float) (vel * (tone * ta + 0.9 * nr * nenv));
        }
    }

    void addHat (Stereo& s, double sr, int64_t at, float vel, bool open, ds::Rng& rng)
    {
        const int len = (int) ((open ? 0.3 : 0.08) * sr);
        ds::SvfCoeffs hp;
        hp.highpass (7000.0f, 0.8f, (float) sr);
        ds::SvfState st;
        const double tau = open ? 0.12 : 0.025;
        for (int i = 0; i < len && at + i < (int64_t) s.size(); ++i)
        {
            const double t = i / sr;
            const float n = st.process (hp, rng.bipolar());
            const double env = std::exp (-t / tau) * std::min (1.0, t / 0.0004);
            const float v = (float) (vel * 0.5 * n * env);
            s.L[(size_t) (at + i)] += v * 0.8f;
            s.R[(size_t) (at + i)] += v * 1.0f;
        }
    }
} // namespace

DrumLoop makeDrumLoop (double sr, double bpm, int bars, uint32_t seed, bool bassLine, float level)
{
    DrumLoop d;
    const double sixteenth = 60.0 / bpm / 4.0;
    const int steps = bars * 16;
    const size_t n = (size_t) ((steps * sixteenth + 0.6) * sr);
    d.audio = Stereo (n);
    ds::Rng rng (seed);
    // pattern (per bar): kick on 1, 2-and(a), 3; snare on 2 and 4; closed hats on 8ths, open on 4-and
    const int kickSteps[] = { 0, 7, 8, 10 };
    for (int s = 0; s < steps; ++s)
    {
        const int pos = s % 16;
        const double jitter = (rng.bipolar() * 0.002); // +-2 ms humanisation
        const int64_t at = (int64_t) ((s * sixteenth + 0.01 + std::max (0.0, jitter)) * sr);
        bool isKick = false;
        for (int k : kickSteps)
            if (pos == k && ! (k == 7 && (s / 16) % 2 == 0))
                isKick = true;
        if (isKick)
        {
            addKick (d.audio, sr, at, 0.75f + 0.25f * rng.unipolar());
            d.kicks.push_back (at);
        }
        if (pos == 4 || pos == 12)
        {
            addSnare (d.audio, sr, at, 0.7f + 0.3f * rng.unipolar(), rng);
            d.snares.push_back (at);
        }
        if (pos % 2 == 0 || (pos == 15 && (s / 16) % 2 == 1))
        {
            addHat (d.audio, sr, at, 0.35f + 0.35f * rng.unipolar(), pos == 14, rng);
            d.hats.push_back (at);
        }
    }
    if (bassLine)
    {
        // sustained saw-ish bass (55-82 Hz) as a confounder for the kick detector
        const double notes[] = { 55.0, 55.0, 73.4, 65.4 };
        double ph = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const int bar = (int) (t / (16 * sixteenth));
            const double f = notes[bar % 4];
            ph += 2 * kPi * f / sr;
            const double fadeIn = std::min (1.0, t / 1.0); // no synthetic onset at t = 0
            const double v = fadeIn * 0.18 * (std::sin (ph) + 0.4 * std::sin (2 * ph) + 0.2 * std::sin (3 * ph));
            d.audio.L[i] += (float) v;
            d.audio.R[i] += (float) v;
        }
    }
    for (size_t i = 0; i < n; ++i)
    {
        d.audio.L[i] *= level * 0.8f;
        d.audio.R[i] *= level * 0.8f;
    }
    return d;
}

Stereo sine (double sr, double freq, double amp, size_t n) { return multiSine (sr, { freq }, amp, n); }

Stereo multiSine (double sr, const std::vector<double>& freqs, double amp, size_t n)
{
    Stereo s (n);
    for (size_t i = 0; i < n; ++i)
    {
        double v = 0.0;
        for (double f : freqs)
            v += std::sin (2 * kPi * f * (double) i / sr);
        s.L[i] = s.R[i] = (float) (amp * v);
    }
    return s;
}

Stereo noise (double, double amp, size_t n, uint32_t seed)
{
    Stereo s (n);
    ds::Rng r (seed), r2 (seed * 7 + 1);
    for (size_t i = 0; i < n; ++i)
    {
        s.L[i] = (float) amp * r.bipolar();
        s.R[i] = (float) amp * r2.bipolar();
    }
    return s;
}

Stereo formantBuzz (double sr, double f0, double fFormant, size_t n)
{
    Stereo s (n);
    ds::SvfCoeffs bp;
    bp.bandpassNorm ((float) fFormant, 4.0f, (float) sr);
    ds::SvfState st;
    // band-limited harmonic series (sum of cosines up to 8 kHz)
    const int nh = (int) (8000.0 / f0);
    for (size_t i = 0; i < n; ++i)
    {
        double v = 0.0;
        for (int h = 1; h <= nh; ++h)
            v += std::cos (2 * kPi * f0 * h * (double) i / sr);
        const float y = st.process (bp, (float) (v * 0.02));
        s.L[i] = s.R[i] = y + (float) (v * 0.002);
    }
    return s;
}

} // namespace tu
