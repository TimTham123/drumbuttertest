// DrumSpread offline verification harness - engine-level tests.
#include "TestSignals.h"
#include "TestSuites.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>

using namespace ds;

namespace tu
{
namespace
{
    constexpr double kSR = 48000.0;

    std::vector<float> defaults()
    {
        std::vector<float> p (kNumParams);
        setDefaults (p.data());
        return p;
    }

    /** Every module active with audible, non-trivial settings. */
    std::vector<float> allActive()
    {
        std::vector<float> p (kNumParams);
        presetParams ("Golden Spread", p.data());
        for (int m = 0; m < kNumModules; ++m)
            p[(size_t) sharedParam (m, kSfBypass)] = 0.0f;
        p[kWarpPitch] = -3.0f;
        p[kWarpMix] = 60.0f;
        p[kLayerSubGrowl] = 25.0f;
        return p;
    }

    /** Neutral master: no HPF/LPF, flat EQ, clip on (identity below its knee). */
    void neutralMaster (std::vector<float>& p)
    {
        p[kMasterHpf] = 10.0f;
        p[kMasterLpf] = 22000.0f;
        p[kMasterLowGain] = p[kMasterMidGain] = p[kMasterHighGain] = 0.0f;
        p[kMasterAir] = p[kMasterAttack] = p[kMasterSustain] = 0.0f;
        p[kInGain] = p[kOutGain] = 0.0f;
    }

    Stereo delayed (const Stereo& in, int d)
    {
        Stereo o (in.size());
        for (size_t i = (size_t) d; i < in.size(); ++i)
        {
            o.L[i] = in.L[i - (size_t) d];
            o.R[i] = in.R[i - (size_t) d];
        }
        return o;
    }

    double residualDb (const Stereo& a, const Stereo& b)
    {
        const double d = std::max (maxAbsDiff (a.L, b.L), maxAbsDiff (a.R, b.R));
        return d == 0 ? -999.0 : toDb (d);
    }
    std::string resStr (double db) { return db <= -999.0 ? "bit-exact" : fmt ("%.1f dB", db); }

    double clickRatio (const Stereo& out, size_t sw, double sr, double transSec = 0.12)
    {
        auto m2 = [&] (size_t st, size_t len) { return std::max (maxSecondDiff (out.L, st, len), maxSecondDiff (out.R, st, len)); };
        const double before = m2 (sw - (size_t) (0.25 * sr), (size_t) (0.2 * sr));
        const double after = m2 (sw + (size_t) (0.35 * sr), (size_t) (0.2 * sr));
        const double trans = m2 (sw, (size_t) (transSec * sr));
        return trans / std::max (1e-9, std::max (before, after));
    }
} // namespace

//==============================================================================
void testLatency()
{
    std::vector<std::vector<std::string>> table;
    bool allOk = true;
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int os = 0; os < 3; ++os)
            for (int wm = 0; wm < 2; ++wm)
            {
                auto p = defaults();
                p[kOversampling] = (float) os;
                p[kWarpMode] = (float) wm;
                neutralMaster (p);
                for (int m = 0; m < kNumModules; ++m)
                    p[(size_t) sharedParam (m, kSfBypass)] = 1.0f;
                auto e = std::make_unique<Engine>();
                Stereo imp ((size_t) (0.3 * sr));
                imp.L[1000] = imp.R[1000] = 0.5f;
                auto out = renderEngine (*e, sr, imp, p.data(), 256);
                const int rep = e->latencySamples();
                const int expect = Engine::latencyFor (sr, os, wm);
                size_t pk = 0;
                for (size_t i = 0; i < out.size(); ++i)
                    if (std::fabs (out.L[i]) > std::fabs (out.L[pk]))
                        pk = i;
                const int meas = (int) pk - 1000;
                // active modules: timing preserved (small impulse, mild settings)
                auto pa = allActive();
                pa[kWarpPitch] = 0.0f;
                pa[kOversampling] = (float) os;
                pa[kWarpMode] = (float) wm;
                pa[kRoomBypass] = 1.0f; // reverb tail would dominate the peak search
                auto e2 = std::make_unique<Engine>();
                auto out2 = renderEngine (*e2, sr, imp, pa.data(), 256);
                size_t pk2 = 0;
                for (size_t i = 0; i < out2.size(); ++i)
                    if (std::fabs (out2.L[i]) > std::fabs (out2.L[pk2]))
                        pk2 = i;
                const int meas2 = (int) pk2 - 1000;
                const bool ok = rep == expect && meas == rep && std::abs (meas2 - rep) <= (int) (0.0003 * sr) + 2;
                allOk = allOk && ok;
                table.push_back ({ fmt ("%.1f k", sr / 1000), fmt ("%dx", osFactorFromIndex (os)), wm ? "Lo-Fi" : "HQ",
                                   fmt ("%d", rep), fmt ("%.2f ms", rep / sr * 1000), fmt ("%d", meas), fmt ("%d", meas2),
                                   ok ? "ok" : "MISMATCH" });
            }
    addResult ("Latency", "reported == formula == measured (bypassed & active paths)",
               fmt ("%d configurations", (int) table.size()), "exact (active path within 0.3 ms)", allOk);
    addTable ("Latency (samples) by sample rate, oversampling and WARP mode",
              { "SR", "OS", "WARP", "Reported", "ms", "Measured (bypassed)", "Measured (active)", "Status" }, table);
}

