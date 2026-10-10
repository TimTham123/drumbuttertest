// DrumSpread offline verification harness - module-level tests.
#include "TestSignals.h"
#include "TestSuites.h"

#include <algorithm>
#include <complex>
#include <memory>

using namespace ds;

namespace tu
{
namespace
{
    constexpr double kSR = 48000.0;

    template <class Mod>
    Stereo runSlot (ModuleSlot<Mod>& slot, int moduleId, double sr, int os, int wm, const Stereo& in,
                    std::vector<float>& p, const Automation& au = {})
    {
        slot.prepare (sr, moduleId);
        slot.configure (os, wm);
        Stereo out = in;
        ModCtx ctx;
        ctx.osFactor = os;
        for (size_t pos = 0; pos < in.size(); pos += kChunk)
        {
            const int n = (int) std::min<size_t> (kChunk, in.size() - pos);
            if (au)
                au ((int64_t) pos, p.data());
            slot.control (p.data(), 1.0f, ctx);
            slot.process (out.L.data() + pos, out.R.data() + pos, n);
        }
        return out;
    }

    /** Runs a module directly (no slot) and returns its full output. */
    template <class Mod>
    Stereo runDirect (Mod& m, double sr, int os, int wm, const Stereo& in, const std::vector<float>& p)
    {
        m.prepare (sr);
        m.configure (os, wm);
        Stereo out (in.size());
        ModCtx ctx;
        ctx.osFactor = os;
        float w[kChunk];
        std::fill (w, w + kChunk, 1.0f);
        for (size_t pos = 0; pos < in.size(); pos += kChunk)
        {
            const int n = (int) std::min<size_t> (kChunk, in.size() - pos);
            m.control (p.data(), ctx);
            m.process (in.L.data() + pos, in.R.data() + pos, out.L.data() + pos, out.R.data() + pos, w, n);
        }
        return out;
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

    std::vector<float> defaults()
    {
        std::vector<float> p (kNumParams);
        setDefaults (p.data());
        return p;
    }

    /** Simple gain module used to verify the focus split. */
    struct GainModule
    {
        static constexpr bool kSendTarget = false;
        static int maxLatency (double) { return 0; }
        float gain = 1.0f;
        void prepare (double) {}
        void configure (int, int) {}
        void reset() {}
        int latency() const { return 0; }
        void control (const float*, const ModCtx&) {}
        void process (const float* l, const float* r, float* ol, float* orr, const float*, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                ol[i] = l[i] * gain;
                orr[i] = r[i] * gain;
            }
        }
    };
} // namespace

//==============================================================================
void testOversampler()
{
    const auto& A = Oversampler::stageA();
    const auto& B = Oversampler::stageB();
    auto response = [] (const std::vector<float>& h, double f) {
        double re = 0, im = 0;
        for (size_t n = 0; n < h.size(); ++n)
        {
            re += h[n] * std::cos (2 * 3.14159265358979 * f * (double) n);
            im -= h[n] * std::sin (2 * 3.14159265358979 * f * (double) n);
        }
        return std::sqrt (re * re + im * im);
    };
    for (int s = 0; s < 2; ++s)
    {
        const auto& st = s == 0 ? A : B;
        const double fp = s == 0 ? 0.2 : 0.136, fsb = s == 0 ? 0.3 : 0.364;
        double ripple = 0, stop = 0;
        for (double f = 0; f <= fp; f += 0.001)
            ripple = std::max (ripple, std::fabs (toDb (response (st.impulse(), f))));
        for (double f = fsb; f <= 0.5; f += 0.001)
            stop = std::max (stop, response (st.impulse(), f));
        const std::string nm = s == 0 ? "stage A (51 taps)" : "stage B (27 taps)";
        addResult ("Oversampler", nm + " passband ripple", fmt ("%.4f dB", ripple), "< 0.01 dB", ripple < 0.01);
        addResult ("Oversampler", nm + " stopband", fmt ("%.1f dB", toDb (stop)), "< -75 dB", toDb (stop) < -75.0);
    }
    // round-trip latency: impulse through up -> down
    for (int f : { 2, 4 })
    {
        Oversampler os;
        os.prepare (kChunk);
        os.setFactor (f);
        std::vector<float> in (4096, 0.0f), out (4096, 0.0f);
        in[100] = 1.0f;
        for (size_t pos = 0; pos < in.size(); pos += kChunk)
        {
            os.upsample (in.data() + pos, in.data() + pos, kChunk);
            os.downsample (out.data() + pos, out.data() + pos, kChunk);
        }
        size_t pk = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::fabs (out[i]) > std::fabs (out[pk]))
                pk = i;
        const int lat = (int) pk - 100;
        addResult ("Oversampler", fmt ("%dx round-trip latency", f), fmt ("%d samples (peak %.4f)", lat, out[pk]),
                   fmt ("== %d (reported)", Oversampler::latencyFor (f)), lat == Oversampler::latencyFor (f));
        // passband gain at 1 kHz and 18 kHz
        auto s1 = sine (kSR, 1000.0, 0.5, 32768), s2 = sine (kSR, 18000.0, 0.5, 32768);
        for (auto* sg : { &s1, &s2 })
        {
            os.reset();
            Buf o (sg->size());
            for (size_t pos = 0; pos < sg->size(); pos += kChunk)
            {
                os.upsample (sg->L.data() + pos, sg->L.data() + pos, kChunk);
                os.downsample (o.data() + pos, o.data() + pos, kChunk);
            }
            const double g = toDb (rms (o, 4096) / rms (sg->L, 4096));
            const bool hi = sg == &s2;
            addResult ("Oversampler", fmt ("%dx gain @ %s", f, hi ? "18 kHz" : "1 kHz"), fmt ("%.3f dB", g),
                       hi ? "> -0.5 dB" : "|g| < 0.01 dB", hi ? g > -0.5 : std::fabs (g) < 0.01);
        }
    }
}

