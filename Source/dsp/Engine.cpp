#include "Engine.h"

namespace ds
{
Engine::Engine() { setDefaults (params); }

int Engine::latencyFor (double sampleRate, int osIndex, int warpMode)
{
    return 3 * Oversampler::latencyFor (osFactorFromIndex (osIndex)) + WarpModule::latencyFor (sampleRate, warpMode);
}

int Engine::latencyForParams (const float* p) const
{
    return latencyFor (sr, (int) p[kOversampling], (int) p[kWarpMode]);
}

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = maxBlockSize;
    (void) paramSpec (0); // force static table initialisation off the audio thread
    (void) Oversampler::stageA();
    (void) Oversampler::stageB();

    layer.prepare (sr, kModLayer);
    for (auto& b : banks)
    {
        b.warp.prepare (sr, kModWarp);
        b.room.prepare (sr, kModRoom);
        b.press.prepare (sr, kModPress);
        b.heat.prepare (sr, kModHeat);
    }
    smash.prepare (sr, kModSmash);
    master.prepare (sr);

    const int maxLat = 3 * Oversampler::latencyFor (kMaxOS) + WarpModule::maxLatency (sr);
    for (auto& d : dryDelay)
        d.prepare (maxLat + 1);

    const float cr = (float) sr / (float) kChunk;
    inGainSm.setup (cr, 0.02f, 0.0f);
    outGainSm.setup (cr, 0.02f, 0.0f);
    dryWetSm.setup (cr, 0.02f, 1.0f);
    ceilSm.setup (cr, 0.02f, -0.3f);
    cfgStep = (float) kChunk / (0.015f * (float) sr);
    toggleStep = (float) kChunk / (0.02f * (float) sr);
    xfInc = 1.0f / (0.04f * (float) sr);
    meterDecay = std::pow (10.0f, -1.0f / (float) sr); // 20 dB/s fall

    applyConfig (osFactorFromIndex ((int) params[kOversampling]), (int) params[kWarpMode]);
    reset();
}

void Engine::applyConfig (int os, int warpMode)
{
    activeOs = os;
    activeWarpMode = warpMode;
    layer.configure (os, warpMode);
    for (auto& b : banks)
    {
        b.warp.configure (os, warpMode);
        b.room.configure (os, warpMode);
        b.press.configure (os, warpMode);
        b.heat.configure (os, warpMode);
    }
    smash.configure (os, warpMode);
    if (xfading)
    {
        xfading = false;
        order = nextOrder;
    }
    activeLatency = computeChainLatency();
    for (auto& d : dryDelay)
        d.setDelay (activeLatency); // history kept: only the read position changes
    tele.latency.store (activeLatency);
}

int Engine::computeChainLatency() const noexcept
{
    const Bank& b = banks[active];
    return layer.latency() + b.warp.latency() + b.room.latency() + b.press.latency() + b.heat.latency()
           + smash.latency();
}

void Engine::reset()
{
    layer.reset();
    for (auto& b : banks)
    {
        b.warp.reset();
        b.room.reset();
        b.press.reset();
        b.heat.reset();
    }
    smash.reset();
    master.reset();
    for (auto& d : dryDelay)
        d.reset();
    xfading = false;
    order = nextOrder = chainPermutation ((int) params[kChainOrder]);
    cfgState = CfgIdle;
    cfgGain = 1.0f;
    sampleCounter = 0;
    firstTick = true;
    meterIn[0] = meterIn[1] = meterOut[0] = meterOut[1] = 0.0f;
}

void Engine::setParameters (const float* values) noexcept
{
    for (int i = 0; i < kNumParams; ++i)
        params[i] = sanitize (i, values[i]);
}

void Engine::controlBank (Bank& b, float blend, const ModCtx& ctx) noexcept
{
    b.warp.control (params, blend, ctx);
    b.room.control (params, blend, ctx);
    b.press.control (params, blend, ctx);
    b.heat.control (params, blend, ctx);
}