//==============================================================================
void testEngineNulls()
{
    auto loop = makeDrumLoop (kSR, 120, 2, 3).audio;
    for (size_t i = 0; i < loop.size(); ++i)
    {
        loop.L[i] *= 0.6f;
        loop.R[i] *= 0.6f;
    }
    struct Case
    {
        const char* name;
        std::function<void (std::vector<float>&)> set;
    };
    const Case cases[] = {
        { "Intensity 0% (all modules active, focus engaged)",
          [] (std::vector<float>& p) {
              p[kIntensity] = 0.0f;
              for (int m = 0; m < kNumModules; ++m)
              {
                  p[(size_t) sharedParam (m, kSfFocusLo)] = 200.0f;
                  p[(size_t) sharedParam (m, kSfFocusHi)] = 6000.0f;
              }
          } },
        { "all module Mix 0%", [] (std::vector<float>& p) {
              for (int m = 0; m < kNumModules; ++m)
                  p[(size_t) sharedParam (m, kSfMix)] = 0.0f;
          } },
        { "all module Bypass on", [] (std::vector<float>& p) {
              for (int m = 0; m < kNumModules; ++m)
                  p[(size_t) sharedParam (m, kSfBypass)] = 1.0f;
          } },
        { "global Dry/Wet 0%", [] (std::vector<float>& p) { p[kDryWet] = 0.0f; } },
        { "global Bypass (host bypass path)", [] (std::vector<float>& p) { p[kBypass] = 1.0f; } },
    };
    for (int os : { 0, 1, 2 })
        for (const auto& c : cases)
        {
            auto p = allActive();
            neutralMaster (p);
            p[kOversampling] = (float) os;
            c.set (p);
            auto e = std::make_unique<Engine>();
            auto out = renderEngine (*e, kSR, loop, p.data(), 512);
            const auto ref = delayed (loop, e->latencySamples());
            const double r = residualDb (out, ref);
            addResult ("Null", fmt ("engine %s, %dx", c.name, osFactorFromIndex (os)), resStr (r),
                       "bit-exact or <= -120 dB", r <= -120.0);
        }
    // mono input == stereo input with R = L
    {
        auto p = allActive();
        Stereo mono = loop;
        mono.R = mono.L;
        auto e1 = std::make_unique<Engine>();
        auto e2 = std::make_unique<Engine>();
        auto a = renderEngine (*e1, kSR, mono, p.data(), 512, {}, true);
        auto b = renderEngine (*e2, kSR, mono, p.data(), 512, {}, false);
        const double r = residualDb (a, b);
        addResult ("I/O", "mono input (nullptr R) == dual-mono stereo input", resStr (r), "bit-exact", r <= -999.0);
    }
}