//==============================================================================
void testFocusSplit()
{
    auto p = defaults();
    const int m = kModHeat;
    p[(size_t) sharedParam (m, kSfFocusLo)] = 300.0f;
    p[(size_t) sharedParam (m, kSfFocusHi)] = 3000.0f;
    // identity processing -> exact reconstruction
    {
        auto slot = std::make_unique<ModuleSlot<GainModule>>();
        auto in = noise (kSR, 0.5, 48000, 7);
        auto out = runSlot (*slot, m, kSR, 1, 0, in, p);
        const double d = maxAbsDiff (out.L, in.L);
        addResult ("Focus", "identity module, focus 300-3k: reconstruction", d == 0 ? "bit-exact" : fmt ("%.2e", d),
                   "bit-exact", d == 0);
    }
    // band gain g = 0 / 2: measured response vs. the analytic |1 + (g-1) * HP2(f) * LP2(f)|
    auto analytic = [] (double f, double g) {
        // 2nd-order Butterworth HP at 300 Hz and LP at 3 kHz (bilinear warping negligible here)
        const std::complex<double> s (0.0, f);
        const std::complex<double> sh = s / 300.0, sl = s / 3000.0;
        const auto hp = sh * sh / (sh * sh + std::sqrt (2.0) * sh + 1.0);
        const auto lp = 1.0 / (sl * sl + std::sqrt (2.0) * sl + 1.0);
        return 20.0 * std::log10 (std::abs (1.0 + (g - 1.0) * hp * lp));
    };
    for (float g : { 0.0f, 2.0f })
    {
        std::vector<std::string> row;
        double worstErr = 0, worstOut = -100;
        for (double f : { 100.0, 212.0, 300.0, 424.0, 950.0, 3000.0, 6000.0, 12000.0 })
        {
            auto slot = std::make_unique<ModuleSlot<GainModule>>();
            slot->mod.gain = g;
            auto in = sine (kSR, f, 0.25, 32768);
            auto out = runSlot (*slot, m, kSR, 1, 0, in, p);
            const double db = toDb (rms (out.L, 8192) / rms (in.L, 8192));
            const double ex = analytic (f, g);
            row.push_back (fmt ("%.0f Hz %+.2f (model %+.2f)", f, db, ex));
            if (std::fabs (ex) < 20.0)
                worstErr = std::max (worstErr, std::fabs (db - ex));
            if (f <= 212.0 || f >= 6000.0)
                worstOut = std::max (worstOut, db);
        }
        const std::string tn = g == 0 ? "kill band (g=0)" : "boost band (g=2)";
        addInfo ("Focus", tn + " response, focus 300 Hz-3 kHz", row[0] + ", " + row[2] + ", " + row[4] + ", " + row[6]);
        addResult ("Focus", tn + " matches analytic complementary response", fmt ("max error %.2f dB", worstErr),
                   "<= 0.25 dB", worstErr <= 0.25);
        if (g == 0)
            addResult ("Focus", "kill band: worst out-of-band bump", fmt ("%+.2f dB", worstOut), "< +2.2 dB (design)",
                       worstOut < 2.2);
    }
}

//==============================================================================
void testModuleNulls()
{
    struct Case
    {
        const char* name;
        int module;
    };
    auto in = makeDrumLoop (kSR, 120, 2, 11).audio;
    auto nz = noise (kSR, 0.3, in.size(), 3);
    for (size_t i = 0; i < in.size(); ++i)
    {
        in.L[i] += nz.L[i];
        in.R[i] += nz.R[i];
    }
    auto heavy = defaults();
    heavy[kLayerSubLevel] = 0;
    heavy[kLayerSubGrowl] = 60;
    heavy[kLayerNoiseLevel] = 0;
    heavy[kWarpPitch] = -5;
    heavy[kWarpFormant] = 3;
    heavy[kPressAmount] = 80;
    heavy[kHeatDrive] = 24;
    heavy[kSmashDrive] = 24;
    heavy[kSmashBias] = 40;
    heavy[kSmashCeiling] = -6;
    for (int m = 0; m < kNumModules; ++m)
        heavy[(size_t) sharedParam (m, kSfMix)] = 100.0f;
    std::vector<std::vector<std::string>> table;
    for (int variant = 0; variant < 3; ++variant)
    {
        for (int m = 0; m < kNumModules; ++m)
        {
            for (int os : { 1, 2, 4 })
            {
                auto p = heavy;
                if (variant == 0)
                    p[(size_t) sharedParam (m, kSfBypass)] = 1.0f;
                else
                    p[(size_t) sharedParam (m, kSfMix)] = 0.0f;
                if (variant == 2)
                {
                    p[(size_t) sharedParam (m, kSfFocusLo)] = 250.0f;
                    p[(size_t) sharedParam (m, kSfFocusHi)] = 4000.0f;
                    p[(size_t) sharedParam (m, kSfTarget)] = -60.0f;
                }
                Stereo out;
                int L = 0;
                switch (m)
                {
                    case kModLayer: { auto s = std::make_unique<ModuleSlot<LayerModule>>(); out = runSlot (*s, m, kSR, os, 0, in, p); L = s->latency(); break; }
                    case kModWarp: { auto s = std::make_unique<ModuleSlot<WarpModule>>(); out = runSlot (*s, m, kSR, os, 0, in, p); L = s->latency(); break; }
                    case kModRoom: { auto s = std::make_unique<ModuleSlot<RoomModule>>(); out = runSlot (*s, m, kSR, os, 0, in, p); L = s->latency(); break; }
                    case kModPress: { auto s = std::make_unique<ModuleSlot<PressModule>>(); out = runSlot (*s, m, kSR, os, 0, in, p); L = s->latency(); break; }
                    case kModHeat: { auto s = std::make_unique<ModuleSlot<HeatModule>>(); out = runSlot (*s, m, kSR, os, 0, in, p); L = s->latency(); break; }
                    default: { auto s = std::make_unique<ModuleSlot<SmashModule>>(); out = runSlot (*s, m, kSR, os, 0, in, p); L = s->latency(); break; }
                }
                const auto ref = delayed (in, L);
                const double d = std::max (maxAbsDiff (out.L, ref.L), maxAbsDiff (out.R, ref.R));
                const char* vn = variant == 0 ? "bypass" : (variant == 1 ? "mix 0%" : "mix 0% + focus/target");
                if (os == 2 || m == kModPress || m == kModHeat || m == kModSmash)
                    table.push_back ({ moduleName (m), vn, fmt ("%dx", os), fmt ("%d", L),
                                       d == 0 ? "bit-exact" : fmt ("%.1f dB", toDb (d)) });
                const bool pass = d == 0 || toDb (d) < -120.0;
                if (! pass)
                    addResult ("Null", fmt ("%s %s %dx", moduleName (m), vn, os), fmt ("%.1f dB", toDb (d)),
                               "<= -120 dB", false);
            }
        }
    }
    int exact = 0;
    for (auto& r : table)
        exact += r[4] == "bit-exact" ? 1 : 0;
    addResult ("Null", "every module: bypass / mix 0 / mix 0 + focus, latency-compensated",
               fmt ("%d/%d cases bit-exact", exact, (int) table.size()), "bit-exact or <= -120 dB",
               exact == (int) table.size());
    addTable ("Module null tests (output vs. input delayed by the module latency)",
              { "Module", "Condition", "OS", "Latency", "Residual" }, table);
}

