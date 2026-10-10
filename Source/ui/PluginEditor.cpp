#include "PluginEditor.h"

#include "FactoryPresets.h"
#include "PluginProcessor.h"
#include "PresetManager.h"

using namespace dsui;

DrumSpreadEditor::DrumSpreadEditor (DrumSpreadProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);

    // ---- top bar ----
    for (auto* b : { &prevBtn, &nextBtn, &presetBtn, &saveBtn, &aBtn, &bBtn, &copyBtn })
        addAndMakeVisible (*b);
    prevBtn.setTooltip ("Previous preset");
    nextBtn.setTooltip ("Next preset");
    presetBtn.setTooltip ("Browse presets");
    saveBtn.setTooltip ("Save the current settings as a user preset");
    aBtn.setTooltip ("A/B compare: slot A");
    bBtn.setTooltip ("A/B compare: slot B");
    copyBtn.setTooltip ("Copy the active slot to the other slot");
    prevBtn.onClick = [this] { proc.presets().previous(); };
    nextBtn.onClick = [this] { proc.presets().next(); };
    presetBtn.onClick = [this] { showPresetMenu(); };
    saveBtn.onClick = [this] { savePreset(); };
    aBtn.onClick = [this] { proc.presets().selectSlot (0); };
    bBtn.onClick = [this] { proc.presets().selectSlot (1); };
    copyBtn.onClick = [this] { proc.presets().copyActiveToOther(); };
    for (auto* b : { &aBtn, &bBtn })
        b->getProperties().set ("accent", (juce::int64) Colours::master.getARGB());

    osCombo = std::make_unique<ParamCombo> (proc, ds::kOversampling);
    addAndMakeVisible (*osCombo);
    intensity = std::make_unique<Knob> (proc, ds::kIntensity, Colours::master, "Intensity");
    addAndMakeVisible (*intensity);
    bypassBtn = std::make_unique<ParamToggle> (proc, ds::kBypass, "ACTIVE", Colours::master, true);
    bypassBtn->setTooltip ("Global bypass (latency-compensated)");
    addAndMakeVisible (*bypassBtn);

    // ---- module panels ----
    for (int m = 0; m < ds::kNumModules; ++m)
    {
        panels[(size_t) m] = std::make_unique<ModulePanel> (proc, m);
        auto& pn = *panels[(size_t) m];
        addAndMakeVisible (pn);
        pn.onDragStart = [this] (ModulePanel& panel) {
            dragPanel = &panel;
            dragOrigin = panel.getBounds();
            panel.toFront (false);
        };
        pn.onDragMove = [this] (ModulePanel& panel, int dx) {
            const int minX = slotRects[0].getX(), maxX = slotRects[3].getRight() - dragOrigin.getWidth();
            panel.setTopLeftPosition (juce::jlimit (minX, maxX, dragOrigin.getX() + dx), dragOrigin.getY());
        };
        pn.onDragEnd = [this] (ModulePanel& panel) {
            const int cx = panel.getBounds().getCentreX();
            int best = 0;
            for (int s = 1; s < 4; ++s)
                if (std::abs (slotRects[(size_t) s].getCentreX() - cx) < std::abs (slotRects[(size_t) best].getCentreX() - cx))
                    best = s;
            const int type = panel.moduleId() - 1;
            std::array<int, 4> ord = shownOrderArr;
            std::vector<int> v (ord.begin(), ord.end());
            v.erase (std::find (v.begin(), v.end(), type));
            v.insert (v.begin() + best, type);
            std::copy (v.begin(), v.end(), ord.begin());
            dragPanel = nullptr;
            setOrder (ord);
            layoutChain();
        };
        pn.onMove = [this] (ModulePanel& panel, int dir) {
            const int type = panel.moduleId() - 1;
            std::array<int, 4> ord = shownOrderArr;
            const int pos = (int) (std::find (ord.begin(), ord.end(), type) - ord.begin());
            const int np = juce::jlimit (0, 3, pos + dir);
            std::swap (ord[(size_t) pos], ord[(size_t) np]);
            setOrder (ord);
            layoutChain();
        };
    }

    // ---- master strip ----
    auto mk = [this] (int param, const juce::String& label) {
        masterKnobs.push_back (std::make_unique<Knob> (proc, param, Colours::master, label));
        addAndMakeVisible (*masterKnobs.back());
    };
    mk (ds::kMasterAttack, "Attack");
    mk (ds::kMasterSustain, "Sustain");
    mk (ds::kMasterLowGain, "Low");
    mk (ds::kMasterMidGain, "Mid");
    mk (ds::kMasterHighGain, "High");
    mk (ds::kMasterAir, "Air");
    mk (ds::kOutGain, "Output");
    mk (ds::kDryWet, "Dry/Wet");
    mk (ds::kInGain, "Input");
    mk (ds::kMasterLowFreq, "Low F");
    mk (ds::kMasterMidFreq, "Mid F");
    mk (ds::kMasterHighFreq, "High F");
    mk (ds::kMasterHpf, "HPF");
    mk (ds::kMasterLpf, "LPF");
    mk (ds::kMasterCeiling, "Ceiling");
    clipBtn = std::make_unique<ParamToggle> (proc, ds::kMasterClip, "CLIP", Colours::master);
    addAndMakeVisible (*clipBtn);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (scope);
    scopeBuf.resize ((size_t) SpectrumScope::kSize);

    proc.presets().addChangeListener (this);
    updatePresetLabel();

    setResizable (true, true);
    setResizeLimits (980, 620, 2560, 1600);
    setSize (1280, 760);
    startTimerHz (30);
}