//==============================================================================
void testClickFree()
{
    const size_t n = (size_t) (2.0 * kSR);
    const size_t sw = (size_t) (1.0 * kSR);
    auto in = multiSine (kSR, { 110.0, 220.0, 330.0 }, 0.15, n);
    struct Case
    {
        std::string name;
        std::function<void (std::vector<float>&)> base, change;
        bool instant = false;
        bool expectClick = false;
    };
    std::vector<Case> cases;
    auto add = [&cases] (std::string nm, std::function<void (std::vector<float>&)> b,
                         std::function<void (std::vector<float>&)> c, bool inst = false, bool expect = false) {
        cases.push_back ({ std::move (nm), std::move (b), std::move (c), inst, expect });
    };
    auto noop = [] (std::vector<float>&) {};
    auto heavyish = [] (std::vector<float>& p) {
        p[kHeatDrive] = 18.0f;
        p[kRoomMix] = 50.0f;
        p[kPressAmount] = 60.0f;
    };
    add ("module reorder (40 ms crossfade)", heavyish, [] (std::vector<float>& p) { p[kChainOrder] = 18.0f; });
    add ("module reorder, crossfade disabled (control)", heavyish, [] (std::vector<float>& p) { p[kChainOrder] = 18.0f; },
         true, true);
    for (int m = 0; m < kNumModules; ++m)
    {
        add (fmt ("%s bypass on", moduleName (m)), heavyish,
             [m] (std::vector<float>& p) { p[(size_t) sharedParam (m, kSfBypass)] = 1.0f; });
        add (fmt ("%s bypass off (re-engage)", moduleName (m)),
             [m, heavyish] (std::vector<float>& p) {
                 heavyish (p);
                 p[(size_t) sharedParam (m, kSfBypass)] = 1.0f;
             },
             [m] (std::vector<float>& p) { p[(size_t) sharedParam (m, kSfBypass)] = 0.0f; });
    }
    add ("HEAT mode Tape -> Fuzz", heavyish, [] (std::vector<float>& p) { p[kHeatMode] = (float) kHeatFuzz; });
    add ("HEAT mode Tube -> Crush", [heavyish] (std::vector<float>& p) { heavyish (p); p[kHeatMode] = 1; },
         [] (std::vector<float>& p) { p[kHeatMode] = (float) kHeatCrush; });
    add ("PRESS type Glue -> FET", heavyish, [] (std::vector<float>& p) { p[kPressType] = (float) kPressFET; });
    add ("PRESS type Opto -> Vari-Mu", [heavyish] (std::vector<float>& p) { heavyish (p); p[kPressType] = 1; },
         [] (std::vector<float>& p) { p[kPressType] = (float) kPressVariMu; });
    add ("ROOM type Room -> Plate", heavyish, [] (std::vector<float>& p) { p[kRoomType] = (float) kRoomPlate; });
    add ("ROOM type Spring -> Vintage", [heavyish] (std::vector<float>& p) { heavyish (p); p[kRoomType] = 2; },
         [] (std::vector<float>& p) { p[kRoomType] = (float) kRoomVintage; });
    add ("SMASH curve Smooth -> Fold", [] (std::vector<float>& p) { p[kSmashDrive] = 12; },
         [] (std::vector<float>& p) { p[kSmashCurve] = (float) kCurveFold; });
    add ("SMASH clip Soft -> Hard", [] (std::vector<float>& p) { p[kSmashDrive] = 12; p[kSmashCeiling] = -6; },
         [] (std::vector<float>& p) { p[kSmashClipMode] = 1.0f; });
    add ("LAYER noise type Pink -> Band", noop, [] (std::vector<float>& p) { p[kLayerNoiseType] = (float) kNoiseBand; });
    add ("WARP mode HQ -> Lo-Fi (latency change)", noop, [] (std::vector<float>& p) { p[kWarpMode] = 1.0f; });
    add ("Oversampling 2x -> 4x (latency change)", heavyish, [] (std::vector<float>& p) { p[kOversampling] = 2.0f; });
    add ("Oversampling 2x -> 1x (latency change)", heavyish, [] (std::vector<float>& p) { p[kOversampling] = 0.0f; });
    add ("global bypass on", heavyish, [] (std::vector<float>& p) { p[kBypass] = 1.0f; });
    add ("Master Clip off", [] (std::vector<float>& p) { p[kOutGain] = 12.0f; }, [] (std::vector<float>& p) { p[kMasterClip] = 0.0f; });
    add ("WARP pitch 0 -> -5 st (PV engage)", [] (std::vector<float>& p) { p[kWarpPitch] = 0.0f; },
         [] (std::vector<float>& p) { p[kWarpPitch] = -5.0f; });
    add ("preset switch Golden Spread -> Industrial", noop,
         [] (std::vector<float>& p) { presetParams ("Industrial", p.data()); });

    std::vector<std::vector<std::string>> table;
    int fails = 0;
    for (const auto& c : cases)
    {
        auto p = allActive();
        c.base (p);
        auto changed = p;
        c.change (changed);
        auto e = std::make_unique<Engine>();
        e->setInstantReorderForTest (c.instant);
        auto out = renderEngine (*e, kSR, in, p.data(), 256, [&] (int64_t pos, float* pp) {
            if (pos >= (int64_t) sw)
                std::copy (changed.begin(), changed.end(), pp);
        });
        const double r = clickRatio (out, sw, kSR);
        const bool finite = allFinite (out.L) && allFinite (out.R);
        const bool pass = finite && (c.expectClick ? r > 1.5 : r < 1.5);
        if (! pass)
            ++fails;
        table.push_back ({ c.name, fmt ("%.2f", r), c.expectClick ? "> 1.5 (must detect)" : "< 1.5", pass ? "pass" : "FAIL" });
    }
    addResult ("Click-free", "reorder / bypass / mode / OS / preset switches", fmt ("%d/%d cases pass", (int) cases.size() - fails, (int) cases.size()),
               "2nd-difference peak during switch < 1.5 x steady state", fails == 0);
    addTable ("Click test: max |2nd difference| in the 120 ms after the switch, relative to steady state before/after "
              "(3-sine input 110/220/330 Hz, all modules active)",
              { "Switch", "Ratio", "Criterion", "Result" }, table);

    // ---- zipper: block-rate automation sweeps of continuous parameters ----
    {
        auto sig = sine (kSR, 400.0, 0.25, (size_t) (2.0 * kSR));
        struct Sweep
        {
            const char* name;
            int param;
            float from, to;
            std::function<void (std::vector<float>&)> setup;
        };
        auto roomOnly = [] (std::vector<float>& p) {
            for (int m = 0; m < kNumModules; ++m)
                p[(size_t) sharedParam (m, kSfBypass)] = 1.0f;
            p[kRoomBypass] = 0.0f;
        };
        auto none = [] (std::vector<float>& p) {
            for (int m = 0; m < kNumModules; ++m)
                p[(size_t) sharedParam (m, kSfBypass)] = 1.0f;
        };
        const Sweep sweeps[] = {
            { "Output gain -12 -> +6 dB", kOutGain, -12, 6, none },
            { "Input gain -12 -> +6 dB", kInGain, -12, 6, none },
            { "Dry/Wet 0 -> 100% (Room on)", kDryWet, 0, 100, roomOnly },
            { "ROOM mix 0 -> 100%", kRoomMix, 0, 100, roomOnly },
            { "ROOM focus Lo 20 -> 2000 Hz", kRoomFocusLo, 20, 2000, roomOnly },
            { "Master low shelf -12 -> +12 dB", kMasterLowGain, -12, 12, none },
            { "Master mid bell -12 -> +12 dB", kMasterMidGain, -12, 12, none },
            { "Master LPF 22k -> 1k", kMasterLpf, 22000, 1000, none },
            { "Intensity 0 -> 200% (Room + EQ)", kIntensity, 0, 200, roomOnly },
        };
        std::vector<std::vector<std::string>> zt;
        double worst = -200;
        for (const auto& s : sweeps)
        {
            auto p = defaults();
            neutralMaster (p);
            p[kMasterClip] = 0.0f;
            s.setup (p);
            if (s.param == kIntensity)
                p[kMasterMidGain] = 6.0f;
            auto e = std::make_unique<Engine>();
            auto out = renderEngine (*e, kSR, sig, p.data(), 512, [&] (int64_t pos, float* pp) {
                const double t = clampf ((float) ((pos - 0.5 * kSR) / kSR), 0.0f, 1.0f);
                pp[s.param] = s.from + (s.to - s.from) * (float) t;
            });
            // energy above 6 kHz (a pure 400 Hz input through linear processing has none)
            SvfCoeffs hp;
            hp.highpass (6000.0f, 0.7071f, (float) kSR);
            SvfState s1, s2;
            Buf h (out.size());
            for (size_t i = 0; i < out.size(); ++i)
                h[i] = s2.process (hp, s1.process (hp, out.L[i]));
            const double ratio = toDb (rms (h, (size_t) (0.5 * kSR), (size_t) kSR) / rms (out.L, (size_t) (0.5 * kSR), (size_t) kSR));
            worst = std::max (worst, ratio);
            zt.push_back ({ s.name, fmt ("%.1f dB", ratio) });
        }
        addResult ("Click-free", "zipper noise during block-rate automation sweeps", fmt ("worst %.1f dB re signal", worst),
                   "< -70 dB (energy > 6 kHz, 400 Hz input)", worst < -70.0);
        addTable ("Zipper test (1 s linear sweeps updated once per 512-sample block; HF residue re signal)",
                  { "Sweep", "Energy > 6 kHz" }, zt);
    }
}

