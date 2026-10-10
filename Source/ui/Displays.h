// DrumSpread - reusable UI widgets: knob, parameter toggle, meters, LED, spectrum scope.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "LookAndFeel.h"

class DrumSpreadProcessor;

namespace dsui
{
/** Rotary knob bound to a parameter, with its label underneath and the value
    printed inside the knob. */
class Knob : public juce::Component
{
public:
    Knob (DrumSpreadProcessor& p, int paramIndex, juce::Colour accent, const juce::String& label = {});
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
    int paramIndex;

private:
    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Toggle for a 0/1 parameter (bool or two-choice), optionally inverted
    (e.g. a power button that shows "active" for bypass = 0). */
class ParamToggle : public juce::ToggleButton
{
public:
    ParamToggle (DrumSpreadProcessor& p, int paramIndex, const juce::String& buttonText, juce::Colour accent, bool invert = false);
    /** Pulls the parameter value into the button (call from a UI timer). */
    void sync();

private:
    void clicked() override;
    DrumSpreadProcessor& proc;
    int index;
    bool inverted;
};

/** Choice parameter combo box. */
class ParamCombo : public juce::ComboBox
{
public:
    ParamCombo (DrumSpreadProcessor& p, int paramIndex);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Stereo level meter (dBFS) with peak hold readout. */
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (juce::String meterTitle) : title (std::move (meterTitle)) {}
    void setLevels (float l, float r);
    void paint (juce::Graphics&) override;

private:
    juce::String title;
    float dbL = -100.0f, dbR = -100.0f, peakDb = -100.0f;
    int peakHold = 0;
};

/** Trigger LED: lights when the trigger counter changes, then decays. */
class TriggerLed : public juce::Component
{
public:
    explicit TriggerLed (juce::Colour c) : colour (c) {}
    void update (uint32_t count);
    void paint (juce::Graphics&) override;

private:
    juce::Colour colour;
    uint32_t last = 0;
    float glow = 0.0f;
};

/** Horizontal gain-reduction meter. */
class GrMeter : public juce::Component
{
public:
    void setGr (float db);
    void paint (juce::Graphics&) override;

private:
    float gr = 0.0f;
};

/** Output spectrum (log frequency) with the recent waveform behind it. */
class SpectrumScope : public juce::Component
{
public:
    SpectrumScope();
    /** Pull the newest samples from the audio-thread FIFO and recompute (UI timer). */
    void update (const float* latest, int n);
    void paint (juce::Graphics&) override;
    static constexpr int kOrder = 11, kSize = 1 << kOrder;

private:
    juce::dsp::FFT fft { kOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 2 * kSize> work {};
    static constexpr int kPoints = 180;
    std::array<float, kPoints> smoothedDb {};
    std::array<float, kSize> wave {};
    double sampleRate = 48000.0;

public:
    void setSampleRate (double sr) { sampleRate = sr > 0 ? sr : 48000.0; }
};

} // namespace dsui