DrumSpreadEditor::~DrumSpreadEditor()
{
    stopTimer();
    proc.presets().removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void DrumSpreadEditor::changeListenerCallback (juce::ChangeBroadcaster*) { updatePresetLabel(); }

void DrumSpreadEditor::updatePresetLabel()
{
    auto& pm = proc.presets();
    presetBtn.setButtonText (pm.currentCategory() + "  /  " + pm.currentName());
    aBtn.setToggleState (pm.activeSlot() == 0, juce::dontSendNotification);
    bBtn.setToggleState (pm.activeSlot() == 1, juce::dontSendNotification);
}

void DrumSpreadEditor::setOrder (const std::array<int, 4>& order)
{
    proc.setParameterValue (ds::kChainOrder, (float) ds::chainIndexOf (order));
}

void DrumSpreadEditor::showPresetMenu()
{
    auto& pm = proc.presets();
    pm.rescanUserPresets();
    juce::PopupMenu menu;
    const auto& entries = pm.entries();
    for (const auto& cat : pm.categories())
    {
        juce::PopupMenu sub;
        for (int i = 0; i < entries.size(); ++i)
            if (entries[i].category == cat)
                sub.addItem (i + 1, entries[i].name, true, i == pm.currentEntry());
        if (sub.getNumItems() > 0)
            menu.addSubMenu (cat, sub);
    }
    menu.addSeparator();
    menu.addItem (10000, "Save current as user preset...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn), [this] (int r) {
        if (r == 10000)
            savePreset();
        else if (r > 0)
            proc.presets().loadEntry (r - 1);
    });
}

void DrumSpreadEditor::savePreset()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name for the user preset:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.presets().currentName() + " copy");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result) {
                            if (result == 1)
                                proc.presets().saveUserPreset (w->getTextEditorContents ("name"));
                        }),
                        true);
}

void DrumSpreadEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    // top bar
    g.setColour (Colours::panel.darker (0.25f));
    g.fillRect (topBar);
    // wordmark: two-weight logotype with a "spread" bar
    auto la = logoArea.toFloat();
    g.setFont (font (la.getHeight() * 0.52f, true));
    g.setColour (Colours::text);
    g.drawText ("DRUM", la.withWidth (la.getWidth() * 0.5f).toNearestInt(), juce::Justification::centredRight);
    g.setColour (Colours::master);
    g.drawText ("SPREAD", la.withTrimmedLeft (la.getWidth() * 0.5f + 2.0f).toNearestInt(), juce::Justification::centredLeft);
    const float barY = la.getBottom() - la.getHeight() * 0.18f;
    const juce::Colour mods[] = { Colours::layer, Colours::warp, Colours::room, Colours::press, Colours::heat, Colours::smash };
    const float segW = la.getWidth() * 0.8f / 6.0f;
    for (int i = 0; i < 6; ++i)
    {
        g.setColour (mods[i]);
        g.fillRoundedRectangle (la.getX() + la.getWidth() * 0.1f + (float) i * segW, barY, segW - 2.0f, 2.5f, 1.2f);
    }
    g.setColour (Colours::textDim);
    g.setFont (font (12.0f, true));
    g.drawText ("OVERSAMPLING", osLabelArea, juce::Justification::centredRight);

    // chain direction hint
    g.setColour (Colours::track);
    g.setFont (font (11.0f));
    g.drawText ("IN  >  LAYER  >  [ drag to reorder ]  >  SMASH  >  MASTER  >  OUT",
                chainArea.withHeight (14).translated (0, -15), juce::Justification::centred);

    // master panel
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (masterArea.toFloat(), 9.0f);
    g.setColour (Colours::master);
    g.fillRoundedRectangle ((float) masterArea.getX() + 10.0f, (float) masterArea.getY() + 1.5f, (float) masterArea.getWidth() - 20.0f, 2.5f, 1.25f);
    g.setColour (Colours::text);
    g.setFont (font (17.0f, true));
    g.drawText ("MASTER", masterArea.reduced (14, 6).removeFromTop (22), juce::Justification::centredLeft);
}