//==============================================================================
void testDetectors()
{
    struct Score
    {
        int gt = 0, hits = 0, det = 0, falses = 0;
    };
    auto score = [] (const std::vector<int64_t>& gt, const std::vector<int64_t>& det, double sr) {
        Score s;
        s.gt = (int) gt.size();
        s.det = (int) det.size();
        std::vector<bool> used (gt.size(), false);
        for (auto d : det)
        {
            bool matched = false;
            for (size_t g = 0; g < gt.size(); ++g)
            {
                if (! used[g] && d >= gt[g] - (int64_t) (0.002 * sr) && d <= gt[g] + (int64_t) (0.020 * sr))
                {
                    used[g] = true;
                    matched = true;
                    break;
                }
            }
            if (matched)
                ++s.hits;
            else
                ++s.falses;
        }
        return s;
    };

    std::vector<std::vector<std::string>> table;
    Score subAll, noiseAll, subBass, noiseBass;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int variant = 0; variant < 4; ++variant)
        {
            const double bpm = variant == 2 ? 150.0 : (variant == 3 ? 90.0 : 120.0);
            const bool bass = variant == 1;
            const float lvl = variant == 3 ? 0.5f : 1.0f; // -6 dB quieter loop
            auto loop = makeDrumLoop (sr, bpm, 4, 100u + (uint32_t) variant, bass, lvl);
            auto p = defaults();
            auto slot = std::make_unique<ModuleSlot<LayerModule>>();
            std::vector<int64_t> subLog, noiseLog;
            subLog.reserve (1000);
            noiseLog.reserve (1000);
            slot->prepare (sr, kModLayer);
            slot->mod.setTriggerLog (&subLog, &noiseLog);
            Stereo out = loop.audio;
            ModCtx ctx;
            for (size_t pos = 0; pos < out.size(); pos += kChunk)
            {
                const int n = (int) std::min<size_t> (kChunk, out.size() - pos);
                slot->control (p.data(), 1.0f, ctx);
                slot->process (out.L.data() + pos, out.R.data() + pos, n);
            }
            // snare/hat ground truth: coincident snare+hat counts once; hats that coincide
            // with a kick are "don't care" (the kick veto suppresses the noise layer there)
            const int64_t tol = (int64_t) (0.005 * sr);
            std::vector<int64_t> hatsSnares;
            std::vector<int64_t> dontCare;
            auto all = loop.snares;
            all.insert (all.end(), loop.hats.begin(), loop.hats.end());
            std::sort (all.begin(), all.end());
            for (auto t : all)
            {
                bool nearKick = false;
                for (auto k : loop.kicks)
                    nearKick = nearKick || std::llabs (t - k) <= tol;
                if (nearKick)
                {
                    dontCare.push_back (t);
                    continue;
                }
                if (hatsSnares.empty() || t - hatsSnares.back() > tol)
                    hatsSnares.push_back (t);
            }
            std::vector<int64_t> noiseFiltered;
            for (auto d : noiseLog)
            {
                bool dc = false;
                for (auto t : dontCare)
                    dc = dc || (d >= t - (int64_t) (0.002 * sr) && d <= t + (int64_t) (0.020 * sr));
                if (! dc)
                    noiseFiltered.push_back (d);
            }
            const auto ks = score (loop.kicks, subLog, sr);
            const auto ns = score (hatsSnares, noiseFiltered, sr);
            auto accumulate = [] (Score& a, const Score& b) {
                a.gt += b.gt;
                a.hits += b.hits;
                a.det += b.det;
                a.falses += b.falses;
            };
            accumulate (bass ? subBass : subAll, ks);
            accumulate (bass ? noiseBass : noiseAll, ns);
            const char* vn[] = { "120 BPM", "120 BPM + bass line", "150 BPM", "90 BPM, -6 dB" };
            table.push_back ({ fmt ("%.1f kHz", sr / 1000.0), vn[variant], fmt ("%d/%d", ks.hits, ks.gt),
                               fmt ("%d", ks.falses), fmt ("%d/%d", ns.hits, ns.gt), fmt ("%d", ns.falses) });
        }
    const double subHit = 100.0 * subAll.hits / std::max (1, subAll.gt);
    const double subFalse = 100.0 * subAll.falses / std::max (1, subAll.det);
    const double nHit = 100.0 * noiseAll.hits / std::max (1, noiseAll.gt);
    const double nFalse = 100.0 * noiseAll.falses / std::max (1, noiseAll.det);
    addResult ("Detectors", "kick (sub) trigger hit rate, drum patterns", fmt ("%.1f%% (%d/%d)", subHit, subAll.hits, subAll.gt),
               ">= 95%", subHit >= 95.0);
    addResult ("Detectors", "kick (sub) false-trigger rate, drum patterns", fmt ("%.1f%% of triggers", subFalse), "<= 5%",
               subFalse <= 5.0);
    addResult ("Detectors", "snare/hat (noise) trigger hit rate, drum patterns",
               fmt ("%.1f%% (%d/%d)", nHit, noiseAll.hits, noiseAll.gt), ">= 95%", nHit >= 95.0);
    addResult ("Detectors", "snare/hat (noise) false-trigger rate, drum patterns", fmt ("%.1f%% of triggers", nFalse), "<= 5%",
               nFalse <= 5.0);
    addInfo ("Detectors", "kick detector with sustained bass-line confounder",
             fmt ("hits %d/%d, %d false of %d triggers (known limitation, see DESIGN.md)", subBass.hits, subBass.gt,
                  subBass.falses, subBass.det));
    addInfo ("Detectors", "snare/hat detector with sustained bass-line confounder",
             fmt ("hits %d/%d, %d false of %d triggers", noiseBass.hits, noiseBass.gt, noiseBass.falses, noiseBass.det));
    addTable ("Transient trigger detectors (default sensitivity 50%, match window -2..+20 ms; coincident snare+hat "
              "counted once; hats on a kick are excluded from the noise detector's ground truth)",
              { "SR", "Loop", "Kick hits", "Kick false", "Snare/hat hits", "Snare/hat false" }, table);

    // dual-envelope Target detector: transientness high at onsets, low in tails
    {
        auto loop = makeDrumLoop (kSR, 120, 4, 5);
        TransientDetector det;
        det.prepare ((float) kSR);
        std::vector<float> t (loop.audio.size() / kChunk + 1, 0.0f);
        for (size_t i = 0; i < loop.audio.size(); ++i)
        {
            if (i % kChunk == 0)
                t[i / kChunk] = det.evaluate();
            det.push (std::max (std::fabs (loop.audio.L[i]), std::fabs (loop.audio.R[i])));
        }
        int onsetHigh = 0, total = 0;
        auto all = loop.kicks;
        all.insert (all.end(), loop.snares.begin(), loop.snares.end());
        std::vector<char> nearOnset (t.size(), 0);
        for (auto o : all)
        {
            float mx = 0;
            for (int64_t k = o / kChunk; k < (o + (int64_t) (0.010 * kSR)) / kChunk && k < (int64_t) t.size(); ++k)
                mx = std::max (mx, t[(size_t) k]);
            onsetHigh += mx > 0.5f ? 1 : 0;
            ++total;
            for (int64_t k = o / kChunk - 2; k < (o + (int64_t) (0.08 * kSR)) / kChunk && k < (int64_t) t.size(); ++k)
                if (k >= 0)
                    nearOnset[(size_t) k] = 1;
        }
        int tailHigh = 0, tailN = 0;
        for (size_t k = 0; k < t.size(); ++k)
            if (! nearOnset[k])
            {
                ++tailN;
                tailHigh += t[k] > 0.5f ? 1 : 0;
            }
        // hats are excluded from "onsets" but still produce transients; tails are time >80 ms after kicks/snares
        addResult ("Detectors", "Target detector: kick/snare onsets flagged (t>0.5 within 10 ms)",
                   fmt ("%d/%d", onsetHigh, total), ">= 95%", onsetHigh >= (int) (0.95 * total));
        addInfo ("Detectors", "Target detector: fraction of tail time flagged transient",
                 fmt ("%.1f%% (includes hat onsets)", 100.0 * tailHigh / std::max (1, tailN)));
    }
}