//==============================================================================
void testExtremes()
{
    std::vector<std::vector<std::string>> table;
    bool allOk = true;
    Rng rng (2024);
    const char* sigNames[] = { "drum loop", "full-scale noise", "50 Hz square 0 dBFS", "impulses", "DC 0.5 + noise" };
    for (int run = 0; run < 10; ++run)
    {
        auto p = defaults();
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& s = paramSpec (i);
            if (i == kBypass || i == kChainOrder)
                continue;
            if (s.type == PType::Float)
                p[(size_t) i] = rng.unipolar() < 0.5f ? s.minV : s.maxV;
            else
                p[(size_t) i] = std::floor (rng.unipolar() * (s.maxV + 1.0f));
        }
        for (int m = 0; m < kNumModules; ++m)
        {
            p[(size_t) sharedParam (m, kSfBypass)] = 0.0f;
            p[(size_t) sharedParam (m, kSfMix)] = 100.0f;
        }
        p[kIntensity] = 200.0f;
        p[kInGain] = run < 5 ? 24.0f : 0.0f;
        p[kOutGain] = 24.0f;
        p[kDryWet] = 100.0f;
        p[kMasterClip] = (run % 2 == 0) ? 1.0f : 0.0f;
        p[kChainOrder] = (float) (run * 5 % kNumChainOrders);
        const double sr = run == 9 ? 192000.0 : (run == 8 ? 44100.0 : kSR);
        const size_t sigLen = (size_t) (2.0 * sr), tail = (size_t) (2.0 * sr);
        Stereo in (sigLen + tail);
        const int sigType = run % 5;
        Rng nr (run + 1u);
        auto loop = makeDrumLoop (sr, 128, 2, 7u + (uint32_t) run);
        for (size_t i = 0; i < sigLen; ++i)
        {
            float v = 0;
            switch (sigType)
            {
                case 0: v = i < loop.audio.size() ? loop.audio.L[i] : 0.0f; break;
                case 1: v = nr.bipolar(); break;
                case 2: v = std::sin (2 * 3.14159265 * 50.0 * (double) i / sr) >= 0 ? 1.0f : -1.0f; break;
                case 3: v = (i % (size_t) (0.25 * sr)) == 0 ? 1.0f : 0.0f; break;
                default: v = 0.5f + 0.2f * nr.bipolar(); break;
            }
            in.L[i] = v;
            in.R[i] = sigType == 1 ? nr.bipolar() : v;
        }
        auto e = std::make_unique<Engine>();
        // render with timing of the signal part vs the silent tail
        std::vector<float> pp = p;
        e->setParameters (pp.data());
        e->prepare (sr, 256);
        e->setParameters (pp.data());
        e->reset();
        Stereo out (in.size());
        double tSig = 0, tTail = 0;
        for (size_t pos = 0; pos < in.size(); pos += 256)
        {
            const int m = (int) std::min<size_t> (256, in.size() - pos);
            const auto t0 = std::chrono::steady_clock::now();
            e->process (in.L.data() + pos, in.R.data() + pos, out.L.data() + pos, out.R.data() + pos, m);
            const double dt = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            (pos < sigLen ? tSig : tTail) += dt;
        }
        const bool finite = allFinite (out.L) && allFinite (out.R);
        const double peak = std::max (peakAbs (out.L), peakAbs (out.R));
        const double ceil = std::pow (10.0, p[kMasterCeiling] / 20.0);
        const bool clipOk = p[kMasterClip] < 0.5f || peak <= ceil * 1.0001;
        const double dc = std::fabs (mean (out.L, sigLen / 2, sigLen / 2));
        const bool dcOk = sigType == 4 || dc < 0.02 * std::max (1.0, peak); // DC input passes the (dry) path by design
        const size_t sub = countSubnormals (out.L) + countSubnormals (out.R);
        const double slow = (tTail / tail) / std::max (1e-12, tSig / sigLen);
        const bool ok = finite && clipOk && dcOk && sub == 0 && slow < 3.0;
        allOk = allOk && ok;
        table.push_back ({ fmt ("%d", run), fmt ("%.1f k", sr / 1000), sigNames[sigType],
                           p[kMasterClip] > 0.5f ? "on" : "off", finite ? "yes" : "NO", fmt ("%.3f", peak),
                           fmt ("%.1f dBFS", toDb (dc)), fmt ("%zu", sub), fmt ("%.2f", slow), ok ? "pass" : "FAIL" });
    }
    addResult ("Robustness", "random extreme settings (intensity 200%, +24 dB in/out, all drives max/min)",
               fmt ("%d runs", (int) table.size()), "finite, clip <= ceiling, DC < -34 dBFS, no subnormals, tail cost < 3x",
               allOk);
    addTable ("Extreme-setting robustness (2 s signal + 2 s silence)",
              { "Run", "SR", "Input", "Master clip", "Finite", "Peak", "DC (mean)", "Subnormals", "Tail/signal CPU", "Result" },
              table);
}

