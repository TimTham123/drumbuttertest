#include "RoomModule.h"

namespace ds
{
namespace
{
    constexpr float kFdnBase[8] = { 0.0231f, 0.0277f, 0.0313f, 0.0369f, 0.0413f, 0.0479f, 0.0533f, 0.0611f };
    constexpr float kFdnOutL[8] = { 1, 1, -1, -1, 1, 1, -1, -1 };
    constexpr float kFdnOutR[8] = { 1, -1, -1, 1, -1, 1, 1, -1 };
    constexpr float kFdnInSign[8] = { 1, 1, -1, 1, 1, -1, -1, -1 };

    // early reflections (ms, gain) - left and right patterns (scaled by size)
    constexpr int kNumEr = 7;
    constexpr float kErTimeL[kNumEr] = { 3.1f, 7.3f, 11.9f, 17.1f, 22.3f, 29.9f, 37.1f };
    constexpr float kErTimeR[kNumEr] = { 4.3f, 8.9f, 13.7f, 19.3f, 25.1f, 31.7f, 41.3f };
    constexpr float kErGain[kNumEr] = { 0.80f, -0.66f, 0.55f, -0.46f, 0.38f, -0.31f, 0.25f };

    // Dattorro reference lengths at 29761 Hz
    constexpr float kDatInAp[4] = { 142.0f, 107.0f, 379.0f, 277.0f };
    constexpr float kDatAp1[2] = { 672.0f, 908.0f };
    constexpr float kDatD1[2] = { 4453.0f, 4217.0f };
    constexpr float kDatAp2[2] = { 1800.0f, 2656.0f };
    constexpr float kDatD2[2] = { 3720.0f, 3163.0f };
    constexpr float kDatExcursion = 8.0f;
    constexpr float kDatLoop = (672.0f + 4453.0f + 1800.0f + 3720.0f + 908.0f + 4217.0f + 2656.0f + 3163.0f) / 29761.0f;

    constexpr float kVinBase[4] = { 0.0297f, 0.0371f, 0.0439f, 0.0533f };
    constexpr float kVinRate[4] = { 0.43f, 0.61f, 0.89f, 1.27f };
    constexpr float kVinDepth = 0.0015f;

    inline void absorption (float dSec, float t60, float hfRatio, float& a, float& b) noexcept
    {
        const float gdc = std::pow (10.0f, -3.0f * dSec / std::max (0.05f, t60));
        const float gny = std::pow (10.0f, -3.0f * dSec / std::max (0.02f, t60 * hfRatio));
        a = (gdc - gny) / (gdc + gny);
        b = gdc * (1.0f - a);
    }
} // namespace

//==============================================================================
void Fdn8::prepare (float sampleRate, float maxSeconds)
{
    sr = sampleRate;
    for (auto& l : line)
        l.prepare ((int) (maxSeconds * sr) + 8);
    const float dl[2][2] = { { 0.0047f, 0.0016f }, { 0.0053f, 0.0019f } };
    for (int ch = 0; ch < 2; ++ch)
        for (int k = 0; k < 2; ++k)
        {
            diff[ch][k].prepare ((int) (dl[ch][k] * sr) + 4);
            diff[ch][k].setDelay ((int) (dl[ch][k] * sr));
        }
    reset();
}

void Fdn8::reset()
{
    for (auto& l : line)
        l.reset();
    for (auto& c : diff)
        for (auto& d : c)
            d.reset();
    for (auto& v : lp)
        v = 0.0f;
}

void Fdn8::setDecay (float t60, float hfRatio, float sizeScale) noexcept
{
    for (int i = 0; i < N; ++i)
        absorption (kFdnBase[i] * sizeScale, t60, hfRatio, ca[i], cb[i]);
}

float Fdn8::meanDelaySec (float sizeScale) const noexcept
{
    float s = 0.0f;
    for (float b : kFdnBase)
        s += b;
    return s / (float) N * sizeScale;
}

void Fdn8::process (float inL, float inR, float sizeScale, float& outL, float& outR) noexcept
{
    const float dL = diff[0][1].process (diff[0][0].process (inL, 0.6f), 0.6f);
    const float dR = diff[1][1].process (diff[1][0].process (inR, 0.6f), 0.6f);
    float y[N];
    float sum = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        const float d = std::max (2.0f, kFdnBase[i] * sizeScale * sr);
        const float o = line[i].readLinear (d - 1.0f);
        lp[i] = flushTiny (cb[i] * o + ca[i] * lp[i]);
        y[i] = lp[i];
        sum += y[i];
    }
    sum *= 2.0f / (float) N;
    float oL = 0.0f, oR = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        const float in = ((i & 1) == 0 ? dL : dR) * kFdnInSign[i] * 0.5f;
        line[i].write (y[i] - sum + in);
        oL += kFdnOutL[i] * y[i];
        oR += kFdnOutR[i] * y[i];
    }
    outL = oL * 0.35355339f;
    outR = oR * 0.35355339f;
}