//==============================================================================
void testCompressors()
{
    const char* names[] = { "FET", "Opto", "VCA", "Diode", "Vari-Mu", "Glue" };
    std::vector<std::vector<std::string>> curveTable, timeTable;
    for (int type = 0; type < kNumPressTypes; ++type)
    {
        auto p = defaults();
        p[kPressType] = (float) type;
        p[kPressAmount] = 60.0f;
        p[kPressAttack] = 50.0f;
        p[kPressRelease] = 50.0f;
        p[kPressMakeup] = 0.0f;
        p[kPressScHpf] = 20.0f;
        const auto set = PressModule::mapControls (type, 0.6f, 0.5f, 0.5f, true, false, false);
        const bool feedback = type == kPressOpto || type == kPressVariMu;

        // ---- static curve: stepped 1 kHz sine levels ----
        std::vector<double> levels;
        for (double l = -42; l <= 0.01; l += 3)
            levels.push_back (l);
        const size_t hold = (size_t) ((feedback ? 1.2 : 0.5) * kSR);
        Stereo in (hold * levels.size());
        for (size_t k = 0; k < levels.size(); ++k)
            for (size_t i = 0; i < hold; ++i)
            {
                const float v = (float) (std::pow (10.0, levels[k] / 20.0)
                                         * std::sin (2 * 3.14159265358979 * 1000.0 * (double) (k * hold + i) / kSR));
                in.L[k * hold + i] = in.R[k * hold + i] = v;
            }
        auto mod = std::make_unique<PressModule>();
        auto out = runDirect (*mod, kSR, 1, 0, in, p);
        std::vector<double> gr;
        std::string curve;
        bool monotonic = true;
        double prevOut = -1e9;
        for (size_t k = 0; k < levels.size(); ++k)
        {
            const size_t st = k * hold + hold - (size_t) (0.1 * kSR);
            const double oDb = toDb (rms (out.L, st, (size_t) (0.1 * kSR)) * std::sqrt (2.0));
            gr.push_back (levels[k] - oDb);
            if (oDb < prevOut - 0.25)
                monotonic = false;
            prevOut = oDb;
        }
        // expected static GR at 0 dBFS
        double expect;
        if (feedback)
        {
            double y = 0.0;
            for (int it = 0; it < 200; ++it)
            {
                double target = (set.ratio - 1.0) * std::max (0.0, y - set.thresh);
                if (type == kPressVariMu)
                    target += 0.015 * std::max (0.0, y - set.thresh) * std::max (0.0, y - set.thresh);
                y = 0.5 * y + 0.5 * (0.0 - target);
            }
            expect = -y;
        }
        else
            expect = PressModule::gainComputer (0.0f, set.thresh, set.ratio, set.knee);
        // effective ratio between T+6 and T+18 dB (input) from the measured curve
        auto grAt = [&] (double lvl) {
            for (size_t k = 0; k + 1 < levels.size(); ++k)
                if (lvl >= levels[k] && lvl <= levels[k + 1])
                    return gr[k] + (gr[k + 1] - gr[k]) * (lvl - levels[k]) / 3.0;
            return gr.back();
        };
        const double l1 = std::min (-3.0, set.thresh + 6.0), l2 = std::min (0.0, set.thresh + 18.0);
        const double slope = 1.0 - (grAt (l2) - grAt (l1)) / (l2 - l1);
        const double effRatio = slope > 1e-3 ? 1.0 / slope : 99.0;
        curveTable.push_back ({ names[type], fmt ("%.1f", set.thresh), fmt ("%.2f:1", set.ratio),
                                fmt ("%.1f", set.knee), fmt ("%.1f", grAt (-30)), fmt ("%.1f", grAt (-18)),
                                fmt ("%.1f", grAt (-6)), fmt ("%.1f", gr.back()), fmt ("%.1f", expect),
                                fmt ("%.2f:1", effRatio) });
        addResult ("Compressor", fmt ("%s static curve monotonic", names[type]), monotonic ? "yes" : "no",
                   "output rises with input", monotonic);
        addResult ("Compressor", fmt ("%s GR @ 0 dBFS (sine peak)", names[type]),
                   fmt ("%.1f dB (model %.1f)", gr.back(), expect), "within +-3 dB of static model",
                   std::fabs (gr.back() - expect) <= 3.0);

        // ---- attack / release: step -40 -> -6 dBFS at 0.5 s, back at 2.5 s ----
        const size_t n = (size_t) (6.0 * kSR);
        Stereo st (n);
        for (size_t i = 0; i < n; ++i)
        {
            const double t = (double) i / kSR;
            const double a = (t >= 0.5 && t < 2.5) ? std::pow (10.0, -6.0 / 20.0) : std::pow (10.0, -40.0 / 20.0);
            st.L[i] = st.R[i] = (float) (a * std::sin (2 * 3.14159265358979 * 1000.0 * t));
        }
        auto m2 = std::make_unique<PressModule>();
        m2->prepare (kSR);
        m2->configure (1, 0);
        std::vector<float> grTrace (n);
        ModCtx ctx;
        float w = 1.0f, oL, oR;
        for (size_t i = 0; i < n; ++i)
        {
            if (i % kChunk == 0)
                m2->control (p.data(), ctx);
            m2->process (&st.L[i], &st.R[i], &oL, &oR, &w, 1);
            grTrace[i] = m2->takeGrMeter();
        }
        // smooth the per-sample GR with a 1 ms max-hold to remove the sine ripple
        const size_t win = (size_t) (0.001 * kSR);
        std::vector<float> env (n);
        for (size_t i = 0; i < n; ++i)
        {
            float mx = 0;
            for (size_t k = (i >= win ? i - win : 0); k <= i; ++k)
                mx = std::max (mx, grTrace[k]);
            env[i] = mx;
        }
        const size_t s0 = (size_t) (0.5 * kSR), s1 = (size_t) (2.5 * kSR);
        const double grMax = env[s1 - 1];
        double tAtt = -1, tRel = -1, tRel50 = -1, tRel90 = -1;
        for (size_t i = s0; i < s1; ++i)
            if (env[i] >= 0.632 * grMax)
            {
                tAtt = (double) (i - s0) / kSR;
                break;
            }
        for (size_t i = s1; i < n; ++i)
        {
            if (tRel50 < 0 && env[i] <= 0.5 * grMax)
                tRel50 = (double) (i - s1) / kSR;
            if (tRel < 0 && env[i] <= 0.368 * grMax)
                tRel = (double) (i - s1) / kSR;
            if (tRel90 < 0 && env[i] <= 0.1 * grMax)
                tRel90 = (double) (i - s1) / kSR;
        }
        timeTable.push_back ({ names[type], fmt ("%.2f ms", set.attackSec * 1000.0), fmt ("%.2f ms", tAtt * 1000.0),
                               fmt ("%.0f ms%s", set.releaseSec * 1000.0, set.autoRelease ? " (auto)" : ""),
                               fmt ("%.0f ms", tRel50 * 1000.0), fmt ("%.0f ms", tRel * 1000.0),
                               fmt ("%.0f ms", tRel90 * 1000.0), fmt ("%.1f dB", grMax) });
        const bool attOk = tAtt >= 0 && tAtt <= std::max (0.0015, 3.0 * set.attackSec + 0.0015);
        addResult ("Compressor", fmt ("%s attack (63%%)", names[type]), fmt ("%.2f ms", tAtt * 1000.0),
                   fmt ("<= 3 x design (%.2f ms) + 1.5 ms", set.attackSec * 1000.0), attOk);
        if (type == kPressOpto)
            addResult ("Compressor", "Opto two-stage release (t90/t50)", fmt ("%.1f", tRel90 / std::max (1e-4, tRel50)),
                       "> 4 (fast first stage, slow tail)", tRel90 / std::max (1e-4, tRel50) > 4.0);
        else
            addResult ("Compressor", fmt ("%s release (37%%)", names[type]), fmt ("%.0f ms", tRel * 1000.0),
                       fmt ("0.2x..3x design (%.0f ms)", set.releaseSec * 1000.0),
                       tRel > 0.2 * set.releaseSec * (set.autoRelease ? 0.3 : 1.0) && tRel < 3.0 * set.releaseSec);
    }
    addTable ("Compressor static curves (Amount 60%, makeup off, 1 kHz sine, GR in dB at input peak level)",
              { "Type", "Thresh dB", "Ratio", "Knee dB", "GR@-30", "GR@-18", "GR@-6", "GR@0", "Model GR@0", "Eff. ratio" },
              curveTable);
    addTable ("Compressor time constants (Attack/Release 50%, step -40 -> -6 dBFS)",
              { "Type", "Design attack", "Meas. attack 63%", "Design release", "Meas. 50%", "Meas. 37%", "Meas. 90%", "GR" },
              timeTable);
}