//==============================================================================
void testSampleRateIndependence()
{
    const char* presets[] = { "Golden Spread", "Bus Glue", "Plate Shine", "Dusty Tape", "Front Row" };
    std::vector<std::vector<std::string>> table;
    bool allOk = true;
    for (const char* pr : presets)
    {
        std::vector<double> centres, ref, refIn;
        double refRms = 0, refInRms = 0;
        std::string row = pr;
        std::vector<std::string> cells { pr };
        for (double sr : { 48000.0, 44100.0, 96000.0, 192000.0 })
        {
            if (sr == 192000.0 && std::string (pr) != "Golden Spread")
            {
                cells.push_back ("-");
                continue;
            }
            auto loop = makeDrumLoop (sr, 120, 4, 77);
            std::vector<float> p (kNumParams);
            presetParams (pr, p.data());
            auto e = std::make_unique<Engine>();
            auto out = renderEngine (*e, sr, loop.audio, p.data(), 512);
            const size_t lat = (size_t) e->latencySamples();
            Buf o (out.L.begin() + (long) lat, out.L.end());
            Buf in (loop.audio.L.begin(), loop.audio.L.end() - (long) lat);
            auto lv = thirdOctaveLevels (o, sr, 50.0, 12500.0, &centres);
            auto lin = thirdOctaveLevels (in, sr, 50.0, 12500.0);
            const double r = toDb (rms (o)), rin = toDb (rms (in));
            if (sr == 48000.0)
            {
                ref = lv;
                refIn = lin;
                refRms = r;
                refInRms = rin;
                cells.push_back ("reference");
                continue;
            }
            double worst = 0;
            for (size_t b = 0; b < lv.size(); ++b)
                worst = std::max (worst, std::fabs ((lv[b] - lin[b]) - (ref[b] - refIn[b])));
            const double dr = (r - rin) - (refRms - refInRms);
            const bool ok = worst <= 2.0 && std::fabs (dr) <= 0.75;
            allOk = allOk && ok;
            cells.push_back (fmt ("%+.2f dB level, %.2f dB worst band%s", dr, worst, ok ? "" : " (FAIL)"));
        }
        table.push_back (cells);
    }
    addResult ("SR independence", "44.1/96/192 kHz vs 48 kHz (gain change and 1/3-oct spectra, 50 Hz-12.5 kHz)",
               fmt ("%d presets", (int) table.size()), "level <= 0.75 dB, every band <= 2 dB", allOk);
    addTable ("Sample-rate independence (processing gain difference vs 48 kHz; input re-synthesised at each rate)",
              { "Preset", "48 kHz", "44.1 kHz", "96 kHz", "192 kHz" }, table);
}

