#include "PluginProcessor.h"

#include "FactoryPresets.h"
#include "PluginEditor.h"
#include "PresetManager.h"

namespace
{
juce::String formatValue (int index, float v)
{
    const auto& s = ds::paramSpec (index);
    const juce::String unit (s.unit);
    const juce::String id (s.id);
    if ((id.endsWith ("_focus_lo") && v <= 20.5f) || (id.endsWith ("_focus_hi") && v >= 19500.0f)
        || (id == "master_hpf" && v <= 10.5f) || (id == "master_lpf" && v >= 21500.0f)
        || (id == "press_sc_hpf" && v <= 20.5f))
        return "Off";
    if (id.endsWith ("_target"))
    {
        if (std::abs (v) < 0.5f)
            return "Even";
        return (v < 0 ? "Trans " : "Sus ") + juce::String (std::abs (v), 0);
    }
    if (unit == "dB")
        return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
    if (unit == "Hz")
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (v, 0) + " Hz";
    if (unit == "ms")
        return juce::String (v, v < 10.0f ? 1 : 0) + " ms";
    if (unit == "s")
        return juce::String (v, 2) + " s";
    if (unit == "st")
        return (v > 0.0f ? "+" : "") + juce::String (v, 2) + " st";
    if (unit == "%")
        return juce::String (v, 0) + "%";
    return juce::String (v, 2);
}

float parseValue (int index, const juce::String& text)
{
    const auto& s = ds::paramSpec (index);
    const auto t = text.trim().toLowerCase();
    if (t == "off")
        return (juce::String (s.id).endsWith ("_hi") || juce::String (s.id) == "master_lpf") ? s.maxV : s.minV;
    float v = t.getFloatValue();
    if (t.contains ("khz"))
        v *= 1000.0f;
    if (t.startsWith ("trans"))
        v = -std::abs (t.fromFirstOccurrenceOf (" ", false, false).getFloatValue());
    else if (t.startsWith ("sus"))
        v = std::abs (t.fromFirstOccurrenceOf (" ", false, false).getFloatValue());
    return juce::jlimit (s.minV, s.maxV, v);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout DrumSpreadProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < ds::kNumParams; ++i)
    {
        const auto& s = ds::paramSpec (i);
        const juce::ParameterID pid { s.id, 1 };
        switch (s.type)
        {
            case ds::PType::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, s.name, s.defV > 0.5f));
                break;
            case ds::PType::Choice:
            {
                juce::StringArray choices;
                for (const auto& c : ds::choiceNames (i))
                    choices.add (c);
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, choices, (int) s.defV));
                break;
            }
            case ds::PType::Float:
            default:
            {
                juce::NormalisableRange<float> range (s.minV, s.maxV, s.step);
                if (s.centre > s.minV && s.centre < s.maxV)
                    range.setSkewForCentre (s.centre);
                auto attrs = juce::AudioParameterFloatAttributes()
                                 .withLabel (s.unit)
                                 .withStringFromValueFunction ([i] (float v, int) { return formatValue (i, v); })
                                 .withValueFromStringFunction ([i] (const juce::String& t) { return parseValue (i, t); });
                layout.add (std::make_unique<juce::AudioParameterFloat> (pid, s.name, range, s.defV, attrs));
                break;
            }
        }
    }
    return layout;
}

DrumSpreadProcessor::DrumSpreadProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DrumSpreadState", createParameterLayout())
{
    for (int i = 0; i < ds::kNumParams; ++i)
    {
        rawParams[(size_t) i] = apvts.getRawParameterValue (ds::paramSpec (i).id);
        params[(size_t) i] = apvts.getParameter (ds::paramSpec (i).id);
        jassert (rawParams[(size_t) i] != nullptr && params[(size_t) i] != nullptr);
        paramValues[i] = rawParams[(size_t) i]->load();
    }
    presetManager = std::make_unique<PresetManager> (*this);
    presetManager->loadFactory (ds::defaultPresetIndex()); // initial "Golden Spread"
    engine.setParameters (paramValues);
    engine.prepare (currentSampleRate, 512);
    setLatencySamples (requiredLatency());
    startTimerHz (10);
}