//==============================================================================
void DattorroPlate::prepare (float sampleRate)
{
    sr = sampleRate;
    k = sr / 29761.0f;
    const float maxS = 1.35f;
    for (int i = 0; i < 4; ++i)
    {
        inAp[i].prepare ((int) (kDatInAp[i] * k) + 4);
        inAp[i].setDelay ((int) std::lround (kDatInAp[i] * k));
    }
    for (int h = 0; h < 2; ++h)
    {
        tankAp1[h].prepare ((int) ((kDatAp1[h] + kDatExcursion + 2) * k * maxS) + 8);
        tankAp2[h].prepare ((int) (kDatAp2[h] * k * maxS) + 8);
        d1[h].prepare ((int) (kDatD1[h] * k * maxS) + 8);
        d2[h].prepare ((int) (kDatD2[h] * k * maxS) + 8);
    }
    lfoInc = (float) (2.0 * kPiD / sr);
    reset();
}

void DattorroPlate::reset()
{
    for (auto& a : inAp)
        a.reset();
    for (int h = 0; h < 2; ++h)
    {
        tankAp1[h].reset();
        tankAp2[h].reset();
        d1[h].reset();
        d2[h].reset();
        damp[h] = 0.0f;
        fb[h] = 0.0f;
    }
    lfoPhase = 0.0;
}

float DattorroPlate::loopSec (float sizeScale) const noexcept { return kDatLoop * sizeScale; }

void DattorroPlate::setParams (float t60, float damping, float sizeScale) noexcept
{
    // the decay gain is applied twice per tank half (after the damping filter and on the
    // cross-feed), i.e. four times per figure-of-eight loop
    decay = clampf (std::pow (10.0f, -3.0f * loopSec (sizeScale) / (4.0f * std::max (0.1f, t60))), 0.0f, 0.97f);
    dampCoef = clampf (damping, 0.0f, 0.95f);
    decayDiff2 = clampf (decay + 0.15f, 0.25f, 0.5f);
}

void DattorroPlate::process (float inL, float inR, float s, float& outL, float& outR) noexcept
{
    float x = 0.5f * (inL + inR);
    x = inAp[0].process (x, 0.75f);
    x = inAp[1].process (x, 0.75f);
    x = inAp[2].process (x, 0.625f);
    x = inAp[3].process (x, 0.625f);

    lfoPhase += lfoInc;
    if (lfoPhase > 2.0 * kPiD)
        lfoPhase -= 2.0 * kPiD;
    const float lfo[2] = { (float) std::sin (lfoPhase), (float) std::cos (lfoPhase) };
    const float ks = k * s;
    float newFb[2];
    for (int h = 0; h < 2; ++h)
    {
        const float u = x + decay * fb[1 - h];
        const float apd = (kDatAp1[h] + kDatExcursion * lfo[h]) * ks;
        const float v = tankAp1[h].processFrac (u, -0.7f, std::max (2.0f, apd));
        d1[h].write (v);
        const float w = d1[h].readLinear (kDatD1[h] * ks);
        damp[h] = flushTiny (w * (1.0f - dampCoef) + damp[h] * dampCoef);
        const float z = damp[h] * decay;
        const float z2 = tankAp2[h].processFrac (z, decayDiff2, std::max (2.0f, kDatAp2[h] * ks));
        d2[h].write (z2);
        newFb[h] = d2[h].readLinear (kDatD2[h] * ks);
    }
    fb[0] = newFb[0];
    fb[1] = newFb[1];

    auto t = [ks] (float v) { return (int) (v * ks); };
    const float yl = d1[1].read (t (266)) + d1[1].read (t (2974)) - tankAp2[1].tap (t (1913)) + d2[1].read (t (1996))
                     - d1[0].read (t (1990)) - tankAp2[0].tap (t (187)) - d2[0].read (t (1066));
    const float yr = d1[0].read (t (353)) + d1[0].read (t (3627)) - tankAp2[0].tap (t (1228)) + d2[0].read (t (2673))
                     - d1[1].read (t (2111)) - tankAp2[1].tap (t (335)) - d2[1].read (t (121));
    outL = 0.6f * yl;
    outR = 0.6f * yr;
}