//==============================================================================
void testSaturation()
{
    const char* modes[] = { "Tape", "Tube", "Transformer", "Diode", "Fuzz", "Crush" };
    std::vector<std::vector<std::string>> thdTable, aliasTable;
    const int nfft = 65536;
    for (int mode = 0; mode < kNumHeatModes; ++mode)
    {
        auto p = defaults();
        p[kHeatMode] = (float) mode;
        p[kHeatDrive] = 12.0f;
        // THD at 2x: 1 kHz, -6 dBFS
        {
            auto in = sine (kSR, 1000.0, 0.5, (size_t) nfft + 24000);
            auto mod = std::make_unique<HeatModule>();
            auto out = runDirect (*mod, kSR, 2, 0, in, p);
            auto P = powerSpectrum (out.L.data() + 24000, nfft);
            const double h1 = tonePowerDb (P, 1000.0, kSR, nfft);
            std::vector<std::string> row { modes[mode], fmt ("%.1f", h1) };
            double sum = 0;
            for (int h = 2; h <= 9; ++h)
            {
                const double hd = tonePowerDb (P, 1000.0 * h, kSR, nfft) - h1;
                sum += std::pow (10.0, hd / 10.0);
                if (h <= 7)
                    row.push_back (fmt ("%.1f", hd));
            }
            const double thd = 100.0 * std::sqrt (sum);
            row.push_back (fmt ("%.2f%%", thd));
            // THD+N: everything except the fundamental (and DC)
            double tot = 0, fund = 0;
            for (size_t k = (size_t) (20.0 * nfft / kSR); k < P.size(); ++k)
                tot += P[k];
            fund = std::pow (10.0, h1 / 10.0) * 0.5;
            const double thdn = 100.0 * std::sqrt (std::max (0.0, tot - fund) / std::max (fund, 1e-30));
            row.push_back (fmt ("%.2f%%", thdn));
            thdTable.push_back (row);
            if (mode == kHeatCrush)
                addResult ("Saturation", "Crush adds non-harmonic grit (THD+N @ +12 dB drive)", fmt ("%.2f%%", thdn),
                           "> 1% (S&H images + quantisation)", thdn > 1.0);
            else
                addResult ("Saturation", fmt ("%s produces harmonics (THD @ +12 dB drive)", modes[mode]),
                           fmt ("%.2f%%", thd), "0.1% .. 60%", thd > 0.1 && thd < 60.0);
        }
        // aliasing vs oversampling: 7919 Hz, -6 dBFS, +24 dB drive
        {
            p[kHeatDrive] = 24.0f;
            const double f0 = 7919.0;
            std::vector<std::string> row { modes[mode] };
            double al[3] {};
            int idx = 0;
            for (int os : { 1, 2, 4 })
            {
                auto in = sine (kSR, f0, 0.5, (size_t) nfft + 24000);
                auto mod = std::make_unique<HeatModule>();
                auto out = runDirect (*mod, kSR, os, 0, in, p);
                auto P = powerSpectrum (out.L.data() + 24000, nfft);
                const double binHz = kSR / nfft;
                std::vector<char> harm (P.size(), 0);
                for (int h = 1; h * f0 < kSR / 2; ++h)
                {
                    const int k0 = (int) std::lround (h * f0 / binHz);
                    for (int k = std::max (0, k0 - 8); k <= std::min ((int) P.size() - 1, k0 + 8); ++k)
                        harm[(size_t) k] = 1;
                }
                for (int k = 0; k < (int) (30.0 / binHz); ++k)
                    harm[(size_t) k] = 1; // ignore DC region
                double alias = 0, fund = 0;
                for (size_t k = 0; k < P.size(); ++k)
                {
                    if (! harm[k])
                        alias += P[k];
                }
                fund = std::pow (10.0, tonePowerDb (P, f0, kSR, nfft) / 10.0) * 0.5;
                al[idx] = 10.0 * std::log10 (std::max (alias, 1e-30) / std::max (fund, 1e-30));
                row.push_back (fmt ("%.1f dB", al[idx]));
                ++idx;
            }
            aliasTable.push_back (row);
            if (mode != kHeatCrush)
                addResult ("Saturation", fmt ("%s aliasing 1x / 2x / 4x", modes[mode]),
                           fmt ("%.1f / %.1f / %.1f dB", al[0], al[1], al[2]),
                           "2x >= 5 dB and 4x >= 12 dB below 1x", al[1] <= al[0] - 5.0 && al[2] <= al[0] - 12.0);
        }
    }
    addTable ("Saturation harmonic spectra (1 kHz, -6 dBFS in, +12 dB drive, 2x OS; harmonics in dB re H1)",
              { "Mode", "H1 dBFS", "H2", "H3", "H4", "H5", "H6", "H7", "THD (H2-H9)", "THD+N" }, thdTable);
    addTable ("Saturation aliasing (7919 Hz, -6 dBFS, +24 dB drive; non-harmonic energy re fundamental)",
              { "Mode", "1x", "2x", "4x" }, aliasTable);

    // SMASH curves, aliasing 1x vs 2x
    const char* curves[] = { "Smooth", "Crunch", "Fold", "Rectify", "Stairs" };
    std::vector<std::vector<std::string>> smashTable;
    for (int c = 0; c < kNumCurves; ++c)
    {
        auto p = defaults();
        p[kSmashCurve] = (float) c;
        p[kSmashDrive] = 18.0f;
        double al[2] {};
        for (int k = 0; k < 2; ++k)
        {
            const int os = k == 0 ? 1 : 2;
            auto in = sine (kSR, 5003.0, 0.5, (size_t) nfft + 24000);
            auto mod = std::make_unique<SmashModule>();
            auto out = runDirect (*mod, kSR, os, 0, in, p);
            auto P = powerSpectrum (out.L.data() + 24000, nfft);
            const double binHz = kSR / nfft;
            std::vector<char> harm (P.size(), 0);
            for (int h = 1; h * 5003.0 < kSR / 2; ++h)
            {
                const int k0 = (int) std::lround (h * 5003.0 / binHz);
                for (int q = std::max (0, k0 - 8); q <= std::min ((int) P.size() - 1, k0 + 8); ++q)
                    harm[(size_t) q] = 1;
            }
            for (int q = 0; q < (int) (30.0 / binHz); ++q)
                harm[(size_t) q] = 1;
            double alias = 0, total = 0;
            for (size_t q = 0; q < P.size(); ++q)
            {
                total += P[q];
                if (! harm[q])
                    alias += P[q];
            }
            al[k] = 10.0 * std::log10 (std::max (alias, 1e-30) / std::max (total, 1e-30));
        }
        smashTable.push_back ({ curves[c], fmt ("%.1f dB", al[0]), fmt ("%.1f dB", al[1]) });
        addResult ("Saturation", fmt ("SMASH %s aliasing: 2x vs 1x", curves[c]), fmt ("%.1f -> %.1f dB", al[0], al[1]),
                   ">= 10 dB lower with 2x (or < -90 dB)", al[1] <= al[0] - 10.0 || al[1] < -90.0);
    }
    addTable ("SMASH aliasing (5003 Hz, -6 dBFS, +18 dB drive; non-harmonic energy re total)", { "Curve", "1x", "2x" },
              smashTable);
}