//==============================================================================
void testBlockSizeIndependence()
{
    auto loop = makeDrumLoop (kSR, 120, 2, 5).audio;
    auto p = allActive();
    auto eRef = std::make_unique<Engine>();
    auto ref = renderEngine (*eRef, kSR, loop, p.data(), 512);
    std::string res;
    bool all = true;
    for (int bs : { 1, 3, 16, 17, 64, 333, 1024, 4096, -1 })
    {
        auto e = std::make_unique<Engine>();
        Stereo out;
        if (bs > 0)
            out = renderEngine (*e, kSR, loop, p.data(), bs);
        else
        {
            // random block sizes 1..1000
            std::vector<float> pp = p;
            e->setParameters (pp.data());
            e->prepare (kSR, 1000);
            e->setParameters (pp.data());
            e->reset();
            out = Stereo (loop.size());
            Rng r (99);
            for (size_t pos = 0; pos < loop.size();)
            {
                const int m = (int) std::min<size_t> (1 + (r.nextU() % 1000), loop.size() - pos);
                e->process (loop.L.data() + pos, loop.R.data() + pos, out.L.data() + pos, out.R.data() + pos, m);
                pos += (size_t) m;
            }
        }
        const double d = residualDb (out, ref);
        all = all && d <= -999.0;
        res += fmt ("%s:%s ", bs > 0 ? fmt ("%d", bs).c_str() : "random", d <= -999.0 ? "exact" : fmt ("%.0fdB", d).c_str());
    }
    addResult ("Block size", "block sizes 1,3,16,17,64,333,1024,4096,random vs 512", res, "bit-identical", all);
}

