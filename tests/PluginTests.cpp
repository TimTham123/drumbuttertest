// DrumSpread plugin-level tests (JUCE): parameters, state, presets, A/B, bus layouts,
// latency reporting, host bypass, RT allocations in processBlock, and UI snapshots.
//
// Usage: plugin_tests [outputDir]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Engine.h"
#include "FactoryPresets.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PresetManager.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace
{
std::atomic<bool> gCount { false };
std::atomic<uint64_t> gAllocs { 0 };

struct Result
{
    juce::String test, measured, criterion;
    bool pass;
};
std::vector<Result> results;

void check (const juce::String& test, const juce::String& measured, const juce::String& criterion, bool pass)
{
    results.push_back ({ test, measured, criterion, pass });
    std::printf ("[%s] %-60s %s (%s)\n", pass ? "PASS" : "FAIL", test.toRawUTF8(), measured.toRawUTF8(), criterion.toRawUTF8());
}

void processBlocks (DrumSpreadProcessor& p, juce::AudioBuffer<float>& io, int block)
{
    juce::MidiBuffer midi;
    for (int pos = 0; pos < io.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, io.getNumSamples() - pos);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), io.getNumChannels(), pos, n);
        p.processBlock (view, midi);
    }
}

juce::AudioBuffer<float> drums (int n, int ch, double sr)
{
    juce::AudioBuffer<float> b (ch, n);
    juce::Random r (7);
    for (int i = 0; i < n; ++i)
    {
        const double t = std::fmod ((double) i / sr, 0.5);
        float v = (float) (0.7 * std::exp (-t * 18.0) * std::sin (2 * juce::MathConstants<double>::pi * (50.0 + 90.0 * std::exp (-t * 30.0)) * t));
        if (t > 0.25 && t < 0.4)
            v += 0.3f * (r.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-(t - 0.25) * 25.0);
        for (int c = 0; c < ch; ++c)
            b.setSample (c, i, v);
    }
    return b;
}

void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
} // namespace