//==============================================================================
void SpringTank::prepare (float sampleRate)
{
    sr = sampleRate;
    const float dsec[2] = { 89.0e-6f, 97.0e-6f };
    for (int ch = 0; ch < 2; ++ch)
    {
        const float D = dsec[ch] * sr;
        apCoef[ch] = (1.0f - D) / (1.0f + D);
        loop[ch].prepare ((int) (0.06f * sr) + 8);
    }
    hpC.highpass (150.0f, 0.7071f, sr);
    reset();
}

void SpringTank::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int s = 0; s < kStages; ++s)
            apX[ch][s] = apY[ch][s] = 0.0f;
        loop[ch].reset();
        lp[ch].reset();
        hpS[ch].reset();
        modCur[ch] = modTarget[ch] = 0.0f;
    }
    modCounter = 0;
}

void SpringTank::setParams (float t60, float sizeScale, float tone) noexcept
{
    loopBaseSec = 0.028f + 0.017f * clampf ((sizeScale - 0.8f) / 0.5f, 0.0f, 1.0f);
    loopDelay[0] = loopBaseSec * sr;
    loopDelay[1] = loopBaseSec * 1.13f * sr;
    const float loopTotal = loopBaseSec + 0.0085f;
    loopGain = clampf (std::pow (10.0f, -3.0f * loopTotal / std::max (0.1f, t60)), 0.0f, 0.985f);
    for (int ch = 0; ch < 2; ++ch)
        lp[ch].setCutoff (4500.0f * (0.6f + 0.8f * tone), sr);
}

void SpringTank::process (float inL, float inR, float& outL, float& outR) noexcept
{
    if (--modCounter <= 0)
    {
        modCounter = (int) (0.05f * sr);
        for (int ch = 0; ch < 2; ++ch)
            modTarget[ch] = rng.bipolar() * 0.0003f * sr;
    }
    const float in[2] = { inL, inR };
    float out[2];
    for (int ch = 0; ch < 2; ++ch)
    {
        modCur[ch] += 0.0005f * (modTarget[ch] - modCur[ch]);
        const float fbk = lp[ch].process (loop[ch].readLinear (std::max (1.0f, loopDelay[ch] + modCur[ch])));
        float u = in[ch] + loopGain * fbk;
        const float a = apCoef[ch];
        float* xs = apX[ch];
        float* ys = apY[ch];
        for (int s = 0; s < kStages; ++s)
        {
            const float y = a * (u - ys[s]) + xs[s];
            xs[s] = u;
            ys[s] = flushTiny (y);
            u = y;
        }
        loop[ch].write (u);
        out[ch] = hpS[ch].process (hpC, u);
    }
    outL = out[0];
    outR = out[1];
}

//==============================================================================
void VintageVerb::prepare (float sampleRate)
{
    sr = sampleRate;
    for (auto& l : line)
        l.prepare ((int) ((0.0533f * 1.55f + kVinDepth * 2.0f) * sr) + 8);
    const float dd[2] = { 0.0061f, 0.0073f };
    for (int ch = 0; ch < 2; ++ch)
    {
        diff[ch].prepare ((int) (dd[ch] * sr) + 4);
        diff[ch].setDelay ((int) (dd[ch] * sr));
    }
    for (int i = 0; i < N; ++i)
        lfoInc[i] = (float) (2.0 * kPiD * kVinRate[i] / sr);
    reset();
}