void Engine::controlTick() noexcept
{
    // ---- latency-changing configuration (OS factor, WARP mode): fade out/in ----
    const int wantOs = osFactorFromIndex ((int) params[kOversampling]);
    const int wantWm = (int) params[kWarpMode];
    const bool cfgDiffers = wantOs != activeOs || wantWm != activeWarpMode;
    if (firstTick)
    {
        if (cfgDiffers)
            applyConfig (wantOs, wantWm);
        cfgGain = 1.0f;
        cfgRamp.snap (1.0f);
        cfgState = CfgIdle;
    }
    else
    {
        if (cfgState == CfgIdle && cfgDiffers)
            cfgState = CfgFadingOut;
        if (cfgState == CfgFadingOut)
        {
            if (cfgRamp.end <= 0.0f)
            {
                applyConfig (wantOs, wantWm);
                cfgState = CfgHolding; // stay silent while restarted filters settle
                cfgHold = (int) (0.003 * sr);
            }
            else
                cfgGain = std::max (0.0f, cfgGain - cfgStep);
        }
        else if (cfgState == CfgHolding)
        {
            cfgHold -= kChunk;
            if (cfgHold <= 0)
                cfgState = CfgFadingIn;
        }
        else if (cfgState == CfgFadingIn)
        {
            if (cfgDiffers)
                cfgState = CfgFadingOut;
            else
            {
                cfgGain = std::min (1.0f, cfgGain + cfgStep);
                if (cfgGain >= 1.0f)
                    cfgState = CfgIdle;
            }
        }
        cfgRamp.rampTo (cfgGain);
    }

    // ---- intensity ----
    const float k = clampf (params[kIntensity] * 0.01f, 0.0f, 2.0f);
    const float blend = std::min (k, 1.0f);
    ModCtx ctx;
    ctx.push = std::max (0.0f, k - 1.0f);
    ctx.osFactor = activeOs;

    // ---- module order (true crossfade via bank cloning) ----
    const auto& desired = chainPermutation ((int) params[kChainOrder]);
    if (! xfading && desired != order)
    {
        if (instantReorder || firstTick)
            order = nextOrder = desired;
        else
        {
            banks[1 - active] = banks[active]; // pre-sized vectors: no allocation
            nextOrder = desired;
            xfading = true;
            xfPos = 0.0f;
            // Each cloned slot's latency-compensation delays still hold the input it saw
            // in its old position; run the new-order bank until those are flushed
            // (sum of the reorderable slots' latencies) before fading it in.
            const Bank& b = banks[active];
            xfWarm = b.warp.latency() + b.room.latency() + b.press.latency() + b.heat.latency() + kChunk;
        }
    }

    layer.control (params, blend, ctx);
    controlBank (banks[active], blend, ctx);
    if (xfading)
        controlBank (banks[1 - active], blend, ctx);
    smash.control (params, blend, ctx);
    master.control (params, k);

    // ---- global gains / toggles ----
    inGainSm.target = params[kInGain];
    outGainSm.target = params[kOutGain];
    dryWetSm.target = params[kDryWet] * 0.01f;
    ceilSm.target = params[kMasterCeiling];
    const float clipT = params[kMasterClip] > 0.5f ? 1.0f : 0.0f;
    const float bypT = params[kBypass] > 0.5f ? 1.0f : 0.0f;
    if (firstTick)
    {
        inGainSm.snap (inGainSm.target);
        outGainSm.snap (outGainSm.target);
        dryWetSm.snap (dryWetSm.target);
        ceilSm.snap (ceilSm.target);
        clipMix = clipT;
        bypassMix = bypT;
        inRamp.snap (dbToGain (inGainSm.target));
        outRamp.snap (dbToGain (outGainSm.target));
        dwRamp.snap (dryWetSm.target);
        ceilRamp.snap (dbToGain (ceilSm.target));
        clipRamp.snap (clipMix);
        bypassRamp.snap (bypassMix);
    }
    clipMix = clipT > clipMix ? std::min (clipT, clipMix + toggleStep) : std::max (clipT, clipMix - toggleStep);
    bypassMix = bypT > bypassMix ? std::min (bypT, bypassMix + toggleStep) : std::max (bypT, bypassMix - toggleStep);
    inRamp.rampTo (dbToGain (inGainSm.step()));
    outRamp.rampTo (dbToGain (outGainSm.step()));
    dwRamp.rampTo (dryWetSm.step());
    ceilRamp.rampTo (dbToGain (ceilSm.step()));
    clipRamp.rampTo (clipMix);
    bypassRamp.rampTo (bypassMix);
    firstTick = false;
}

