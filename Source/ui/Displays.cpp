#include "Displays.h"

#include "PluginProcessor.h"

namespace dsui
{
//==============================================================================
Knob::Knob (DrumSpreadProcessor& p, int idx, juce::Colour accent, const juce::String& lbl) : paramIndex (idx)
{
    const auto& spec = ds::paramSpec (idx);
    label = lbl.isNotEmpty() ? lbl : juce::String (spec.name);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.getProperties().set ("accent", (juce::int64) accent.getARGB());
    slider.setTooltip (juce::String (spec.name) + " - double-click to reset, drag to change (Shift = fine)");
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.state(), spec.id, slider);
    slider.setDoubleClickReturnValue (true, (double) spec.defV);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (220);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    const int labelH = juce::jlimit (11, 18, getHeight() / 5);
    r.removeFromBottom (labelH);
    const int s = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (s, s));
}

void Knob::paint (juce::Graphics& g)
{
    const int labelH = juce::jlimit (11, 18, getHeight() / 5);
    g.setColour (Colours::textDim);
    g.setFont (font ((float) labelH * 0.78f, true));
    g.drawFittedText (label.toUpperCase(), getLocalBounds().removeFromBottom (labelH), juce::Justification::centred, 1, 0.6f);
}

//==============================================================================
ParamToggle::ParamToggle (DrumSpreadProcessor& p, int idx, const juce::String& buttonText, juce::Colour accent, bool invert)
    : juce::ToggleButton (buttonText), proc (p), index (idx), inverted (invert)
{
    getProperties().set ("accent", (juce::int64) accent.getARGB());
    setClickingTogglesState (false);
    setTooltip (ds::paramSpec (idx).name);
    sync();
}

void ParamToggle::sync()
{
    const bool on = (proc.getParameterValue (index) > 0.5f) != inverted;
    if (on != getToggleState())
        setToggleState (on, juce::dontSendNotification);
}

void ParamToggle::clicked()
{
    const bool nowOn = ! getToggleState();
    const float v = (nowOn != inverted) ? 1.0f : 0.0f;
    if (auto* prm = proc.parameter (index))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        prm->endChangeGesture();
    }
    setToggleState (nowOn, juce::dontSendNotification);
}

//==============================================================================
ParamCombo::ParamCombo (DrumSpreadProcessor& p, int idx)
{
    const auto names = ds::choiceNames (idx);
    for (size_t i = 0; i < names.size(); ++i)
        addItem (names[i], (int) i + 1);
    setTooltip (ds::paramSpec (idx).name);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state(), ds::paramSpec (idx).id, *this);
}

//==============================================================================
void LevelMeter::setLevels (float l, float r)
{
    auto toDb = [] (float v) { return v > 1.0e-6f ? 20.0f * std::log10 (v) : -120.0f; };
    const float nl = toDb (l), nr = toDb (r);
    dbL = nl > dbL ? nl : dbL - 1.5f;
    dbR = nr > dbR ? nr : dbR - 1.5f;
    const float pk = juce::jmax (nl, nr);
    if (pk >= peakDb || --peakHold <= 0)
    {
        peakDb = pk;
        peakHold = 45;
    }
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float titleH = juce::jlimit (10.0f, 16.0f, r.getHeight() * 0.09f);
    g.setColour (Colours::textDim);
    g.setFont (font (titleH * 0.9f, true));
    g.drawText (title, r.removeFromTop (titleH).toNearestInt(), juce::Justification::centred);
    auto readout = r.removeFromBottom (titleH);
    g.drawText (peakDb > -99.0f ? juce::String (peakDb, 1) : juce::String ("-inf"), readout.toNearestInt(), juce::Justification::centred);
    r = r.reduced (2.0f, 2.0f);
    const float bw = (r.getWidth() - 3.0f) * 0.5f;
    auto norm = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f); };
    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = juce::Rectangle<float> (r.getX() + (float) ch * (bw + 3.0f), r.getY(), bw, r.getHeight());
        g.setColour (Colours::well);
        g.fillRoundedRectangle (bar, 2.0f);
        const float v = norm (ch == 0 ? dbL : dbR);
        auto fill = bar.withTop (bar.getBottom() - bar.getHeight() * v);
        juce::ColourGradient grad (Colours::room, bar.getBottomLeft(), Colours::press, bar.getTopLeft(), false);
        grad.addColour (0.85, Colours::layer);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 2.0f);
    }
    // 0 dB tick
    const float y0 = r.getBottom() - r.getHeight() * norm (0.0f);
    g.setColour (Colours::text.withAlpha (0.5f));
    g.drawHorizontalLine ((int) y0, r.getX(), r.getRight());
}

//==============================================================================
void TriggerLed::update (uint32_t count)
{
    if (count != last)
    {
        last = count;
        glow = 1.0f;
    }
    else
        glow *= 0.75f;
    repaint();
}

void TriggerLed::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.8f;
    auto c = r.withSizeKeepingCentre (s, s);
    g.setColour (Colours::well);
    g.fillEllipse (c);
    g.setColour (colour.withAlpha (0.15f + 0.85f * glow));
    g.fillEllipse (c.reduced (s * 0.15f));
}