//==============================================================================
void testRealtimeAllocations()
{
    auto loop = makeDrumLoop (kSR, 120, 4, 8).audio;
    auto p = allActive();
    auto e = std::make_unique<Engine>();
    e->setParameters (p.data());
    e->prepare (kSR, 512);
    e->setParameters (p.data());
    e->reset();
    const auto& presets = factoryPresets();
    Stereo out (loop.size());
    uint64_t allocs = 0;
    int events = 0;
    std::vector<float> cur = p;
    for (size_t pos = 0, blk = 0; pos < loop.size(); pos += 512, ++blk)
    {
        // automation outside the counted region (it allocates nothing either, but is not audio-thread code)
        if (blk % 20 == 0)
        {
            applyPreset (presets[(blk / 20) % presets.size()], cur.data());
            cur[kWarpPitch] = (float) ((blk / 20) % 5) - 2.0f;
            ++events;
        }
        if (blk % 13 == 0)
        {
            cur[kChainOrder] = (float) ((blk / 13) % kNumChainOrders);
            ++events;
        }
        if (blk % 31 == 0)
        {
            cur[kOversampling] = (float) ((blk / 31) % 3);
            cur[kWarpMode] = (float) ((blk / 31) % 2);
            ++events;
        }
        if (blk % 7 == 0)
        {
            cur[(size_t) sharedParam ((int) (blk / 7) % kNumModules, kSfBypass)] = (float) ((blk / 7) % 2);
            cur[kHeatMode] = (float) ((blk / 7) % kNumHeatModes);
            cur[kPressType] = (float) ((blk / 7) % kNumPressTypes);
            cur[kRoomType] = (float) ((blk / 7) % kNumRoomTypes);
            ++events;
        }
        const int m = (int) std::min<size_t> (512, loop.size() - pos);
        setAllocCounting (true);
        const uint64_t before = allocCount();
        e->setParameters (cur.data());
        e->process (loop.L.data() + pos, loop.R.data() + pos, out.L.data() + pos, out.R.data() + pos, m);
        allocs += allocCount() - before;
        setAllocCounting (false);
    }
    addResult ("Real-time", "heap allocations inside setParameters()+process()",
               fmt ("%llu allocations over %d automation events (presets, reorders, OS, modes, bypass)",
                    (unsigned long long) allocs, events),
               "0", allocs == 0);
}

//==============================================================================
void testPerformance (bool gate)
{
    std::vector<std::vector<std::string>> table;
    auto loop = makeDrumLoop (kSR, 120, 4, 1).audio;
    struct Cfg
    {
        const char* name;
        int os;
        int warpMode;
        float warpPitch;
    };
    const Cfg cfgs[] = {
        { "all modules active, 2x, WARP HQ -3 st (target config)", 1, 0, -3.0f },
        { "all modules active, 2x, WARP neutral", 1, 0, 0.0f },
        { "all modules active, 2x, WARP Lo-Fi -3 st", 1, 1, -3.0f },
        { "all modules active, 1x, WARP HQ -3 st", 0, 0, -3.0f },
        { "all modules active, 4x, WARP HQ -3 st", 2, 0, -3.0f },
    };
    double target = 0;
    for (const auto& c : cfgs)
    {
        auto p = allActive();
        p[kOversampling] = (float) c.os;
        p[kWarpMode] = (float) c.warpMode;
        p[kWarpPitch] = c.warpPitch;
        auto e = std::make_unique<Engine>();
        e->setParameters (p.data());
        e->prepare (kSR, 512);
        e->setParameters (p.data());
        e->reset();
        Stereo out (loop.size());
        const int reps = 5;
        double best = 1e9;
        for (int r = 0; r < reps; ++r)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (size_t pos = 0; pos < loop.size(); pos += 512)
            {
                const int m = (int) std::min<size_t> (512, loop.size() - pos);
                e->process (loop.L.data() + pos, loop.R.data() + pos, out.L.data() + pos, out.R.data() + pos, m);
            }
            best = std::min (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        }
        const double pct = 100.0 * best / ((double) loop.size() / kSR);
        if (&c == &cfgs[0])
            target = pct;
        table.push_back ({ c.name, fmt ("%.2f%%", pct) });
    }
    if (gate)
        addResult ("Performance", "CPU, 48 kHz stereo, 2x OS, all modules active (WARP shifting)",
                   fmt ("%.2f%% of one core", target), "< 8%", target < 8.0);
    else
        addInfo ("Performance", "CPU, 48 kHz stereo, 2x OS, all modules active (WARP shifting)",
                 fmt ("%.2f%% of one core (not gated on shared CI runners)", target));
    addTable ("CPU load (% of one core of the build machine, 48 kHz stereo, 512-sample blocks, best of 5, measured "
              "first in the run)",
              { "Configuration", "CPU" }, table);
}

