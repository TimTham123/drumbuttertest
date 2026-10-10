#include "PresetManager.h"

#include "FactoryPresets.h"
#include "PluginProcessor.h"

PresetManager::PresetManager (DrumSpreadProcessor& p) : proc (p) { rescanUserPresets(); }

juce::File PresetManager::userPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("DrumSpread")
        .getChildFile ("Presets");
}

void PresetManager::rescanUserPresets()
{
    const juce::String keepName = list.isEmpty() ? juce::String() : currentName();
    const juce::String keepCat = list.isEmpty() ? juce::String() : currentCategory();
    list.clearQuick();
    const auto& fp = ds::factoryPresets();
    for (int i = 0; i < (int) fp.size(); ++i)
        list.add ({ fp[(size_t) i].name, fp[(size_t) i].category, true, i, {} });
    const auto dir = userPresetDirectory();
    if (dir.isDirectory())
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExtension);
        files.sort();
        for (const auto& f : files)
        {
            juce::String cat = "User";
            if (auto xml = juce::XmlDocument::parse (f))
                cat = xml->getStringAttribute ("category", "User");
            list.add ({ f.getFileNameWithoutExtension(), cat, false, -1, f });
        }
    }
    current = 0;
    for (int i = 0; i < list.size(); ++i)
        if (list[i].name == keepName && list[i].category == keepCat)
            current = i;
}

juce::StringArray PresetManager::categories() const
{
    juce::StringArray c;
    for (const auto& s : ds::presetCategories())
        c.add (s);
    for (const auto& e : list)
        c.addIfNotAlreadyThere (e.category);
    return c;
}

int PresetManager::currentFactoryIndex() const noexcept
{
    return (current >= 0 && current < list.size() && list[current].factory) ? list[current].factoryIndex : -1;
}

juce::String PresetManager::currentName() const
{
    return (current >= 0 && current < list.size()) ? list[current].name : juce::String ("Init");
}

juce::String PresetManager::currentCategory() const
{
    return (current >= 0 && current < list.size()) ? list[current].category : juce::String();
}

juce::ValueTree PresetManager::captureParams() const
{
    juce::ValueTree t ("Params");
    for (int i = 0; i < ds::kNumParams; ++i)
    {
        juce::ValueTree c ("P");
        c.setProperty ("id", ds::paramSpec (i).id, nullptr);
        c.setProperty ("value", proc.getParameterValue (i), nullptr);
        t.appendChild (c, nullptr);
    }
    return t;
}

void PresetManager::applyParams (const juce::ValueTree& t)
{
    for (int i = 0; i < ds::kNumParams; ++i)
    {
        float v = ds::paramSpec (i).defV;
        const auto c = t.getChildWithProperty ("id", ds::paramSpec (i).id);
        if (c.isValid())
            v = (float) (double) c.getProperty ("value", v);
        proc.setParameterValue (i, v);
    }
}

void PresetManager::loadFactory (int factoryIndex)
{
    const auto& fp = ds::factoryPresets();
    if (factoryIndex < 0 || factoryIndex >= (int) fp.size())
        return;
    float values[ds::kNumParams];
    ds::applyPreset (fp[(size_t) factoryIndex], values);
    for (int i = 0; i < ds::kNumParams; ++i)
        proc.setParameterValue (i, values[i]);
    for (int i = 0; i < list.size(); ++i)
        if (list[i].factory && list[i].factoryIndex == factoryIndex)
            current = i;
    sendChangeMessage();
}

bool PresetManager::loadEntry (int entryIndex)
{
    if (entryIndex < 0 || entryIndex >= list.size())
        return false;
    const auto e = list[entryIndex];
    if (e.factory)
    {
        loadFactory (e.factoryIndex);
        return true;
    }
    if (loadUserPresetFile (e.file))
    {
        current = entryIndex;
        sendChangeMessage();
        return true;
    }
    return false;
}

void PresetManager::next()
{
    if (! list.isEmpty())
        loadEntry ((current + 1) % list.size());
}

void PresetManager::previous()
{
    if (! list.isEmpty())
        loadEntry ((current - 1 + list.size()) % list.size());
}

bool PresetManager::saveUserPreset (const juce::String& name, const juce::String& category)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return false;
    const auto dir = userPresetDirectory();
    if (! dir.createDirectory())
        return false;
    juce::XmlElement root ("DrumSpreadPreset");
    root.setAttribute ("name", clean);
    root.setAttribute ("category", category);
    root.setAttribute ("version", 1);
    if (auto px = captureParams().createXml())
        root.addChildElement (px.release());
    const auto file = dir.getChildFile (clean + kExtension);
    if (! root.writeTo (file))
        return false;
    rescanUserPresets();
    for (int i = 0; i < list.size(); ++i)
        if (! list[i].factory && list[i].file == file)
            current = i;
    sendChangeMessage();
    return true;
}

bool PresetManager::loadUserPresetFile (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName ("DrumSpreadPreset"))
        return false;
    if (auto* px = xml->getChildByName ("Params"))
    {
        applyParams (juce::ValueTree::fromXml (*px));
        return true;
    }
    return false;
}

void PresetManager::toggleAB()
{
    slots[abActive] = captureParams();
    abActive ^= 1;
    if (slots[abActive].isValid())
        applyParams (slots[abActive]);
    else
        slots[abActive] = captureParams();
    sendChangeMessage();
}

void PresetManager::selectSlot (int slot)
{
    if (slot != abActive)
        toggleAB();
}

void PresetManager::copyActiveToOther()
{
    slots[1 - abActive] = captureParams();
    sendChangeMessage();
}

void PresetManager::writeToState (juce::ValueTree& st) const
{
    st.setProperty ("presetName", currentName(), nullptr);
    st.setProperty ("presetCategory", currentCategory(), nullptr);
    st.setProperty ("abActive", abActive, nullptr);
    for (int s = 0; s < 2; ++s)
        if (slots[s].isValid())
        {
            juce::ValueTree holder (s == 0 ? "SlotA" : "SlotB");
            holder.appendChild (slots[s].createCopy(), nullptr);
            st.appendChild (holder, nullptr);
        }
}

void PresetManager::readFromState (juce::ValueTree& st)
{
    const juce::String name = st.getProperty ("presetName", "");
    const juce::String cat = st.getProperty ("presetCategory", "");
    abActive = juce::jlimit (0, 1, (int) st.getProperty ("abActive", 0));
    for (int s = 0; s < 2; ++s)
    {
        auto holder = st.getChildWithName (s == 0 ? "SlotA" : "SlotB");
        slots[s] = holder.isValid() ? holder.getChildWithName ("Params").createCopy() : juce::ValueTree();
        if (holder.isValid())
            st.removeChild (holder, nullptr);
    }
    st.removeProperty ("presetName", nullptr);
    st.removeProperty ("presetCategory", nullptr);
    st.removeProperty ("abActive", nullptr);
    for (int i = 0; i < list.size(); ++i)
        if (list[i].name == name && list[i].category == cat)
            current = i;
    sendChangeMessage();
}
