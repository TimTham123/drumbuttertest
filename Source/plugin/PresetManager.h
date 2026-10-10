// DrumSpread - preset browsing (factory + user XML presets) and A/B compare.
// Message-thread only.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class DrumSpreadProcessor;

class PresetManager : public juce::ChangeBroadcaster
{
public:
    explicit PresetManager (DrumSpreadProcessor& p);

    struct Entry
    {
        juce::String name, category;
        bool factory = true;
        int factoryIndex = -1;
        juce::File file;
    };

    /** Factory presets followed by user presets (rescanned on demand). */
    const juce::Array<Entry>& entries() const noexcept { return list; }
    void rescanUserPresets();
    juce::StringArray categories() const;

    void loadFactory (int factoryIndex);
    bool loadEntry (int entryIndex);
    void next();
    void previous();
    /** Saves the current state as a user preset. Returns false on I/O failure. */
    bool saveUserPreset (const juce::String& name, const juce::String& category = "User");
    bool loadUserPresetFile (const juce::File& f);

    int currentEntry() const noexcept { return current; }
    int currentFactoryIndex() const noexcept;
    juce::String currentName() const;
    juce::String currentCategory() const;

    // A/B compare
    int activeSlot() const noexcept { return abActive; }
    /** Stores the current settings in the active slot and recalls the other slot
        (the first switch to an empty slot copies the current settings into it). */
    void toggleAB();
    void selectSlot (int slot);
    void copyActiveToOther();

    // persistence of browser/A-B state inside the plugin state
    void writeToState (juce::ValueTree& st) const;
    void readFromState (juce::ValueTree& st);

    static juce::File userPresetDirectory();
    static constexpr const char* kExtension = ".dspreset";

private:
    juce::ValueTree captureParams() const;
    void applyParams (const juce::ValueTree& paramsTree);

    DrumSpreadProcessor& proc;
    juce::Array<Entry> list;
    int current = 0;
    juce::ValueTree slots[2];
    int abActive = 0;
};
