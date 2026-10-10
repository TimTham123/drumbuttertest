#include "MasterSection.h"

namespace ds
{
void MasterSection::prepare (double sampleRate)
{
    sr = sampleRate;
    const float fs = (float) sr;
    aFast.setTimes (0.0005f, 0.060f, fs);
    aSlow.setTimes (0.025f, 0.060f, fs);
    sFast.setTimes (0.001f, 0.030f, fs);
    sSlow.setTimes (0.001f, 0.250f, fs);
    const float cr = fs / (float) kChunk;
    const float tau = 0.02f;
    lowG.setup (cr, tau, 0.0f);
    midG.setup (cr, tau, 0.0f);
    highG.setup (cr, tau, 0.0f);
    lowF.setup (cr, tau, std::log (90.0f));
    midF.setup (cr, tau, std::log (1200.0f));
    highF.setup (cr, tau, std::log (8000.0f));
    hpfF.setup (cr, tau, std::log (10.0f));
    lpfF.setup (cr, tau, std::log (22000.0f));
    air.setup (cr, tau, 0.0f);
    atk.setup (cr, tau, 0.0f);
    sus.setup (cr, tau, 0.0f);
    exHp.c.highpass (5000.0f, 0.7071f, fs);
    exHp2.c.highpass (5000.0f, 0.7071f, fs);
    reset();
}

void MasterSection::reset()
{
    aFast.reset();
    aSlow.reset();
    sFast.reset();
    sSlow.reset();
    for (auto* f : { &low, &mid, &high, &hpf, &lpf, &airShelf, &exHp, &exHp2 })
        f->reset();
    tsGain.snap (1.0f);
    first = true;
}

void MasterSection::control (const float* p, float k) noexcept
{
    const float fs = (float) sr;
    lowG.target = clampf (p[kMasterLowGain] * k, -18.0f, 18.0f);
    midG.target = clampf (p[kMasterMidGain] * k, -18.0f, 18.0f);
    highG.target = clampf (p[kMasterHighGain] * k, -18.0f, 18.0f);
    lowF.target = std::log (p[kMasterLowFreq]);
    midF.target = std::log (p[kMasterMidFreq]);
    highF.target = std::log (p[kMasterHighFreq]);
    hpfF.target = std::log (p[kMasterHpf]);
    lpfF.target = std::log (p[kMasterLpf]);
    air.target = clampf (p[kMasterAir] * 0.01f * k, 0.0f, 2.0f);
    atk.target = clampf (p[kMasterAttack] * 0.01f * k, -2.0f, 2.0f);
    sus.target = clampf (p[kMasterSustain] * 0.01f * k, -2.0f, 2.0f);
    hpfOn = p[kMasterHpf] > 10.5f;
    lpfOn = p[kMasterLpf] < 21500.0f;
    if (first)
    {
        for (auto* s : { &lowG, &midG, &highG, &lowF, &midF, &highF, &hpfF, &lpfF, &air, &atk, &sus })
            s->snap (s->target);
        exMix.snap (0.0f);
    }
    const float lg = lowG.step(), mg = midG.step(), hg = highG.step();
    auto engage = [] (bool& flag, bool want, StereoSvf& f) {
        if (want && ! flag)
            f.reset();
        flag = want;
    };
    engage (lowOn, lg != 0.0f, low);
    engage (midOn, mg != 0.0f, mid);
    engage (highOn, hg != 0.0f, high);
    low.c.lowShelf (std::exp (lowF.step()), 0.7071f, lg, fs);
    mid.c.bell (std::exp (midF.step()), 0.8f, mg, fs);
    high.c.highShelf (std::exp (highF.step()), 0.7071f, hg, fs);
    hpf.c.highpass (std::exp (hpfF.step()), 0.7071f, fs);
    lpf.c.lowpass (std::exp (lpfF.step()), 0.7071f, fs);
    const float a = air.step();
    const bool wantAir = a != 0.0f || exMix.end != 0.0f;
    if (wantAir && ! airOn)
    {
        airShelf.reset();
        exHp.reset();
        exHp2.reset();
    }
    airOn = wantAir;
    airShelf.c.highShelf (10000.0f, 0.7071f, 6.0f * a, fs);
    exMix.rampTo (0.2f * a);

    // transient shaper gain (per chunk; ramped per sample)
    const float at = atk.step(), su = sus.step();
    float gDb = 0.0f;
    if (at != 0.0f)
    {
        const float d = std::max (0.0f, gainToDb (aFast.y) - gainToDb (aSlow.y));
        gDb += clampf (at * 0.9f * d, -12.0f, 12.0f);
    }
    if (su != 0.0f)
    {
        const float d = std::max (0.0f, gainToDb (sSlow.y) - gainToDb (sFast.y));
        gDb += clampf (su * 0.9f * d, -12.0f, 12.0f);
    }
    if (aFast.y < 1.0e-5f)
        gDb = 0.0f;
    tsGain.rampTo (dbToGain (gDb));
    tsOn = ! (tsGain.value == 1.0f && tsGain.end == 1.0f);
    first = false;
}

void MasterSection::process (float* L, float* R, int n) noexcept
{
    float* io[2] = { L, R };
    for (int i = 0; i < n; ++i)
    {
        const float lvl = std::max (std::fabs (L[i]), std::fabs (R[i]));
        aFast.process (lvl);
        aSlow.process (lvl);
        sFast.process (lvl);
        sSlow.process (lvl);
        const float g = tsGain.next();
        const float em = exMix.next();
        for (int ch = 0; ch < 2; ++ch)
        {
            float x = tsOn ? io[ch][i] * g : io[ch][i];
            if (lowOn)
                x = low.process (ch, x);
            if (midOn)
                x = mid.process (ch, x);
            if (highOn)
                x = high.process (ch, x);
            const float h = hpf.process (ch, x);
            x = hpfOn ? h : x;
            const float l = lpf.process (ch, x);
            x = lpfOn ? l : x;
            if (airOn)
            {
                x = airShelf.process (ch, x);
                const float hp = exHp.process (ch, x);
                const float e = (fastTanh (2.5f * hp + 0.3f) - 0.29131261f) * 0.4f;
                x += em * exHp2.process (ch, e);
            }
            io[ch][i] = x;
        }
    }
}

} // namespace ds