void Engine::processBank (Bank& b, const std::array<int, 4>& ord, float* L, float* R, int n) noexcept
{
    for (int s : ord)
    {
        switch (s)
        {
            case kSlotWarp: b.warp.process (L, R, n); break;
            case kSlotRoom: b.room.process (L, R, n); break;
            case kSlotPress: b.press.process (L, R, n); break;
            case kSlotHeat:
            default: b.heat.process (L, R, n); break;
        }
    }
}

void Engine::processSegment (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    float dL[kChunk], dR[kChunk];
    for (int i = 0; i < n; ++i)
    {
        dL[i] = inL[i];
        dR[i] = inR != nullptr ? inR[i] : inL[i];
    }
    for (int i = 0; i < n; ++i)
    {
        const float g = inRamp.next();
        bufL[i] = dL[i] * g;
        bufR[i] = dR[i] * g;
        meterIn[0] = std::max (std::fabs (bufL[i]), meterIn[0] * meterDecay);
        meterIn[1] = std::max (std::fabs (bufR[i]), meterIn[1] * meterDecay);
    }

    layer.process (bufL, bufR, n);

    if (xfading)
    {
        std::memcpy (xfL, bufL, sizeof (float) * (size_t) n);
        std::memcpy (xfR, bufR, sizeof (float) * (size_t) n);
        processBank (banks[active], order, bufL, bufR, n);
        processBank (banks[1 - active], nextOrder, xfL, xfR, n);
        for (int i = 0; i < n; ++i)
        {
            if (xfWarm > 0)
                --xfWarm;
            else
                xfPos = std::min (1.0f, xfPos + xfInc);
            const float t = raisedCos (xfPos);
            bufL[i] += (xfL[i] - bufL[i]) * t;
            bufR[i] += (xfR[i] - bufR[i]) * t;
        }
        if (xfPos >= 1.0f)
        {
            active = 1 - active;
            order = nextOrder;
            xfading = false;
        }
    }
    else
        processBank (banks[active], order, bufL, bufR, n);

    smash.process (bufL, bufR, n);
    master.process (bufL, bufR, n);

    for (int i = 0; i < n; ++i)
    {
        const float yl = dryDelay[0].process (dL[i]);
        const float yr = dryDelay[1].process (dR[i]);
        const float dw = dwRamp.next();
        float oL = yl + dw * (bufL[i] - yl);
        float oR = yr + dw * (bufR[i] - yr);
        const float og = outRamp.next();
        oL *= og;
        oR *= og;
        const float c = ceilRamp.next();
        const float cm = clipRamp.next();
        if (cm > 0.0f)
        {
            oL += cm * (softClipKnee (oL, c, 0.7f * c) - oL);
            oR += cm * (softClipKnee (oR, c, 0.7f * c) - oR);
        }
        const float b = bypassRamp.next();
        if (b >= 1.0f)
        {
            oL = yl; // fully bypassed: exact latency-compensated dry signal
            oR = yr;
        }
        else
        {
            oL += b * (yl - oL);
            oR += b * (yr - oR);
        }
        const float cg = cfgRamp.next();
        oL *= cg;
        oR *= cg;
        outL[i] = oL;
        outR[i] = oR;
        meterOut[0] = std::max (std::fabs (oL), meterOut[0] * meterDecay);
        meterOut[1] = std::max (std::fabs (oR), meterOut[1] * meterDecay);
        tele.scope.push (0.5f * (oL + oR));
    }
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    DenormalGuard guard;
    int done = 0;
    while (done < n)
    {
        const int posInChunk = (int) (sampleCounter % kChunk);
        if (posInChunk == 0)
            controlTick();
        const int m = std::min (n - done, kChunk - posInChunk);
        processSegment (inL + done, inR != nullptr ? inR + done : nullptr, outL + done, outR + done, m);
        done += m;
        sampleCounter += m;
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        tele.inLevel[ch].store (meterIn[ch], std::memory_order_relaxed);
        tele.outLevel[ch].store (meterOut[ch], std::memory_order_relaxed);
    }
    const float gr = banks[active].press.mod.takeGrMeter();
    const float prevGr = tele.pressGrDb.load (std::memory_order_relaxed);
    tele.pressGrDb.store (std::max (gr, prevGr * 0.9f), std::memory_order_relaxed);
    tele.subTriggers.store (layer.mod.subTriggerCount(), std::memory_order_relaxed);
    tele.noiseTriggers.store (layer.mod.noiseTriggerCount(), std::memory_order_relaxed);
    tele.chainOrder.store (chainIndexOf (order), std::memory_order_relaxed);
}

} // namespace ds