void DrumSpreadEditor::resized()
{
    const float sc = (float) getHeight() / 760.0f;
    const int m = juce::roundToInt (10.0f * sc);
    auto r = getLocalBounds();
    topBar = r.removeFromTop (juce::roundToInt (58.0f * sc));
    auto tb = topBar.reduced (m, juce::roundToInt (8.0f * sc));
    logoArea = tb.removeFromLeft (juce::roundToInt (230.0f * sc));
    tb.removeFromLeft (m);
    const int bh = tb.getHeight();
    prevBtn.setBounds (tb.removeFromLeft (bh).reduced (2, 4));
    presetBtn.setBounds (tb.removeFromLeft (juce::roundToInt (300.0f * sc)).reduced (2, 4));
    nextBtn.setBounds (tb.removeFromLeft (bh).reduced (2, 4));
    saveBtn.setBounds (tb.removeFromLeft (juce::roundToInt (64.0f * sc)).reduced (4, 4));
    tb.removeFromLeft (m);
    aBtn.setBounds (tb.removeFromLeft (bh).reduced (2, 4));
    bBtn.setBounds (tb.removeFromLeft (bh).reduced (2, 4));
    copyBtn.setBounds (tb.removeFromLeft (juce::roundToInt (60.0f * sc)).reduced (2, 4));
    bypassBtn->setBounds (tb.removeFromRight (juce::roundToInt (86.0f * sc)).reduced (4, 8));
    tb.removeFromRight (m);
    osCombo->setBounds (tb.removeFromRight (juce::roundToInt (70.0f * sc)).reduced (2, 6));
    osLabelArea = tb.removeFromRight (juce::roundToInt (110.0f * sc));

    r.reduce (m, m);
    const int masterH = juce::roundToInt ((float) r.getHeight() * 0.33f);
    masterArea = r.removeFromBottom (masterH);
    r.removeFromBottom (m);
    r.removeFromTop (juce::roundToInt (14.0f * sc));
    chainArea = r;
    layoutChain();

    // master contents
    auto ma = masterArea.reduced (m, m);
    ma.removeFromTop (juce::roundToInt (16.0f * sc));
    auto meters = ma.removeFromRight (juce::roundToInt (110.0f * sc));
    inMeter.setBounds (meters.removeFromLeft (meters.getWidth() / 2).reduced (3, 0));
    outMeter.setBounds (meters.reduced (3, 0));
    ma.removeFromRight (m);
    scope.setBounds (ma.removeFromRight (juce::roundToInt ((float) ma.getWidth() * 0.36f)));
    ma.removeFromRight (m);
    intensity->setBounds (ma.removeFromLeft (juce::roundToInt ((float) ma.getHeight() * 0.95f)).reduced (2));
    ma.removeFromLeft (m);
    masterKnobArea = ma;
    const int cols = 8;
    auto row1 = ma.removeFromTop (ma.getHeight() / 2);
    auto row2 = ma;
    const int cw1 = row1.getWidth() / cols;
    for (int i = 0; i < 8; ++i)
        masterKnobs[(size_t) i]->setBounds (row1.removeFromLeft (cw1).reduced (2));
    const int cw2 = row2.getWidth() / cols;
    for (int i = 8; i < 15; ++i)
        masterKnobs[(size_t) i]->setBounds (row2.removeFromLeft (cw2).reduced (2));
    clipBtn->setBounds (row2.removeFromLeft (cw2).withSizeKeepingCentre (cw2 - 8, juce::roundToInt (26.0f * sc)));
}

void DrumSpreadEditor::layoutChain()
{
    if (chainArea.isEmpty())
        return;
    const int gap = juce::jmax (6, chainArea.getWidth() / 160);
    const float unit = (float) (chainArea.getWidth() - 5 * gap) / 7.0f;
    auto r = chainArea;
    auto layerR = r.removeFromLeft (juce::roundToInt (unit * 2.0f));
    r.removeFromLeft (gap);
    for (int s = 0; s < 4; ++s)
    {
        slotRects[(size_t) s] = r.removeFromLeft (juce::roundToInt (unit));
        r.removeFromLeft (gap);
    }
    auto smashR = r;
    panels[ds::kModLayer]->setBounds (layerR);
    panels[ds::kModSmash]->setBounds (smashR);
    for (int s = 0; s < 4; ++s)
    {
        auto* pnl = panels[(size_t) ds::moduleForSlot (shownOrderArr[(size_t) s])].get();
        if (pnl != dragPanel)
            pnl->setBounds (slotRects[(size_t) s]);
    }
}

void DrumSpreadEditor::timerCallback()
{
    // chain order from the parameter (host automation, presets, undo)
    const int ordIdx = juce::roundToInt (proc.getParameterValue (ds::kChainOrder));
    if (ordIdx != shownOrderIndex && dragPanel == nullptr)
    {
        shownOrderIndex = ordIdx;
        shownOrderArr = ds::chainPermutation (ordIdx);
        layoutChain();
    }
    for (auto& pnl : panels)
        pnl->refresh();
    bypassBtn->sync();
    clipBtn->sync();
    auto& t = proc.telemetry();
    inMeter.setLevels (t.inLevel[0].load(), t.inLevel[1].load());
    outMeter.setLevels (t.outLevel[0].load(), t.outLevel[1].load());
    scope.setSampleRate (proc.getSampleRate());
    t.scope.readLatest (scopeBuf.data(), SpectrumScope::kSize);
    scope.update (scopeBuf.data(), SpectrumScope::kSize);
    updatePresetLabel();
}
