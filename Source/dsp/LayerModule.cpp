#include "LayerModule.h"

namespace ds
{
void LayerModule::prepare (double sampleRate)
{
    sr = sampleRate;
    subTrig.prepare ((float) sr, OnsetTrigger::Band::Low);
    noiseTrig.prepare ((float) sr, OnsetTrigger::Band::High);
    decim.prepare (kChunk);
    subOs.assign ((size_t) (kChunk * kMaxOS), 0.0f);
    const float chunkRate = (float) sr / (float) kChunk;
    subLevelSm.setup (chunkRate, 0.02f, 0.25f);
    noiseLevelSm.setup (chunkRate, 0.02f, 0.06f);
    toneSm.setup (chunkRate, 0.02f, 0.5f);
    growlSm.setup (chunkRate, 0.02f, 0.0f);
    typeFadeLen = 0.02f * (float) sr;
    noiseDecisionSamples = std::max (1, (int) (0.008f * (float) sr));
    noiseVetoBefore = (int) (0.003f * (float) sr);
    noiseKillCoef = std::exp (-1.0f / (0.002f * (float) sr));
    noiseAttackInc = 1.0f / (0.0005f * (float) sr);
    configure (1, 0);
    reset();
}

void LayerModule::configure (int f, int)
{
    decim.setFactor (f); // voices are kept; only the decimator restarts
    osFactor = decim.factor();
}

void LayerModule::reset()
{
    subTrig.reset();
    noiseTrig.reset();
    decim.reset();
    for (auto& v : voices)
        v = SubVoice {};
    nv = NoiseVoice {};
    for (int ch = 0; ch < 2; ++ch)
    {
        for (auto& b : pink[ch])
            b = 0.0f;
        crackleHp[ch] = 0.0f;
        bandS[ch].reset();
        for (auto& t : toneLp[ch])
            t.reset();
    }
    sampleCount = 0;
    lastSubAt = lastNoiseAt = -1;
    pendingVoice = -1;
    noisePending = 0;
    first = true;
}

void LayerModule::control (const float* p, const ModCtx& ctx) noexcept
{
    subTrig.setSensitivity (p[kLayerSubSens] * 0.01f);
    noiseTrig.setSensitivity (p[kLayerNoiseSens] * 0.01f);
    subFreq = p[kLayerSubFreq];
    subDrop = p[kLayerSubDrop];
    subDecay = p[kLayerSubDecay] * 0.001f;
    const float osr = (float) sr * (float) osFactor;
    subDecayCoef = std::exp (-6.9077553f / (subDecay * osr));
    subLevelSm.target = dbToGain (p[kLayerSubLevel] + 6.0f * ctx.push);
    noiseLevelSm.target = dbToGain (p[kLayerNoiseLevel] + 6.0f * ctx.push);
    toneSm.target = p[kLayerNoiseTone] * 0.01f;
    growlSm.target = p[kLayerSubGrowl] * 0.01f;
    noiseDecayCoef = std::exp (-6.9077553f / (p[kLayerNoiseDecay] * 0.001f * (float) sr));
    const int type = (int) p[kLayerNoiseType];
    if (first)
    {
        typeFader.snap (type);
        subLevelSm.snap (subLevelSm.target);
        noiseLevelSm.snap (noiseLevelSm.target);
        subLevelRamp.snap (subLevelSm.target);
        noiseLevelRamp.snap (noiseLevelSm.target);
        toneSm.snap (toneSm.target);
        growlSm.snap (growlSm.target);
    }
    else
        typeFader.request (type, typeFadeLen);

    subLevelRamp.rampTo (subLevelSm.step());
    noiseLevelRamp.rampTo (noiseLevelSm.step());
    growlSm.step();
    const float t = toneSm.step();
    toneMixLow = std::min (1.0f, 2.0f * (1.0f - t));
    toneMixHigh = std::min (1.0f, 2.0f * t);
    for (int ch = 0; ch < 2; ++ch)
        for (auto& tl : toneLp[ch])
            tl.setCutoff (2000.0f, (float) sr);
    bandC.bandpassNorm (300.0f * std::pow (40.0f, t), 2.5f, (float) sr);
    first = false;
}

void LayerModule::triggerSub (float phase0, float velocity) noexcept
{
    // fade out the currently sounding voice(s)
    for (auto& v : voices)
        if (v.active)
            v.releasing = true;
    auto& v = voices[nextVoice];
    nextVoice ^= 1;
    v.active = true;
    v.releasing = false;
    v.phase = phase0;
    v.t = 0.0f;
    v.amp = velocity;
    v.env = 1.0f;
    v.fade = 1.0f;
    v.pitchEnv = 1.0f;
    v.atkPos = 0.0f;
}

float LayerModule::noiseSample (int type, int ch) noexcept
{
    auto& r = rng[ch];
    switch (type)
    {
        case kNoiseWhite: return r.bipolar() * 0.8f;
        case kNoisePink:
        {
            const float w = r.bipolar();
            float* b = pink[ch];
            b[0] = 0.99886f * b[0] + w * 0.0555179f;
            b[1] = 0.99332f * b[1] + w * 0.0750759f;
            b[2] = 0.96900f * b[2] + w * 0.1538520f;
            b[3] = 0.86650f * b[3] + w * 0.3104856f;
            b[4] = 0.55000f * b[4] + w * 0.5329522f;
            b[5] = -0.7616f * b[5] - w * 0.0168980f;
            const float out = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f;
            b[6] = w * 0.115926f;
            return out * 0.25f;
        }
        case kNoiseCrackle:
        {
            const float pr = 4000.0f / (float) sr;
            float x = 0.0f;
            if (r.unipolar() < pr)
                x = r.bipolar() * 2.5f;
            const float y = x - crackleHp[ch];
            crackleHp[ch] = x * 0.6f;
            return y;
        }
        case kNoiseBand:
        default: return bandS[ch].process (bandC, r.bipolar()) * 1.6f;
    }
}

void LayerModule::process (const float* inL, const float* inR, float* outL, float* outR, const float*, int n) noexcept
{
    const int os = osFactor;
    const float osr = (float) sr * (float) os;
    const float invOsr = 1.0f / osr;
    const float ddelay = Oversampler::decimDelayFor (os);
    const float attackInc = 1.0f / (0.0015f * osr);
    const float fadeCoef = std::exp (-1.0f / (0.005f * osr));
    const float tauP = clampf (0.12f * subDecay, 0.008f, 0.060f);
    const float pitchCoef = std::exp (-invOsr / tauP);
    const float g = growlSm.current;
    const float gd = 1.0f + 15.0f * g;
    const float gb = 0.25f * g;
    const float gNorm = 1.0f / std::max (1.0e-3f, fastTanh (gd + gb) - fastTanh (gb));
    const float tgb = fastTanh (gb);
    const float fStartAll = subFreq * std::pow (2.0f, subDrop / 12.0f);

    float noiseOut[2][kChunk];
    for (int i = 0; i < n; ++i)
    {
        const float mono = 0.5f * (inL[i] + inR[i]);
        const float lpPrev = subTrig.filtered();
        if (subTrig.process (mono))
        {
            const float omega = kTwoPi * fStartAll;
            const float lp = subTrig.filtered();
            const float deriv = (lp - lpPrev) * (float) sr / omega;
            float ph = std::atan2 (lp, deriv);
            ph += subTrig.phaseLag (fStartAll);  // the detection filter lags the source
            ph += omega * ddelay / (float) sr;   // compensate the decimator's group delay
            triggerSub (ph, subTrig.velocity());
            pendingVoice = nextVoice ^ 1;
            ++subTrigs;
            lastSubAt = sampleCount;
            if (subLogPtr != nullptr)
                subLogPtr->push_back (sampleCount);
        }
        if (pendingVoice >= 0)
        {
            voices[pendingVoice].amp = subTrig.velocity();
            if (! subTrig.capturing())
                pendingVoice = -1;
        }
        // noise layer: starts immediately on a snare/hat onset; if the kick detector
        // confirms a kick within the decision window, the HF onset was the kick's
        // beater click - the noise voice is cancelled (2 ms fade) and not counted
        if (noiseTrig.process (mono))
        {
            const bool kickJustFired = lastSubAt >= 0 && sampleCount - lastSubAt <= noiseVetoBefore;
            if (! kickJustFired)
            {
                nv.attack = 1.0f;
                nv.kill = false;
                nv.vel = noiseTrig.velocity();
                noisePending = noiseDecisionSamples;
                noisePendingAt = sampleCount;
            }
        }
        if (noiseTrig.capturing() && ! nv.kill)
            nv.vel = std::max (nv.vel, noiseTrig.velocity());
        if (noisePending > 0)
        {
            if (lastSubAt >= noisePendingAt)
            {
                nv.kill = true;
                nv.attack = 0.0f;
                noisePending = 0;
            }
            else if (--noisePending == 0)
            {
                ++noiseTrigs;
                lastNoiseAt = noisePendingAt;
                if (noiseLogPtr != nullptr)
                    noiseLogPtr->push_back (noisePendingAt);
            }
        }

        // --- sub at the oversampled rate ---
        for (int k = 0; k < os; ++k)
        {
            float s = 0.0f;
            for (auto& v : voices)
            {
                if (! v.active)
                    continue;
                v.pitchEnv *= pitchCoef;
                const float f = subFreq + (fStartAll - subFreq) * v.pitchEnv;
                v.phase += (double) (kTwoPi * f * invOsr);
                if (v.phase > 2.0 * kPiD)
                    v.phase -= 2.0 * kPiD;
                float atk = 1.0f;
                if (v.atkPos < 1.0f)
                {
                    atk = 0.5f - 0.5f * std::cos (kPi * v.atkPos);
                    v.atkPos += attackInc;
                }
                v.env *= subDecayCoef;
                if (v.releasing)
                    v.fade *= fadeCoef;
                float ph = (float) v.phase;
                if (ph > kPi)
                    ph -= kTwoPi;
                float sn, cs;
                fastSinCos (ph, sn, cs);
                s += sn * v.env * atk * v.amp * v.fade;
                v.t += invOsr;
                if (v.env < 1.0e-5f || v.fade < 1.0e-4f)
                    v.active = false;
            }
            if (g > 0.0f)
            {
                const float shaped = (fastTanh (gd * s + gb) - tgb) * gNorm;
                s += g * (shaped - s);
            }
            subOs[(size_t) (i * os + k)] = s;
        }

        // --- noise layer ---
        if (nv.kill)
            nv.env = flushTiny (nv.env * noiseKillCoef);
        else if (nv.attack > 0.0f)
        {
            nv.env += noiseAttackInc * std::max (0.05f, nv.vel);
            if (nv.env >= nv.vel)
            {
                nv.env = nv.vel;
                nv.attack = 0.0f;
            }
        }
        else
            nv.env = flushTiny (nv.env * noiseDecayCoef);

        const bool fading = typeFader.active();
        const float ft = fading ? typeFader.next() : 1.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            float x = 0.0f;
            if (nv.env > 1.0e-6f || fading)
            {
                auto shape = [&] (int type) {
                    float y = noiseSample (type, ch);
                    if (type != kNoiseBand)
                    {
                        const float lo = toneLp[ch][type].process (y);
                        y = lo * toneMixLow + (y - lo) * toneMixHigh;
                    }
                    return y;
                };
                x = shape (typeFader.current);
                if (fading)
                    x = shape (typeFader.previous) * (1.0f - ft) + x * ft;
            }
            noiseOut[ch][i] = x * nv.env;
        }
        ++sampleCount;
    }

    decim.decimateMono (subOs.data(), subBase, n);
    for (int i = 0; i < n; ++i)
    {
        const float sub = subBase[i] * subLevelRamp.next();
        const float nl = noiseLevelRamp.next();
        outL[i] = inL[i] + sub + noiseOut[0][i] * nl;
        outR[i] = inR[i] + sub + noiseOut[1][i] * nl;
    }
}

} // namespace ds