void VintageVerb::reset()
{
    for (auto& l : line)
        l.reset();
    for (auto& d : diff)
        d.reset();
    for (int i = 0; i < N; ++i)
    {
        lp[i] = 0.0f;
        lfo[i] = (double) i * 1.3;
    }
    for (auto& c : bw)
        for (auto& f : c)
            f.reset();
}

float VintageVerb::meanDelaySec (float sizeScale) const noexcept
{
    float s = 0.0f;
    for (float b : kVinBase)
        s += b;
    return s / (float) N * sizeScale;
}

void VintageVerb::setParams (float t60, float tone, float sizeScale) noexcept
{
    for (int i = 0; i < N; ++i)
        absorption (kVinBase[i] * sizeScale, t60, 0.3f + 0.5f * tone, ca[i], cb[i]);
    const float fc = 4000.0f + 5000.0f * tone;
    for (auto& c : bw)
        for (auto& f : c)
            f.setCutoff (fc, sr);
}

void VintageVerb::process (float inL, float inR, float s, float& outL, float& outR) noexcept
{
    const float dL = diff[0].process (inL, 0.65f);
    const float dR = diff[1].process (inR, 0.65f);
    float y[N];
    for (int i = 0; i < N; ++i)
    {
        lfo[i] += lfoInc[i];
        if (lfo[i] > 2.0 * kPiD)
            lfo[i] -= 2.0 * kPiD;
        const float d = (kVinBase[i] * s + kVinDepth * (float) std::sin (lfo[i])) * sr;
        const float o = line[i].readLinear (std::max (1.0f, d - 1.0f));
        lp[i] = flushTiny (cb[i] * o + ca[i] * lp[i]);
        y[i] = lp[i];
    }
    // 4x4 Hadamard (orthonormal)
    const float h0 = 0.5f * (y[0] + y[1] + y[2] + y[3]);
    const float h1 = 0.5f * (y[0] - y[1] + y[2] - y[3]);
    const float h2 = 0.5f * (y[0] + y[1] - y[2] - y[3]);
    const float h3 = 0.5f * (y[0] - y[1] - y[2] + y[3]);
    line[0].write (h0 + 0.5f * dL);
    line[1].write (h1 + 0.5f * dR);
    line[2].write (h2 - 0.5f * dL);
    line[3].write (h3 - 0.5f * dR);
    float oL = 0.7f * (y[0] - y[3]);
    float oR = 0.7f * (y[1] - y[2]);
    oL = bw[0][1].process (bw[0][0].process (oL));
    oR = bw[1][1].process (bw[1][0].process (oR));
    // 12-bit grain
    outL = std::round (oL * 2048.0f) * (1.0f / 2048.0f);
    outR = std::round (oR * 2048.0f) * (1.0f / 2048.0f);
}

//==============================================================================
float RoomModule::typeGain (int type) noexcept
{
    // calibrated with the harness (room.gain) so that 100% mix gives a wet RMS
    // roughly equal to the dry RMS on the reference drum loop at default settings
    static const float g[kNumRoomTypes] = { 0.957f, 0.829f, 1.58f, 0.669f, 1.80f };
    return g[std::min (kNumRoomTypes - 1, std::max (0, type))];
}