void* operator new (std::size_t n)
{
    if (gCount.load (std::memory_order_relaxed))
        gAllocs.fetch_add (1);
    if (void* p = std::malloc (n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                      : juce::File::getCurrentWorkingDirectory().getChildFile ("test_output"));
    outDir.createDirectory();
    const double sr = 48000.0;

    // ---------------------------------------------------------------- parameters
    {
        DrumSpreadProcessor p;
        const auto& ps = p.getParameters();
        check ("parameter count == table", juce::String (ps.size()), juce::String (ds::kNumParams), ps.size() == ds::kNumParams);
        int automatable = 0;
        for (auto* prm : ps)
            automatable += prm->isAutomatable() ? 1 : 0;
        check ("all parameters automatable", juce::String (automatable), "all", automatable == ps.size());
        check ("chain order is a host parameter", p.parameter (ds::kChainOrder)->getName (64), "\"Chain Order\"",
               p.parameter (ds::kChainOrder)->getNumSteps() == ds::kNumChainOrders);
        // initial preset
        std::vector<float> expect (ds::kNumParams);
        ds::applyPreset (ds::factoryPresets()[(size_t) ds::defaultPresetIndex()], expect.data());
        int mismatches = 0;
        for (int i = 0; i < ds::kNumParams; ++i)
            if (std::abs (p.getParameterValue (i) - expect[(size_t) i]) > 1e-3f * (1.0f + std::abs (expect[(size_t) i])))
                ++mismatches;
        check ("initial state == \"Golden Spread\" preset", juce::String (mismatches) + " mismatches", "0", mismatches == 0);
        check ("bypass parameter exposed to host", p.getBypassParameter() != nullptr ? "yes" : "no", "yes", p.getBypassParameter() == p.parameter (ds::kBypass));
    }

    // ---------------------------------------------------------------- bus layouts
    {
        DrumSpreadProcessor p;
        juce::AudioProcessor::BusesLayout stereo, mono, surround;
        stereo.inputBuses.add (juce::AudioChannelSet::stereo());
        stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::stereo());
        surround.inputBuses.add (juce::AudioChannelSet::stereo());
        surround.outputBuses.add (juce::AudioChannelSet::create5point1());
        check ("layouts: stereo->stereo, mono->stereo accepted; 5.1 out rejected",
               juce::String (p.checkBusesLayoutSupported (stereo) ? "S " : "- ") + (p.checkBusesLayoutSupported (mono) ? "M " : "- ")
                   + (p.checkBusesLayoutSupported (surround) ? "5.1" : "-"),
               "S M -", p.checkBusesLayoutSupported (stereo) && p.checkBusesLayoutSupported (mono) && ! p.checkBusesLayoutSupported (surround));

        // mono input processing
        p.setBusesLayout (mono);
        p.prepareToPlay (sr, 512);
        auto buf = drums (48000, 2, sr);
        buf.clear (1, 0, buf.getNumSamples());
        processBlocks (p, buf, 512);
        bool finite = true;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                finite = finite && std::isfinite (buf.getSample (c, i));
        const float rmsR = buf.getRMSLevel (1, 0, buf.getNumSamples());
        check ("mono input -> stereo output (right channel populated)", "R rms " + juce::String (rmsR, 3), "> 0.01, finite", finite && rmsR > 0.01f);
    }

    // ---------------------------------------------------------------- latency
    {
        DrumSpreadProcessor p;
        p.prepareToPlay (sr, 512);
        const int l0 = p.getLatencySamples();
        const int e0 = ds::Engine::latencyFor (sr, (int) p.getParameterValue (ds::kOversampling), (int) p.getParameterValue (ds::kWarpMode));
        p.setParameterValue (ds::kOversampling, 2.0f);
        p.setParameterValue (ds::kWarpMode, 1.0f);
        pump (400); // message-thread timer updates the reported latency
        const int l1 = p.getLatencySamples();
        const int e1 = ds::Engine::latencyFor (sr, 2, 1);
        check ("reported latency follows OS / WARP mode", juce::String (l0) + " -> " + juce::String (l1),
               juce::String (e0) + " -> " + juce::String (e1), l0 == e0 && l1 == e1);
        p.prepareToPlay (96000.0, 256);
        check ("latency at 96 kHz", juce::String (p.getLatencySamples()), juce::String (ds::Engine::latencyFor (96000.0, 2, 1)),
               p.getLatencySamples() == ds::Engine::latencyFor (96000.0, 2, 1));
    }

    // ---------------------------------------------------------------- host bypass compensation
    {
        DrumSpreadProcessor p;
        p.prepareToPlay (sr, 512);
        p.setParameterValue (ds::kBypass, 1.0f);
        p.prepareToPlay (sr, 512); // start fully bypassed
        auto in = drums (24000, 2, sr);
        auto out = in;
        processBlocks (p, out, 300);
        const int L = p.getLatencySamples();
        double maxd = 0;
        for (int i = L; i < in.getNumSamples(); ++i)
            maxd = juce::jmax (maxd, (double) std::abs (out.getSample (0, i) - in.getSample (0, i - L)));
        check ("host bypass output == input delayed by reported latency", juce::String (maxd, 9), "< 1e-6", maxd < 1e-6);
    }

    // ---------------------------------------------------------------- RT allocations in processBlock
    {
        DrumSpreadProcessor p;
        p.prepareToPlay (sr, 512);
        auto buf = drums (48000 * 4, 2, sr);
        juce::MidiBuffer midi;
        uint64_t allocs = 0;
        int blk = 0;
        for (int pos = 0; pos + 512 <= buf.getNumSamples(); pos += 512, ++blk)
        {
            if (blk % 25 == 0) // parameter changes from the "host" between blocks
            {
                p.setParameterValue (ds::kChainOrder, (float) (blk / 25 % ds::kNumChainOrders));
                p.setParameterValue (ds::kHeatMode, (float) (blk / 25 % ds::kNumHeatModes));
                p.setParameterValue (ds::kOversampling, (float) (blk / 25 % 3));
                p.setParameterValue (ds::kRoomType, (float) (blk / 25 % ds::kNumRoomTypes));
            }
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, 512);
            gCount = true;
            const auto before = gAllocs.load();
            p.processBlock (view, midi);
            allocs += gAllocs.load() - before;
            gCount = false;
        }
        check ("heap allocations inside processBlock()", juce::String ((juce::int64) allocs), "0", allocs == 0);
    }

    // ---------------------------------------------------------------- state save / restore
    {
        DrumSpreadProcessor a;
        juce::Random rnd (42);
        for (int i = 0; i < ds::kNumParams; ++i)
        {
            const auto& s = ds::paramSpec (i);
            a.setParameterValue (i, s.minV + rnd.nextFloat() * (s.maxV - s.minV));
        }
        a.setParameterValue (ds::kChainOrder, 17.0f);
        a.presets().selectSlot (1); // make slot B active (slot A stored)
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        DrumSpreadProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        int diff = 0;
        for (int i = 0; i < ds::kNumParams; ++i)
            if (std::abs (a.parameter (i)->getValue() - b.parameter (i)->getValue()) > 1e-6f)
                ++diff;
        check ("state save/restore: every parameter (incl. chain order)", juce::String (diff) + " differences (" + juce::String ((int) mb.getSize()) + " bytes)",
               "0", diff == 0);
        check ("state save/restore: chain order", juce::String (b.getParameterValue (ds::kChainOrder)), "17",
               (int) b.getParameterValue (ds::kChainOrder) == 17);
        check ("state save/restore: A/B slot and preset name", "slot " + juce::String (b.presets().activeSlot()) + ", " + b.presets().currentName(),
               "slot 1, " + a.presets().currentName(), b.presets().activeSlot() == 1 && b.presets().currentName() == a.presets().currentName());
    }

    // ---------------------------------------------------------------- presets
    {
        DrumSpreadProcessor p;
        int bad = 0;
        for (int k = 0; k < p.getNumPrograms(); ++k)
        {
            p.setCurrentProgram (k);
            std::vector<float> expect (ds::kNumParams);
            ds::applyPreset (ds::factoryPresets()[(size_t) k], expect.data());
            for (int i = 0; i < ds::kNumParams; ++i)
                if (std::abs (p.getParameterValue (i) - expect[(size_t) i]) > 1e-3f * (1.0f + std::abs (expect[(size_t) i])))
                {
                    ++bad;
                    break;
                }
        }
        check ("all factory presets load exactly (host programs)", juce::String (p.getNumPrograms() - bad) + "/" + juce::String (p.getNumPrograms()),
               "all", bad == 0);
        // next / previous
        p.setCurrentProgram (0);
        p.presets().next();
        const int afterNext = p.presets().currentEntry();
        p.presets().previous();
        p.presets().previous();
        const int wrapped = p.presets().currentEntry();
        check ("next / previous (wraps around)", juce::String (afterNext) + ", " + juce::String (wrapped),
               "1, " + juce::String (p.presets().entries().size() - 1), afterNext == 1 && wrapped == p.presets().entries().size() - 1);
        // user preset round trip
        p.setCurrentProgram (5);
        p.setParameterValue (ds::kHeatDrive, 21.5f);
        const juce::String name = "zz_harness_test_" + juce::String (juce::Time::currentTimeMillis());
        const bool saved = p.presets().saveUserPreset (name);
        p.setCurrentProgram (0);
        bool loaded = false;
        for (int e = 0; e < p.presets().entries().size(); ++e)
            if (p.presets().entries()[e].name == name)
                loaded = p.presets().loadEntry (e);
        const float drive = p.getParameterValue (ds::kHeatDrive);
        check ("user preset save -> browse -> load (XML file)", juce::String (saved && loaded ? "ok" : "failed") + ", drive " + juce::String (drive, 2),
               "ok, drive 21.50", saved && loaded && std::abs (drive - 21.5f) < 0.01f);
        PresetManager::userPresetDirectory().getChildFile (name + PresetManager::kExtension).deleteFile();
        // A/B
        p.setCurrentProgram (0);
        p.presets().selectSlot (0);
        p.setParameterValue (ds::kHeatDrive, 3.0f);
        p.presets().selectSlot (1); // B starts as a copy
        p.setParameterValue (ds::kHeatDrive, 30.0f);
        p.presets().selectSlot (0);
        const float aVal = p.getParameterValue (ds::kHeatDrive);
        p.presets().selectSlot (1);
        const float bVal = p.getParameterValue (ds::kHeatDrive);
        check ("A/B compare recalls each slot", "A " + juce::String (aVal, 1) + " / B " + juce::String (bVal, 1), "A 3.0 / B 30.0",
               std::abs (aVal - 3.0f) < 0.01f && std::abs (bVal - 30.0f) < 0.01f);
    }

    // ---------------------------------------------------------------- editor
    {
        DrumSpreadProcessor p;
        p.prepareToPlay (sr, 512);
        auto buf = drums (48000, 2, sr);
        processBlocks (p, buf, 512); // give the meters / scope something to show
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* e = dynamic_cast<DrumSpreadEditor*> (ed.get());
        check ("editor created, default size", juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()), "1280x760",
               e != nullptr && ed->getWidth() == 1280 && ed->getHeight() == 760);
        pump (200);
        processBlocks (p, buf, 512);
        pump (200);
        bool snapOk = true;
        for (auto sz : { juce::Point<int> (1280, 760), juce::Point<int> (980, 620), juce::Point<int> (1920, 1140) })
        {
            ed->setSize (sz.x, sz.y);
            pump (120);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile ("ui_" + juce::String (sz.x) + "x" + juce::String (sz.y) + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat png;
            snapOk = snapOk && img.isValid() && png.writeImageToStream (img, os);
        }
        check ("UI snapshots at 1280x760, 980x620, 1920x1140 (resizable)", snapOk ? "written" : "failed", "PNG files", snapOk);
        // order change via the parameter is reflected in the panel layout
        p.setParameterValue (ds::kChainOrder, 23.0f);
        pump (200);
        const auto shown = e->displayedOrder();
        check ("editor panels follow the chain-order parameter", juce::String (shown[0]) + juce::String (shown[1]) + juce::String (shown[2]) + juce::String (shown[3]),
               "3210", shown == ds::chainPermutation (23));
        ed->setSize (1280, 760);
        pump (120);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = outDir.getChildFile ("ui_reordered_1280x760.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
    }

    // ---------------------------------------------------------------- report
    int fails = 0;
    juce::String md = "\n## Plugin-level tests (`plugin_tests`, JUCE)\n\n| Test | Measured | Criterion | Result |\n|---|---|---|---|\n";
    for (const auto& r : results)
    {
        md << "| " << r.test << " | " << r.measured << " | " << r.criterion << " | " << (r.pass ? "PASS" : "**FAIL**") << " |\n";
        fails += r.pass ? 0 : 1;
    }
    md << "\n**Plugin tests: " << (int) results.size() << " checks, " << fails << " failed.**\n";
    outDir.getChildFile ("PLUGIN_TESTS.md").replaceWithText (md);
    std::printf ("\n%d plugin checks, %d failed\n", (int) results.size(), fails);
    return fails == 0 ? 0 : 1;
}