//==============================================================================
void GrMeter::setGr (float db)
{
    gr = db > gr ? db : gr * 0.85f;
    repaint();
}

void GrMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::textDim);
    g.setFont (font (juce::jlimit (9.0f, 13.0f, r.getHeight() * 0.8f), true));
    auto lab = r.removeFromLeft (r.getHeight() * 1.8f);
    g.drawText ("GR", lab.toNearestInt(), juce::Justification::centredLeft);
    auto txt = r.removeFromRight (r.getHeight() * 3.2f);
    g.drawText (juce::String (gr, 1) + " dB", txt.toNearestInt(), juce::Justification::centredRight);
    r = r.reduced (2.0f, r.getHeight() * 0.25f);
    g.setColour (Colours::well);
    g.fillRoundedRectangle (r, 2.0f);
    const float v = juce::jlimit (0.0f, 1.0f, gr / 24.0f);
    g.setColour (Colours::press);
    g.fillRoundedRectangle (r.withWidth (r.getWidth() * v), 2.0f);
}

//==============================================================================
SpectrumScope::SpectrumScope() { smoothedDb.fill (-90.0f); }

void SpectrumScope::update (const float* latest, int n)
{
    n = juce::jmin (n, kSize);
    std::fill (work.begin(), work.end(), 0.0f);
    std::copy (latest, latest + n, work.begin());
    std::copy (latest, latest + n, wave.begin());
    window.multiplyWithWindowingTable (work.data(), (size_t) kSize);
    fft.performFrequencyOnlyForwardTransform (work.data());
    const float norm = 4.0f / (float) kSize; // Hann coherent gain 0.5
    const double binHz = sampleRate / kSize;
    for (int i = 0; i < kPoints; ++i)
    {
        // log-spaced display points, each averaging the bins within +-1/24 octave
        const double f = 20.0 * std::pow (1000.0, (double) i / (kPoints - 1));
        const int k0 = juce::jlimit (1, kSize / 2 - 1, (int) std::floor (f * std::pow (2.0, -1.0 / 24.0) / binHz));
        const int k1 = juce::jlimit (k0, kSize / 2 - 1, (int) std::ceil (f * std::pow (2.0, 1.0 / 24.0) / binHz));
        float pw = 0.0f;
        for (int k = k0; k <= k1; ++k)
            pw += work[(size_t) k] * work[(size_t) k];
        pw /= (float) (k1 - k0 + 1);
        const float db = 10.0f * std::log10 (juce::jmax (1.0e-14f, pw * norm * norm));
        auto& sd = smoothedDb[(size_t) i];
        sd = db > sd ? 0.5f * (sd + db) : sd - 1.0f;
    }
    repaint();
}

void SpectrumScope::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::well);
    g.fillRoundedRectangle (r, 6.0f);
    r = r.reduced (6.0f, 4.0f);
    // grid: octave lines and labels
    g.setFont (font (10.0f));
    const float fMin = 20.0f, fMax = 20000.0f;
    auto xOf = [&] (float f) { return r.getX() + r.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xOf (f);
        g.setColour (Colours::track.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (Colours::textDim.withAlpha (0.7f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x + 2.0f, r.getBottom() - 12.0f, 30.0f, 12.0f).toNearestInt(),
                    juce::Justification::left);
    }
    // waveform (behind)
    {
        juce::Path w;
        const int n = kSize;
        for (int i = 0; i < n; i += 2)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) (n - 1);
            const float y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, wave[(size_t) i]) * r.getHeight() * 0.45f;
            if (i == 0)
                w.startNewSubPath (x, y);
            else
                w.lineTo (x, y);
        }
        g.setColour (Colours::master.withAlpha (0.18f));
        g.strokePath (w, juce::PathStrokeType (1.0f));
    }
    // spectrum (display tilt +3 dB/oct around 1 kHz so drum mixes read flat)
    juce::Path s;
    bool started = false;
    for (int i = 0; i < kPoints; ++i)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) i / (float) (kPoints - 1));
        const float db = juce::jlimit (-90.0f, 0.0f, smoothedDb[(size_t) i] + 3.0f * std::log2 (f / 1000.0f));
        const float x = xOf (f);
        const float y = r.getBottom() - r.getHeight() * (db + 90.0f) / 90.0f;
        if (! started)
        {
            s.startNewSubPath (x, y);
            started = true;
        }
        else
            s.lineTo (x, y);
    }
    if (started)
    {
        auto fill = s;
        fill.lineTo (r.getRight(), r.getBottom());
        fill.lineTo (r.getX(), r.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Colours::master.withAlpha (0.35f), r.getX(), r.getY(),
                                                 Colours::warp.withAlpha (0.05f), r.getX(), r.getBottom(), false));
        g.fillPath (fill);
        g.setColour (Colours::master);
        g.strokePath (s, juce::PathStrokeType (1.5f));
    }
}

} // namespace dsui