void RoomModule::prepare (double sampleRate)
{
    sr = sampleRate;
    const float fs = (float) sr;
    for (int ch = 0; ch < 2; ++ch)
    {
        pre[ch].prepare ((int) ((0.2f + 0.045f * 1.6f) * fs) + 16);
        bwLp[ch].reset();
    }
    roomFdn.prepare (fs, 0.1f);
    gateFdn.prepare (fs, 0.1f);
    plate.prepare (fs);
    spring.prepare (fs);
    vintage.prepare (fs);
    const float chunkRate = fs / (float) kChunk;
    sizeSm.setup (chunkRate, 0.08f, 0.4f);
    decaySm.setup (chunkRate, 0.05f, 0.8f);
    preSm.setup (chunkRate, 0.05f, 8.0f);
    toneSm.setup (chunkRate, 0.03f, 0.55f);
    widthSm.setup (chunkRate, 0.03f, 1.0f);
    levelSm.setup (chunkRate, 0.03f, 1.0f);
    xfInLen = (int) (0.05f * fs);
    xfOutLen = (int) (0.25f * fs);
    gateKey.setTimes (0.001f, 0.01f, fs);
    gateAtt = tauCoef (0.001f, fs);
    gateRel = tauCoef (0.015f, fs);
    reset();
}

void RoomModule::configure (int, int) {} // not oversampled: nothing to change

void RoomModule::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        pre[ch].reset();
        bwLp[ch].reset();
    }
    roomFdn.reset();
    gateFdn.reset();
    plate.reset();
    spring.reset();
    vintage.reset();
    gateKey.reset();
    gateGain = 0.0f;
    gateHold = 0;
    xfActive = false;
    first = true;
}

float RoomModule::typeNorm (int type) const noexcept
{
    // Wet energy of a drum loop grows with decay time; this law (fitted by the
    // harness, room.gain) keeps the wet level within ~1 dB for Decay 0.3..8 s.
    float t = decaySec;
    if (type == kRoomGated)
        t = std::max (0.05f, (float) gateHoldSamples / (float) sr);
    return typeGain (type) * std::pow (1.0f + t / 0.6f, -0.45f);
}

void RoomModule::control (const float* p, const ModCtx& ctx) noexcept
{
    const int type = (int) p[kRoomType];
    sizeSm.target = p[kRoomSize] * 0.01f;
    decaySm.target = p[kRoomDecay];
    preSm.target = p[kRoomPredelay];
    toneSm.target = p[kRoomTone] * 0.01f;
    widthSm.target = p[kRoomWidth] * 0.01f;
    levelSm.target = dbToGain (4.0f * ctx.push);
    if (first)
    {
        curType = prevType = type;
        xfActive = false;
        sizeSm.snap (sizeSm.target);
        decaySm.snap (decaySm.target);
        preSm.snap (preSm.target);
        toneSm.snap (toneSm.target);
        widthSm.snap (widthSm.target);
        levelSm.snap (levelSm.target);
        sizeRamp.snap (sizeSm.target);
        preRamp.snap (preSm.target * 0.001f * (float) sr);
        widthRamp.snap (widthSm.target);
    }
    else if (type != curType && ! xfActive)
    {
        prevType = curType;
        curType = type;
        xfActive = true;
        xfCount = 0;
        switch (type)
        {
            case kRoomRoom: roomFdn.reset(); break;
            case kRoomPlate: plate.reset(); break;
            case kRoomSpring: spring.reset(); break;
            case kRoomGated: gateFdn.reset(); gateGain = 0.0f; gateHold = 0; break;
            case kRoomVintage: vintage.reset(); break;
            default: break;
        }
    }

    const float size = sizeSm.step();
    decaySec = decaySm.step();
    const float tone = toneSm.step();
    sizeRamp.rampTo (size);
    preRamp.rampTo (preSm.step() * 0.001f * (float) sr);
    widthRamp.rampTo (widthSm.step());

    roomScale = 0.35f + 1.25f * size;
    plateScale = 0.5f + 0.8f * size;
    vintageScale = 0.5f + 1.0f * size;
    const float hf = 0.15f + 0.75f * tone;
    roomFdn.setDecay (decaySec, hf, roomScale);
    gateFdn.setDecay (3.0f, hf, roomScale);
    plate.setParams (decaySec, 0.05f + 0.65f * (1.0f - tone), plateScale);
    spring.setParams (decaySec, 0.8f + 0.5f * size, tone);
    vintage.setParams (decaySec, tone, vintageScale);
    gateHoldSamples = (int) (clampf (decaySec, 0.04f, 1.5f) * (float) sr);
    const float bwHz = 2500.0f * std::pow (18000.0f / 2500.0f, tone);
    for (auto& f : bwLp)
        f.setCutoff (bwHz, (float) sr);

    for (int t = 0; t < kNumRoomTypes; ++t)
        norm[t] = typeNorm (t);
    levelRamp.rampTo (levelSm.step());
    first = false;
}