DrumSpreadProcessor::~DrumSpreadProcessor() { stopTimer(); }

int DrumSpreadProcessor::requiredLatency() const
{
    return ds::Engine::latencyFor (currentSampleRate, (int) rawParams[ds::kOversampling]->load(),
                                   (int) rawParams[ds::kWarpMode]->load());
}

void DrumSpreadProcessor::updateLatency()
{
    const int l = requiredLatency();
    if (l != getLatencySamples())
        setLatencySamples (l);
}

void DrumSpreadProcessor::timerCallback() { updateLatency(); }

void DrumSpreadProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    for (int i = 0; i < ds::kNumParams; ++i)
        paramValues[i] = rawParams[(size_t) i]->load();
    engine.setParameters (paramValues);
    engine.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
    engine.setParameters (paramValues);
    engine.reset();
    setLatencySamples (requiredLatency());
}

void DrumSpreadProcessor::releaseResources() {}

bool DrumSpreadProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo() || in.isDisabled();
}

void DrumSpreadProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    if (n <= 0 || buffer.getNumChannels() < 2)
        return;
    for (int i = 0; i < ds::kNumParams; ++i)
        paramValues[i] = rawParams[(size_t) i]->load (std::memory_order_relaxed);
    engine.setParameters (paramValues);

    if (numIn == 0)
        buffer.clear();
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (1);
    const float* inL = outL;
    const float* inR = numIn >= 2 ? outR : nullptr; // mono input: the engine duplicates L
    engine.process (inL, inR, outL, outR, n);
}

juce::AudioProcessorEditor* DrumSpreadProcessor::createEditor() { return new DrumSpreadEditor (*this); }

int DrumSpreadProcessor::getNumPrograms() { return (int) ds::factoryPresets().size(); }
int DrumSpreadProcessor::getCurrentProgram() { return juce::jmax (0, presetManager->currentFactoryIndex()); }
void DrumSpreadProcessor::setCurrentProgram (int index)
{
    if (index >= 0 && index < getNumPrograms())
        presetManager->loadFactory (index);
}
const juce::String DrumSpreadProcessor::getProgramName (int index)
{
    const auto& p = ds::factoryPresets();
    return (index >= 0 && index < (int) p.size()) ? juce::String (p[(size_t) index].category + " / " + p[(size_t) index].name)
                                                  : juce::String();
}

void DrumSpreadProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    presetManager->writeToState (st);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, destData);
}

void DrumSpreadProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto st = juce::ValueTree::fromXml (*xml);
            presetManager->readFromState (st);
            apvts.replaceState (st);
            // APVTS only pushes values whose *denormalised* value changed; a bool that
            // was set to e.g. 0.49 (= false) would keep reporting 0.49 after restoring
            // a saved 0. Re-sync every parameter's normalised value to the state.
            for (int i = 0; i < ds::kNumParams; ++i)
            {
                auto* prm = params[(size_t) i];
                const auto child = st.getChildWithProperty ("id", ds::paramSpec (i).id);
                const float real = child.isValid() ? (float) (double) child.getProperty ("value", ds::paramSpec (i).defV)
                                                   : ds::paramSpec (i).defV;
                const float target = prm->convertTo0to1 (ds::sanitize (i, real));
                if (std::abs (prm->getValue() - target) > 1.0e-6f)
                    prm->setValueNotifyingHost (target);
            }
        }
    }
}

juce::AudioProcessorParameter* DrumSpreadProcessor::getBypassParameter() const { return params[ds::kBypass]; }

void DrumSpreadProcessor::setParameterValue (int index, float realValue)
{
    if (auto* p = params[(size_t) index])
        p->setValueNotifyingHost (p->convertTo0to1 (ds::sanitize (index, realValue)));
}

#ifndef DS_PLUGIN_TESTS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DrumSpreadProcessor(); }
#endif