//==============================================================================
void testPresets (bool writeRenders)
{
    const auto& presets = factoryPresets();
    auto loop = makeDrumLoop (kSR, 120, 4, 2024);
    if (writeRenders)
        writeWav (outPath ("00_dry_drum_loop.wav"), loop.audio, kSR, 16);
    std::vector<std::vector<std::string>> table;
    int unknown = 0, bad = 0;
    std::vector<std::string> seenCat;
    std::map<std::string, int> perCat;
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto& pr = presets[i];
        std::vector<float> p (kNumParams);
        unknown += applyPreset (pr, p.data());
        ++perCat[pr.category];
        auto e = std::make_unique<Engine>();
        auto out = renderEngine (*e, kSR, loop.audio, p.data(), 512);
        const bool finite = allFinite (out.L) && allFinite (out.R);
        const double peak = std::max (peakAbs (out.L), peakAbs (out.R));
        const double ceil = std::pow (10.0, p[kMasterCeiling] / 20.0);
        const double dRms = toDb (rms (out.L) / rms (loop.audio.L));
        const double crestIn = toDb (peakAbs (loop.audio.L) / rms (loop.audio.L));
        const double crestOut = toDb (peakAbs (out.L) / rms (out.L));
        const bool ok = finite && peak <= ceil * 1.0001 && dRms > -12.0 && dRms < 12.0;
        if (! ok)
            ++bad;
        table.push_back ({ pr.category, pr.name, fmt ("%+.1f dB", dRms), fmt ("%.1f dB", toDb (peak)),
                           fmt ("%.1f -> %.1f dB", crestIn, crestOut), ok ? "ok" : "CHECK" });
        const bool firstOfCat = std::find (seenCat.begin(), seenCat.end(), pr.category) == seenCat.end();
        if (firstOfCat)
            seenCat.push_back (pr.category);
        if (writeRenders && (firstOfCat || (int) i == defaultPresetIndex()))
        {
            std::string fn = pr.category + "_" + pr.name;
            for (auto& ch : fn)
                if (ch == ' ' || ch == '/' || ch == '-')
                    ch = '_';
            writeWav (outPath (fmt ("%02d_%s.wav", (int) seenCat.size(), fn.c_str())), out, kSR, 16);
        }
    }
    std::string cats;
    for (const auto& kv : perCat)
        cats += fmt ("%s %d, ", kv.first.c_str(), kv.second);
    addResult ("Presets", "factory preset count / categories", fmt ("%d presets (%s)", (int) presets.size(), cats.c_str()),
               ">= 40 in 7 categories", presets.size() >= 40 && perCat.size() == 7);
    addResult ("Presets", "all presets reference valid parameter ids", fmt ("%d unknown ids", unknown), "0", unknown == 0);
    addResult ("Presets", "all presets render finite, peak <= ceiling, level change within +-12 dB",
               fmt ("%d/%d ok", (int) presets.size() - bad, (int) presets.size()), "all", bad == 0);
    addTable ("Factory presets on the synthesized drum loop (8 s, 120 BPM)",
              { "Category", "Preset", "RMS change", "Peak", "Crest in -> out", "Status" }, table);
}

} // namespace tu