void RoomModule::runType (int type, float inGain, float preL, float preR, float& wL, float& wR) noexcept
{
    preL *= inGain;
    preR *= inGain;
    const float erScale = roomScale * 0.001f * (float) sr;
    auto early = [&] (float& eL, float& eR, float pd) {
        eL = eR = 0.0f;
        for (int k = 0; k < kNumEr; ++k)
        {
            eL += kErGain[k] * pre[0].readLinear (pd + kErTimeL[k] * erScale);
            eR += kErGain[k] * pre[1].readLinear (pd + kErTimeR[k] * erScale);
        }
        eL *= inGain;
        eR *= inGain;
    };
    switch (type)
    {
        case kRoomRoom:
        {
            float eL, eR, lL, lR;
            early (eL, eR, preRamp.value);
            roomFdn.process (preL + 0.3f * eL, preR + 0.3f * eR, roomScale, lL, lR);
            wL = 0.55f * eL + lL;
            wR = 0.55f * eR + lR;
            break;
        }
        case kRoomGated:
        {
            float eL, eR, lL, lR;
            early (eL, eR, preRamp.value);
            gateFdn.process (preL + 0.3f * eL, preR + 0.3f * eR, roomScale, lL, lR);
            wL = (0.4f * eL + lL) * gateGain;
            wR = (0.4f * eR + lR) * gateGain;
            break;
        }
        case kRoomPlate: plate.process (preL, preR, plateScale, wL, wR); break;
        case kRoomSpring: spring.process (preL, preR, wL, wR); break;
        case kRoomVintage:
        default: vintage.process (preL, preR, vintageScale, wL, wR); break;
    }
}

void RoomModule::process (const float* inL, const float* inR, float* outL, float* outR, const float* w, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float sL = bwLp[0].process (inL[i] * w[i]);
        const float sR = bwLp[1].process (inR[i] * w[i]);
        pre[0].write (sL);
        pre[1].write (sR);
        sizeRamp.next();
        const float pd = preRamp.next();
        const float preL = pre[0].readLinear (pd);
        const float preR = pre[1].readLinear (pd);

        // gate key from the (un-delayed) send
        const float key = gateKey.process (std::max (std::fabs (sL), std::fabs (sR)));
        if (key > 0.0158f)
            gateHold = gateHoldSamples + (int) pd;
        const float gt = gateHold > 0 ? 1.0f : 0.0f;
        if (gateHold > 0)
            --gateHold;
        gateGain = gt + (gt > gateGain ? gateAtt : gateRel) * (gateGain - gt);

        float wL, wR;
        if (! xfActive)
        {
            runType (curType, 1.0f, preL, preR, wL, wR);
            wL *= norm[curType];
            wR *= norm[curType];
        }
        else
        {
            const float tin = raisedCos ((float) xfCount / (float) xfInLen);
            const float gOld = xfCount < xfInLen ? 1.0f : 1.0f - raisedCos ((float) (xfCount - xfInLen) / (float) xfOutLen);
            float pL, pR;
            runType (curType, tin, preL, preR, wL, wR);
            runType (prevType, 1.0f - tin, preL, preR, pL, pR);
            wL = wL * norm[curType] + pL * norm[prevType] * gOld;
            wR = wR * norm[curType] + pR * norm[prevType] * gOld;
            if (++xfCount >= xfInLen + xfOutLen)
                xfActive = false;
        }
        const float width = widthRamp.next();
        const float lvl = levelRamp.next();
        const float m = 0.5f * (wL + wR);
        const float sd = 0.5f * (wL - wR) * width;
        outL[i] = inL[i] + (m + sd) * lvl;
        outR[i] = inR[i] + (m - sd) * lvl;
    }
}

} // namespace ds