//==============================================================================
void testShift()
{
    std::vector<std::vector<std::string>> table;
    for (int mode : { kWarpHQ, kWarpLoFi })
    {
        double worst = 0;
        for (double st : { -12.0, -7.0, -3.5, 3.0, 7.0, 12.0 })
        {
            auto p = defaults();
            p[kWarpPitch] = (float) st;
            p[kWarpMode] = (float) mode;
            auto in = sine (kSR, 440.0, 0.3, (size_t) (2.5 * kSR));
            auto mod = std::make_unique<WarpModule>();
            auto out = runDirect (*mod, kSR, 1, mode, in, p);
            const int n = 65536;
            const double expect = 440.0 * std::pow (2.0, st / 12.0);
            const double f = peakFrequency (out.L.data() + out.size() - n, n, kSR, expect * 0.8, expect * 1.25);
            const double cents = 1200.0 * std::log2 (f / expect);
            worst = std::max (worst, std::fabs (cents));
            table.push_back ({ mode == kWarpHQ ? "HQ" : "Lo-Fi", fmt ("%+.1f st", st), fmt ("%.2f Hz", expect),
                               fmt ("%.2f Hz", f), fmt ("%+.2f", cents) });
        }
        const double lim = mode == kWarpHQ ? 5.0 : 15.0;
        addResult ("Shift", fmt ("%s pitch accuracy (440 Hz, -12..+12 st)", mode == kWarpHQ ? "HQ" : "Lo-Fi"),
                   fmt ("worst %.2f cents", worst), fmt ("<= %.0f cents", lim), worst <= lim);
    }
    addTable ("Shift pitch accuracy (sine 440 Hz)", { "Mode", "Shift", "Expected", "Measured", "Error (cents)" }, table);

    // formant: 150 Hz harmonic series with a resonance at 1 kHz
    auto analyse = [] (const Stereo& out, double& f0, double& envPeak) {
        const int n = 65536;
        const float* x = out.L.data() + out.size() - n;
        f0 = peakFrequency (x, n, kSR, 100.0, 260.0);
        auto P = powerSpectrum (x, n);
        // harmonic amplitudes -> envelope peak via parabolic fit around the max harmonic
        std::vector<double> amps;
        for (int h = 1; h * f0 < 5000.0; ++h)
            amps.push_back (tonePowerDb (P, h * f0, kSR, n, 3));
        size_t best = 0;
        for (size_t h = 0; h < amps.size(); ++h)
            if (amps[h] > amps[best])
                best = h;
        double fpk = (best + 1) * f0;
        if (best > 0 && best + 1 < amps.size())
        {
            const double a = amps[best - 1], b = amps[best], c = amps[best + 1];
            const double d = 0.5 * (a - c) / (a - 2 * b + c);
            fpk = (best + 1 + d) * f0;
        }
        envPeak = fpk;
    };
    struct FCase
    {
        const char* name;
        float pitch, formant;
        bool link;
        double expF0, expEnv;
    };
    const FCase cases[] = {
        { "formant +5 st only", 0.0f, 5.0f, false, 150.0, 1000.0 * std::pow (2.0, 5.0 / 12.0) },
        { "formant -4 st only", 0.0f, -4.0f, false, 150.0, 1000.0 * std::pow (2.0, -4.0 / 12.0) },
        { "pitch +5 st, formant kept", 5.0f, 0.0f, false, 150.0 * std::pow (2.0, 5.0 / 12.0), 1000.0 },
        { "pitch +5 st linked", 5.0f, 0.0f, true, 150.0 * std::pow (2.0, 5.0 / 12.0), 1000.0 * std::pow (2.0, 5.0 / 12.0) },
    };
    // (1) noise through a 1 kHz resonance: the resonance must move with the formant
    //     control and stay put for a formant-preserving pitch shift (pass/fail)
    auto resonancePeak = [] (const Buf& x) {
        const int seg = 8192;
        std::vector<double> P ((size_t) (seg / 2 + 1), 0.0);
        for (size_t off = 0; off + seg <= x.size(); off += seg / 2)
        {
            auto pp = powerSpectrum (x.data() + off, seg);
            for (size_t k = 0; k < P.size(); ++k)
                P[k] += pp[k];
        }
        const int hw = (int) (60.0 * seg / kSR);
        int kb = 0;
        double best = -1;
        for (int k = (int) (300.0 * seg / kSR); k <= (int) (4000.0 * seg / kSR); ++k)
        {
            double sm = 0;
            for (int j = -hw; j <= hw; ++j)
                sm += P[(size_t) (k + j)];
            if (sm > best)
            {
                best = sm;
                kb = k;
            }
        }
        return kb * kSR / seg;
    };
    Stereo resIn ((size_t) (4.0 * kSR));
    {
        SvfCoeffs bp;
        bp.bandpassNorm (1000.0f, 4.0f, (float) kSR);
        SvfState st;
        Rng r (5);
        for (size_t i = 0; i < resIn.size(); ++i)
            resIn.L[i] = resIn.R[i] = st.process (bp, r.bipolar()) * 0.5f + 0.02f * r.bipolar();
    }
    std::vector<std::vector<std::string>> ftable;
    ftable.push_back ({ "input (noise, resonance)", fmt ("%.0f", resonancePeak (resIn.L)), "-", "-" });
    for (const auto& c : cases)
    {
        auto p = defaults();
        p[kWarpPitch] = c.pitch;
        p[kWarpFormant] = c.formant;
        p[kWarpLink] = c.link ? 1.0f : 0.0f;
        auto mod = std::make_unique<WarpModule>();
        auto out = runDirect (*mod, kSR, 1, kWarpHQ, resIn, p);
        Buf o (out.L.begin() + 48000, out.L.end());
        const double pk = resonancePeak (o);
        const double err = 100.0 * (pk / c.expEnv - 1.0);
        ftable.push_back ({ c.name, fmt ("%.0f", pk), fmt ("%.0f", c.expEnv), fmt ("%+.1f%%", err) });
        addResult ("Shift", fmt ("%s: resonance position", c.name), fmt ("%.0f Hz (exp %.0f, %+.1f%%)", pk, c.expEnv, err),
                   "within +-8%", std::fabs (err) <= 8.0);
    }
    addTable ("Formant control (noise through a 1 kHz resonance; Welch spectrum peak)",
              { "Case", "Resonance Hz", "Expected Hz", "Error" }, ftable);
    // (2) harmonic series: pitch must follow the pitch control exactly (pass/fail);
    //     the envelope estimate from sparse harmonics is reported for information
    std::vector<std::vector<std::string>> htable;
    for (const auto& c : cases)
    {
        auto p = defaults();
        p[kWarpPitch] = c.pitch;
        p[kWarpFormant] = c.formant;
        p[kWarpLink] = c.link ? 1.0f : 0.0f;
        auto in = formantBuzz (kSR, 150.0, 1000.0, (size_t) (2.5 * kSR));
        auto mod = std::make_unique<WarpModule>();
        auto out = runDirect (*mod, kSR, 1, kWarpHQ, in, p);
        double f0, ep;
        analyse (out, f0, ep);
        const double f0err = 1200.0 * std::log2 (f0 / c.expF0);
        htable.push_back ({ c.name, fmt ("%.1f (exp %.1f)", f0, c.expF0), fmt ("%+.2f", f0err),
                            fmt ("%.0f (exp %.0f)", ep, c.expEnv) });
        addResult ("Shift", fmt ("%s: harmonic pitch", c.name), fmt ("%+.2f cents", f0err), "|err| <= 10 cents",
                   std::fabs (f0err) <= 10.0);
    }
    addTable ("Formant control on a 150 Hz harmonic series (envelope peak estimated from harmonics, information only)",
              { "Case", "F0 Hz", "F0 err (cents)", "Envelope peak Hz" }, htable);

    // latency: reported vs measured (click through a non-neutral shift)
    for (int mode : { kWarpHQ, kWarpLoFi })
    {
        auto p = defaults();
        p[kWarpPitch] = 0.3f;
        p[kWarpMode] = (float) mode;
        Stereo in ((size_t) kSR);
        const size_t at = 24000;
        for (int k = 0; k < 24; ++k) // short broadband burst
            in.L[at + (size_t) k] = in.R[at + (size_t) k] = (k % 2 ? -0.8f : 0.8f) * (1.0f - k / 24.0f);
        auto mod = std::make_unique<WarpModule>();
        auto out = runDirect (*mod, kSR, 1, mode, in, p);
        size_t pk = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::fabs (out.L[i]) > std::fabs (out.L[pk]))
                pk = i;
        const int meas = (int) pk - (int) at;
        const int rep = WarpModule::latencyFor (kSR, mode);
        addResult ("Shift", fmt ("%s latency: reported vs measured", mode == kWarpHQ ? "HQ" : "Lo-Fi"),
                   fmt ("%d vs %d samples (%.1f ms)", rep, meas, rep / kSR * 1000.0), "|diff| <= 2 ms",
                   std::abs (meas - rep) <= (int) (0.002 * kSR));
    }
    // neutral: exact delay
    {
        auto p = defaults();
        auto in = noise (kSR, 0.3, 48000, 9);
        auto mod = std::make_unique<WarpModule>();
        auto out = runDirect (*mod, kSR, 1, kWarpHQ, in, p);
        const auto ref = delayed (in, WarpModule::latencyFor (kSR, kWarpHQ));
        const double d = maxAbsDiff (out.L, ref.L);
        addResult ("Shift", "pitch 0 / formant 0: exact latency-compensated pass", d == 0 ? "bit-exact" : fmt ("%.2e", d),
                   "bit-exact", d == 0);
    }
}

