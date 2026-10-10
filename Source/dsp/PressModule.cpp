#include "PressModule.h"

namespace ds
{
namespace
{
    inline float logInterp (float lo, float hi, float t) noexcept { return lo * std::pow (hi / lo, clampf (t, 0.0f, 1.0f)); }
    inline float kneeFn (float over, float knee) noexcept
    {
        if (knee <= 0.0f)
            return std::max (0.0f, over);
        if (2.0f * over < -knee)
            return 0.0f;
        if (2.0f * over > knee)
            return over;
        const float t = over + 0.5f * knee;
        return t * t / (2.0f * knee);
    }
    constexpr float kGlueAttack[6] = { 0.0001f, 0.0003f, 0.001f, 0.003f, 0.010f, 0.030f };
    constexpr float kGlueRelease[4] = { 0.1f, 0.3f, 0.6f, 1.2f };
    inline bool isFeedback (int type) noexcept { return type == kPressOpto || type == kPressVariMu; }
} // namespace

float PressModule::gainComputer (float levelDb, float threshDb, float ratio, float kneeDb) noexcept
{
    return (1.0f - 1.0f / std::max (1.0f, ratio)) * kneeFn (levelDb - threshDb, kneeDb);
}

PressModule::TypeSettings PressModule::mapControls (int type, float a, float at, float rl, bool softKnee, bool abi,
                                                    bool autoMakeup) noexcept
{
    TypeSettings s;
    a = clampf (a, 0.0f, 1.0f);
    s.thresh = -36.0f * a;
    switch (type)
    {
        case kPressFET:
            s.allButtons = abi;
            s.ratio = abi ? 20.0f : 4.0f + 8.0f * a;
            s.knee = abi ? 1.0f : 2.0f;
            s.attackSec = logInterp (0.00002f, 0.0008f, at) * (abi ? 0.5f : 1.0f);
            s.releaseSec = logInterp (0.050f, 1.1f, rl);
            s.autoRelease = true;
            s.autoFastSec = 0.3f * s.releaseSec;
            break;
        case kPressOpto:
            s.ratio = 2.0f + 2.0f * a;
            s.knee = 10.0f;
            s.attackSec = logInterp (0.005f, 0.030f, at);
            s.releaseSec = logInterp (0.5f, 5.0f, rl);
            break;
        case kPressVCA:
            s.ratio = 1.5f + 6.5f * a;
            s.knee = softKnee ? 8.0f : 0.0f;
            s.attackSec = logInterp (0.0001f, 0.030f, at);
            s.releaseSec = logInterp (0.020f, 1.0f, rl);
            break;
        case kPressDiode:
            s.ratio = 1.5f + 4.5f * a;
            s.knee = 8.0f;
            s.attackSec = logInterp (0.0005f, 0.050f, at);
            s.releaseSec = logInterp (0.050f, 1.5f, rl);
            break;
        case kPressVariMu:
            s.ratio = 1.2f + 3.0f * a;
            s.knee = 18.0f;
            s.attackSec = logInterp (0.001f, 0.050f, at);
            s.releaseSec = logInterp (0.100f, 2.0f, rl);
            break;
        case kPressGlue:
        default:
        {
            s.ratio = 2.0f * std::pow (5.0f, a);
            s.knee = softKnee ? 6.0f : 1.0f;
            const int ai = (int) std::lround (clampf (at, 0.0f, 1.0f) * 5.0f);
            const int ri = (int) std::lround (clampf (rl, 0.0f, 1.0f) * 4.0f);
            s.attackSec = kGlueAttack[ai];
            s.autoRelease = ri >= 4;
            s.releaseSec = s.autoRelease ? 1.2f : kGlueRelease[ri];
            s.autoFastSec = 0.1f;
            break;
        }
    }
    s.makeupDb = autoMakeup ? 0.5f * std::fabs (s.thresh) * (1.0f - 1.0f / s.ratio) : 0.0f;
    return s;
}

void PressModule::prepare (double sampleRate)
{
    sr = sampleRate;
    os.prepare (kChunk);
    const float chunkRate = (float) sr / (float) kChunk;
    amountSm.setup (chunkRate, 0.03f, 0.35f);
    scSm.setup (chunkRate, 0.02f, std::log (80.0f));
    for (auto& d : dc)
        d.setup (5.0f, (float) sr);
    configure (1, 0);
    reset();
}

void PressModule::configure (int osFactor, int)
{
    os.setFactor (osFactor);
    osr = sr * os.factor();
    fadeLen = 0.025f * (float) osr;
    holdA = tauCoef (0.3f, (float) osr);
    memA = tauCoef (2.0f, (float) osr);
    // gain-reduction state is kept; time constants are recomputed at the next control tick
    for (auto& t : ts)
        updateCoefs (t);
    scHpf.reset();
}

void PressModule::reset()
{
    os.reset();
    for (auto& t : ts)
        t = TypeState {};
    scHpf.reset();
    for (auto& d : dc)
        d.reset();
    grMeter = 0.0f;
    first = true;
}

void PressModule::updateCoefs (TypeState& s) noexcept
{
    const float fs = (float) osr;
    s.aAtt = tauCoef (s.set.attackSec, fs);
    s.aRel = tauCoef (s.set.releaseSec, fs);
    s.mkLin = dbToGain (s.set.makeupDb);
    if (s.set.autoRelease)
    {
        s.aRelFast = tauCoef (s.set.autoFastSec, fs);
        s.aRelSlow = s.aRel;
    }
    else
    {
        s.aRelFast = tauCoef (0.060f, fs);              // opto fast stage
        s.aRelSlow = tauCoef (2.0f * s.set.releaseSec, fs); // opto slow stage upper bound
    }
}

void PressModule::control (const float* p, const ModCtx& ctx) noexcept
{
    const int type = (int) p[kPressType];
    const float amount = clampf (p[kPressAmount] * 0.01f * (1.0f + 0.5f * ctx.push), 0.0f, 1.0f);
    amountSm.target = amount;
    const float scHz = p[kPressScHpf];
    scOn = scHz > 20.5f;
    scSm.target = std::log (clampf (scHz, 20.0f, 500.0f));
    if (first)
    {
        fader.snap (type);
        amountSm.snap (amount);
        scSm.snap (scSm.target);
    }
    else if (fader.request (type, fadeLen))
    {
        // the incoming type inherits the outgoing type's gain reduction
        auto& from = ts[fader.previous];
        auto& to = ts[fader.current];
        to.gr = to.grFast = to.grSlow = from.gr;
        to.fbLevel = from.fbLevel;
        to.hold = from.hold;
        to.mem = from.mem;
    }
    const float a = amountSm.step();
    scHpf.c.highpass (std::exp (scSm.step()), 0.7071f, (float) osr);
    const bool soft = p[kPressKnee] > 0.5f;
    const bool abi = p[kPressAllButtons] > 0.5f;
    const bool mk = p[kPressMakeup] > 0.5f;
    const int types[2] = { fader.current, fader.previous };
    for (int k = 0; k < (fader.active() ? 2 : 1); ++k)
    {
        auto& s = ts[types[k]];
        s.set = mapControls (types[k], a, p[kPressAttack] * 0.01f, p[kPressRelease] * 0.01f, soft, abi, mk);
        updateCoefs (s);
    }
    first = false;
}

float PressModule::color (int type, const TypeState& s, float y) const noexcept
{
    switch (type)
    {
        case kPressFET:
        {
            float k = 0.3f + 0.03f * std::min (s.gr, 20.0f);
            if (s.set.allButtons)
                k *= 2.0f;
            return fastTanh (k * y) / k;
        }
        case kPressOpto: return y + 0.04f * y * y;
        case kPressDiode:
        {
            const float d = std::min (0.4f, 0.02f * s.gr);
            const float v = y + d * 0.5f * y * y;
            return fastTanh (0.5f * v) / 0.5f;
        }
        case kPressVariMu:
        {
            const float v = y + (0.05f + 0.005f * std::min (s.gr, 12.0f)) * y * y;
            return fastTanh (0.35f * v) / 0.35f;
        }
        case kPressVCA:
        case kPressGlue:
        default: return y;
    }
}

void PressModule::runType (int type, TypeState& s, float xL, float xR, float scL, float scR, float& yL,
                           float& yR) noexcept
{
    const auto& set = s.set;
    const float scLevel = std::max (std::fabs (scL), std::fabs (scR));
    float target;
    if (isFeedback (type))
    {
        // feedback: detect the (side-chain filtered) output = input * current gain
        s.fbLevel = scLevel * dbToGain (-s.gr);
        const float over = gainToDb (s.fbLevel) - set.thresh;
        target = (set.ratio - 1.0f) * kneeFn (over, set.knee);
        if (type == kPressVariMu)
            target += 0.015f * std::max (0.0f, over) * std::max (0.0f, over);
    }
    else
    {
        const float lvl = gainToDb (scLevel);
        target = gainComputer (lvl, set.thresh, set.ratio, set.knee);
        if (set.allButtons)
            target += 0.25f * std::max (0.0f, lvl - set.thresh - 3.0f);
    }
    target = std::min (target, 40.0f);

    if (type == kPressOpto)
    {
        s.mem += (1.0f - memA) * (clampf (target / 10.0f, 0.0f, 1.0f) - s.mem);
        const float aSlow = s.aRel + (s.aRelSlow - s.aRel) * s.mem; // tau from 1x to 2x release
        s.grFast = target > s.grFast ? target + s.aAtt * (s.grFast - target) : target + s.aRelFast * (s.grFast - target);
        s.grSlow = target > s.grSlow ? target + s.aAtt * (s.grSlow - target) : target + aSlow * (s.grSlow - target);
        s.gr = 0.5f * (s.grFast + s.grSlow);
    }
    else
    {
        float aRel = s.aRel;
        if (set.autoRelease)
        {
            s.hold += (1.0f - holdA) * ((target > 1.0f ? 1.0f : 0.0f) - s.hold);
            aRel = s.aRelFast + (s.aRelSlow - s.aRelFast) * s.hold;
        }
        s.gr = target > s.gr ? target + s.aAtt * (s.gr - target) : target + aRel * (s.gr - target);
    }
    s.gr = flushTiny (s.gr);

    const float g = std::exp (-0.11512925f * s.gr); // dB -> gain
    yL = color (type, s, xL * g) * s.mkLin;
    yR = color (type, s, xR * g) * s.mkLin;
}

void PressModule::process (const float* inL, const float* inR, float* outL, float* outR, const float*, int n) noexcept
{
    os.upsample (inL, inR, n);
    const int N = n * os.factor();
    float* b[2] = { os.buffer (0), os.buffer (1) };
    for (int i = 0; i < N; ++i)
    {
        const float xL = b[0][i], xR = b[1][i];
        const float scL = scOn ? scHpf.process (0, xL) : xL;
        const float scR = scOn ? scHpf.process (1, xR) : xR;
        float yL, yR;
        runType (fader.current, ts[fader.current], xL, xR, scL, scR, yL, yR);
        if (fader.active())
        {
            const float t = fader.next();
            float pL, pR;
            runType (fader.previous, ts[fader.previous], xL, xR, scL, scR, pL, pR);
            yL = pL + (yL - pL) * t;
            yR = pR + (yR - pR) * t;
        }
        b[0][i] = yL;
        b[1][i] = yR;
        grMeter = std::max (grMeter, ts[fader.current].gr);
    }
    os.downsample (outL, outR, n);
    for (int i = 0; i < n; ++i)
    {
        outL[i] = dc[0].process (outL[i]);
        outR[i] = dc[1].process (outR[i]);
    }
}

} // namespace ds
