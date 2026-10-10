#include "SmashModule.h"

namespace ds
{
namespace
{
    constexpr float kRef = 0.5f;
    constexpr float kBiasScale = 0.6f;
} // namespace

float SmashModule::curve (int c, float u) noexcept
{
    switch (c)
    {
        case kCurveSmooth: return fastTanh (u);
        case kCurveCrunch:
        {
            const float v = u * (2.0f / 3.0f);
            const float f = std::fabs (v) <= 1.0f ? 1.5f * v - 0.5f * v * v * v : (v > 0.0f ? 1.0f : -1.0f);
            return clampf (1.2f * f, -1.0f, 1.0f);
        }
        case kCurveFold: return std::sin (u);
        case kCurveRectify:
        {
            const float t = fastTanh (u);
            const float a = t * t;
            return t * (1.0f - 0.6f * a) + 1.4f * a * std::fabs (t);
        }
        case kCurveStairs:
        default:
        {
            const float t = fastTanh (u);
            constexpr float n = 4.0f;
            return t - 0.85f * std::sin (kTwoPi * n * t) / (kTwoPi * n);
        }
    }
}

float SmashModule::computeComp (int c, float g, float bias) const noexcept
{
    const float b0 = curve (c, bias);
    const float a = std::fabs (curve (c, g * kRef + bias) - b0);
    const float b = std::fabs (curve (c, -g * kRef + bias) - b0);
    const float m = 0.5f * (a + b);
    return m > 1.0e-5f ? clampf (kRef / m, 0.0f, 50.0f) : 1.0f;
}

void SmashModule::prepare (double sampleRate)
{
    sr = sampleRate;
    os.prepare (kChunk);
    const float chunkRate = (float) sr / (float) kChunk;
    driveSm.setup (chunkRate, 0.02f, 0.0f);
    biasSm.setup (chunkRate, 0.02f, 0.0f);
    ceilSm.setup (chunkRate, 0.02f, 0.0f);
    trimSm.setup (chunkRate, 0.02f, 0.0f);
    for (auto& d : dc)
        d.setup (5.0f, (float) sr);
    configure (1, 0);
    reset();
}

void SmashModule::configure (int osFactor, int)
{
    os.setFactor (osFactor);
    fadeLen = 0.02f * (float) (sr * os.factor());
}

void SmashModule::reset()
{
    os.reset();
    for (auto& d : dc)
        d.reset();
    first = true;
}

void SmashModule::control (const float* p, const ModCtx& ctx) noexcept
{
    const int mode = (int) p[kSmashCurve] * 2 + (int) p[kSmashClipMode];
    driveSm.target = p[kSmashDrive] + 6.0f * ctx.push;
    biasSm.target = p[kSmashBias] * 0.01f * kBiasScale;
    ceilSm.target = p[kSmashCeiling];
    trimSm.target = p[kSmashTrim];
    const int len = kChunk * os.factor();
    if (first)
    {
        fader.snap (mode);
        driveSm.snap (driveSm.target);
        biasSm.snap (biasSm.target);
        ceilSm.snap (ceilSm.target);
        trimSm.snap (trimSm.target);
        const float g = dbToGain (driveSm.target);
        driveRamp.snap (g);
        biasRamp.snap (biasSm.target);
        ceilRamp.snap (dbToGain (ceilSm.target));
        compCur.snap (computeComp (mode / 2, g, biasSm.target));
        compPrev.snap (compCur.end);
        trimRamp.snap (dbToGain (trimSm.target));
    }
    else if (fader.request (mode, fadeLen))
        compPrev.snap (compCur.end);

    const float g = dbToGain (driveSm.step());
    const float b = biasSm.step();
    driveRamp.rampTo (g, len);
    biasRamp.rampTo (b, len);
    ceilRamp.rampTo (dbToGain (ceilSm.step()), len);
    compCur.rampTo (computeComp (fader.current / 2, g, b), len);
    compPrev.rampTo (computeComp (fader.previous / 2, g, b), len);
    trimRamp.rampTo (dbToGain (trimSm.step()));
    first = false;
}

float SmashModule::shapeAndClip (int mode, float x, float g, float bias, float comp, float ceil) const noexcept
{
    const int c = mode / 2;
    float y = (curve (c, g * x + bias) - curve (c, bias)) * comp;
    if ((mode & 1) == 0)
        y = softClipKnee (y, ceil, 0.708f * ceil);
    else
        y = hardClip (y, ceil);
    return y;
}

void SmashModule::process (const float* inL, const float* inR, float* outL, float* outR, const float*, int n) noexcept
{
    os.upsample (inL, inR, n);
    const int N = n * os.factor();
    float* b[2] = { os.buffer (0), os.buffer (1) };
    for (int i = 0; i < N; ++i)
    {
        const float g = driveRamp.next();
        const float bias = biasRamp.next();
        const float ceil = ceilRamp.next();
        const float cc = compCur.next();
        const float cp = compPrev.next();
        if (fader.active())
        {
            const float t = fader.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                const float yc = shapeAndClip (fader.current, b[ch][i], g, bias, cc, ceil);
                const float yp = shapeAndClip (fader.previous, b[ch][i], g, bias, cp, ceil);
                b[ch][i] = yp + (yc - yp) * t;
            }
        }
        else
        {
            for (int ch = 0; ch < 2; ++ch)
                b[ch][i] = shapeAndClip (fader.current, b[ch][i], g, bias, cc, ceil);
        }
    }
    os.downsample (outL, outR, n);
    for (int i = 0; i < n; ++i)
    {
        const float tr = trimRamp.next();
        outL[i] = dc[0].process (outL[i]) * tr;
        outR[i] = dc[1].process (outR[i]) * tr;
    }
}

} // namespace ds
