// DrumSpread DSP core - factory presets (authored for this project).
#pragma once

#include "Params.h"
#include <string>
#include <utility>
#include <vector>

namespace ds
{
struct FactoryPreset
{
    std::string name;
    std::string category;
    std::vector<std::pair<std::string, float>> values; // param id -> real value
};

/** All factory presets, grouped by category in display order. */
const std::vector<FactoryPreset>& factoryPresets();
/** Category names in display order. */
const std::vector<std::string>& presetCategories();
/** Index of the initial/default preset ("Golden Spread"). */
int defaultPresetIndex();

/** Fills params (kNumParams values) for a preset: parameter defaults, every
    module bypassed, then the preset's own values applied. Returns the number of
    unknown parameter ids (0 for a valid preset). */
int applyPreset (const FactoryPreset& preset, float* params);

} // namespace ds