//==============================================================================
void testRoom()
{
    const char* names[] = { "Room", "Plate", "Spring", "Gated", "Vintage" };
    auto loop = makeDrumLoop (kSR, 120, 4, 21);
    Stereo mono = loop.audio;
    for (size_t i = 0; i < mono.size(); ++i)
        mono.R[i] = mono.L[i];
    std::vector<std::vector<std::string>> table;
    for (int type = 0; type < kNumRoomTypes; ++type)
    {
        auto p = defaults();
        p[kRoomType] = (float) type;
        // wet only (module output minus input)
        auto mod = std::make_unique<RoomModule>();
        auto out = runDirect (*mod, kSR, 1, 0, mono, p);
        Buf wl (out.size()), wr (out.size());
        for (size_t i = 0; i < out.size(); ++i)
        {
            wl[i] = out.L[i] - mono.L[i];
            wr[i] = out.R[i] - mono.R[i];
        }
        double sxy = 0, sxx = 0, syy = 0;
        for (size_t i = 0; i < wl.size(); ++i)
        {
            sxy += (double) wl[i] * wr[i];
            sxx += (double) wl[i] * wl[i];
            syy += (double) wr[i] * wr[i];
        }
        const double corr = sxy / std::sqrt (sxx * syy + 1e-30);
        const double wetDb = toDb (rms (wl) / rms (mono.L));
        // RT60 from an impulse response (Schroeder backward integration, T20 x 3)
        p[kRoomDecay] = 1.0f;
        p[kRoomPredelay] = 0.0f;
        Stereo imp ((size_t) (4.0 * kSR));
        imp.L[100] = imp.R[100] = 1.0f;
        auto mod2 = std::make_unique<RoomModule>();
        auto ir = runDirect (*mod2, kSR, 1, 0, imp, p);
        ir.L[100] -= 1.0f;
        ir.R[100] -= 1.0f;
        {   // 1 kHz octave band (RT60 is conventionally quoted at mid frequencies)
            SvfCoeffs bp;
            bp.bandpassNorm (1000.0f, 1.41f, (float) kSR);
            SvfState a1, a2, b1, b2;
            for (size_t i = 0; i < ir.size(); ++i)
            {
                ir.L[i] = a2.process (bp, a1.process (bp, ir.L[i]));
                ir.R[i] = b2.process (bp, b1.process (bp, ir.R[i]));
            }
        }
        std::vector<double> edc (ir.size());
        double acc = 0;
        for (size_t i = ir.size(); i-- > 0;)
        {
            acc += (double) ir.L[i] * ir.L[i] + (double) ir.R[i] * ir.R[i];
            edc[i] = acc;
        }
        double t5 = -1, t25 = -1;
        for (size_t i = 0; i < edc.size(); ++i)
        {
            const double db = 10 * std::log10 (edc[i] / edc[0] + 1e-30);
            if (t5 < 0 && db <= -5)
                t5 = (double) i / kSR;
            if (t25 < 0 && db <= -25)
            {
                t25 = (double) i / kSR;
                break;
            }
        }
        const double rt60 = (t25 > 0 && t5 > 0) ? 3.0 * (t25 - t5) : -1;
        table.push_back ({ names[type], fmt ("%.2f", corr), fmt ("%+.1f dB", wetDb),
                           type == kRoomGated ? "gated" : fmt ("%.2f s", rt60) });
        addResult ("Room", fmt ("%s: mono in -> decorrelated stereo", names[type]), fmt ("corr %.2f", corr),
                   "|corr| < 0.6", std::fabs (corr) < 0.6);
        if (type != kRoomGated)
            addResult ("Room", fmt ("%s: RT60 for Decay = 1.0 s", names[type]), fmt ("%.2f s", rt60), "0.6 .. 1.5 s",
                       rt60 > 0.6 && rt60 < 1.5);
        addResult ("Room", fmt ("%s: wet level @100%% mix (re dry RMS)", names[type]), fmt ("%+.1f dB", wetDb),
                   "-7 .. -1 dB (calibrated to about -4 dB)", wetDb > -7.0 && wetDb < -1.0);
    }
    // RT60 tracking across the Decay range (1 kHz octave band)
    {
        std::vector<std::vector<std::string>> rtTable;
        double worst = 0;
        for (int type : { kRoomRoom, kRoomPlate, kRoomSpring, kRoomVintage })
        {
            std::vector<std::string> row { names[type] };
            for (float d : { 0.5f, 1.0f, 2.0f, 4.0f })
            {
                auto p = defaults();
                p[kRoomType] = (float) type;
                p[kRoomDecay] = d;
                p[kRoomPredelay] = 0.0f;
                Stereo imp ((size_t) ((2.0 * d + 1.0) * kSR));
                imp.L[100] = imp.R[100] = 1.0f;
                auto m3 = std::make_unique<RoomModule>();
                auto ir = runDirect (*m3, kSR, 1, 0, imp, p);
                ir.L[100] -= 1.0f;
                ir.R[100] -= 1.0f;
                SvfCoeffs bp;
                bp.bandpassNorm (1000.0f, 1.41f, (float) kSR);
                SvfState a1, a2, b1, b2;
                double acc = 0;
                std::vector<double> edc (ir.size());
                for (size_t i = 0; i < ir.size(); ++i)
                {
                    ir.L[i] = a2.process (bp, a1.process (bp, ir.L[i]));
                    ir.R[i] = b2.process (bp, b1.process (bp, ir.R[i]));
                }
                for (size_t i = ir.size(); i-- > 0;)
                {
                    acc += (double) ir.L[i] * ir.L[i] + (double) ir.R[i] * ir.R[i];
                    edc[i] = acc;
                }
                double t5 = -1, t25 = -1;
                for (size_t i = 0; i < edc.size(); ++i)
                {
                    const double db = 10 * std::log10 (edc[i] / edc[0] + 1e-30);
                    if (t5 < 0 && db <= -5)
                        t5 = (double) i / kSR;
                    if (t25 < 0 && db <= -25)
                    {
                        t25 = (double) i / kSR;
                        break;
                    }
                }
                const double rt = 3.0 * (t25 - t5);
                worst = std::max (worst, std::fabs (rt / d - 1.0));
                row.push_back (fmt ("%.2f s", rt));
            }
            rtTable.push_back (row);
        }
        addResult ("Room", "RT60 tracks Decay 0.5..4 s (Room/Plate/Spring/Vintage)", fmt ("worst error %.0f%%", worst * 100),
                   "within +-25%", worst <= 0.25);
        addTable ("ROOM RT60 (1 kHz octave band, T20 x 3) vs. Decay setting", { "Type", "0.5 s", "1 s", "2 s", "4 s" }, rtTable);
    }
    addTable ("ROOM (defaults: size 40%, decay 0.8 s, tone 55%; mono drum loop in)",
              { "Type", "Wet L/R correlation", "Wet RMS re dry @100%", "RT60 @1 kHz octave (decay 1.0 s)" }, table);
    // gain staging across decay settings (Room type)
    {
        double lo = 1e9, hi = -1e9;
        std::string s;
        for (float d : { 0.3f, 1.0f, 3.0f, 8.0f })
        {
            auto p = defaults();
            p[kRoomDecay] = d;
            auto mod = std::make_unique<RoomModule>();
            auto out = runDirect (*mod, kSR, 1, 0, mono, p);
            Buf wl (out.size());
            for (size_t i = 0; i < out.size(); ++i)
                wl[i] = out.L[i] - mono.L[i];
            const double db = toDb (rms (wl) / rms (mono.L));
            lo = std::min (lo, db);
            hi = std::max (hi, db);
            s += fmt ("%.1fs:%+.1f ", d, db);
        }
        addResult ("Room", "wet level spread across Decay 0.3..8 s", fmt ("%.1f dB (%s)", hi - lo, s.c_str()), "<= 3 dB",
                   hi - lo <= 3.0);
    }
    // gated: energy after the gate closes
    {
        auto p = defaults();
        p[kRoomType] = (float) kRoomGated;
        p[kRoomDecay] = 0.3f;
        p[kRoomPredelay] = 0.0f;
        Stereo imp ((size_t) (2.0 * kSR));
        for (int k = 0; k < 64; ++k)
            imp.L[100 + (size_t) k] = imp.R[100 + (size_t) k] = 0.5f;
        auto mod = std::make_unique<RoomModule>();
        auto out = runDirect (*mod, kSR, 1, 0, imp, p);
        const double during = rms (out.L, (size_t) (0.05 * kSR), (size_t) (0.2 * kSR));
        const double after = rms (out.L, (size_t) (0.5 * kSR), (size_t) (1.0 * kSR));
        addResult ("Room", "Gated: tail after gate close (re during)", fmt ("%.1f dB", toDb (after / during)), "< -40 dB",
                   toDb (after / during) < -40.0);
    }
}

} // namespace tu
