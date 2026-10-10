// DrumSpread - JUCE AudioProcessor wrapper around the dscore Engine.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine.h"
#include "Params.h"

#include <array>
#include <atomic>
#include <memory>

class PresetManager;

class DrumSpreadProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    DrumSpreadProcessor();
    ~DrumSpreadProcessor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // juce::AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DrumSpread"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    // DrumSpread API
    juce::AudioProcessorValueTreeState& state() noexcept { return apvts; }
    PresetManager& presets() noexcept { return *presetManager; }
    ds::Telemetry& telemetry() noexcept { return engine.telemetry(); }
    /** Latency the current parameters require (used by the latency timer and tests). */
    int requiredLatency() const;
    /** Applies a parameter's real value from the message thread (notifies the host). */
    void setParameterValue (int index, float realValue);
    float getParameterValue (int index) const noexcept { return rawParams[(size_t) index]->load(); }
    juce::RangedAudioParameter* parameter (int index) const noexcept { return params[(size_t) index]; }

private:
    void timerCallback() override;
    void updateLatency();

    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<float>*, ds::kNumParams> rawParams {};
    std::array<juce::RangedAudioParameter*, ds::kNumParams> params {};
    float paramValues[ds::kNumParams] {};
    ds::Engine engine;
    double currentSampleRate = 48000.0;
    std::unique_ptr<PresetManager> presetManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumSpreadProcessor)
};
