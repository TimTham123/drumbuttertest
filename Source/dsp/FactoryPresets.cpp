// DrumSpread factory presets. All values were authored for this project.
// Every preset starts from the parameter defaults with all six modules bypassed;
// a preset enables the modules it uses via "<module>_bypass", 0.
//
// chain_order indices (Warp=W, Room=R, Press=P, Heat=H):
//   3 = W>P>H>R (default)   9 = R>P>H>W   15 = P>R>H>W   16 = P>H>W>R
//   21 = H>R>P>W   22 = H>P>W>R   23 = H>P>R>W   2 = W>P>R>H   5 = W>H>P>R
#include "FactoryPresets.h"

namespace ds
{
namespace
{
    using V = std::vector<std::pair<std::string, float>>;

    std::vector<FactoryPreset> build()
    {
        std::vector<FactoryPreset> p;
        auto add = [&p] (const char* name, const char* cat, V v) { p.push_back ({ name, cat, std::move (v) }); };

        // ------------------------------------------------------------------ Polish
        add ("Golden Spread", "Polish",
             { { "layer_bypass", 0 }, { "layer_sub_sens", 45 }, { "layer_sub_freq", 52 }, { "layer_sub_drop", 7 },
               { "layer_sub_decay", 200 }, { "layer_sub_level", -18 }, { "layer_noise_sens", 45 },
               { "layer_noise_type", 1 }, { "layer_noise_tone", 62 }, { "layer_noise_decay", 70 },
               { "layer_noise_level", -32 },
               { "press_bypass", 0 }, { "press_type", 5 }, { "press_amount", 30 }, { "press_attack", 60 },
               { "press_release", 100 }, { "press_sc_hpf", 90 }, { "press_mix", 80 },
               { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 8 }, { "heat_tone", 10 },
               { "room_bypass", 0 }, { "room_type", 0 }, { "room_size", 30 }, { "room_decay", 0.6f },
               { "room_predelay", 6 }, { "room_tone", 55 }, { "room_width", 110 }, { "room_mix", 18 },
               { "room_target", -30 },
               { "smash_bypass", 0 }, { "smash_curve", 0 }, { "smash_drive", 3 },
               { "master_low_gain", 1.5f }, { "master_low_freq", 80 }, { "master_high_gain", 1.5f },
               { "master_high_freq", 9000 }, { "master_air", 25 }, { "master_attack", 10 } });
        add ("Gentle Sheen", "Polish",
             { { "press_bypass", 0 }, { "press_type", 1 }, { "press_amount", 25 }, { "press_attack", 40 },
               { "press_release", 30 }, { "press_mix", 70 },
               { "heat_bypass", 0 }, { "heat_mode", 1 }, { "heat_drive", 5 }, { "heat_tone", 15 },
               { "master_high_gain", 2 }, { "master_high_freq", 10000 }, { "master_air", 40 } });
        add ("Clean Lift", "Polish",
             { { "press_bypass", 0 }, { "press_type", 2 }, { "press_amount", 28 }, { "press_attack", 55 },
               { "press_release", 35 }, { "press_knee", 1 }, { "press_mix", 100 },
               { "master_low_gain", 1 }, { "master_mid_gain", -1.5f }, { "master_mid_freq", 450 },
               { "master_high_gain", 2.5f }, { "master_high_freq", 7000 }, { "master_air", 20 },
               { "master_attack", 15 } });
        add ("Silk Bus", "Polish",
             { { "press_bypass", 0 }, { "press_type", 4 }, { "press_amount", 35 }, { "press_attack", 50 },
               { "press_release", 45 },
               { "heat_bypass", 0 }, { "heat_mode", 2 }, { "heat_drive", 7 }, { "heat_tone", -5 },
               { "room_bypass", 0 }, { "room_type", 1 }, { "room_size", 25 }, { "room_decay", 0.9f },
               { "room_tone", 65 }, { "room_mix", 10 }, { "room_focus_lo", 300 },
               { "master_air", 30 }, { "master_low_gain", 1 }, { "master_low_freq", 60 } });
        add ("Bright Kit", "Polish",
             { { "layer_bypass", 0 }, { "layer_sub_level", -48 }, { "layer_noise_type", 0 },
               { "layer_noise_tone", 85 }, { "layer_noise_decay", 45 }, { "layer_noise_level", -28 },
               { "layer_noise_sens", 55 },
               { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 6 }, { "heat_tone", 35 },
               { "heat_focus_lo", 2000 },
               { "master_high_gain", 3 }, { "master_high_freq", 8000 }, { "master_air", 45 },
               { "master_hpf", 30 } });
        add ("Tight Polish", "Polish",
             { { "press_bypass", 0 }, { "press_type", 2 }, { "press_amount", 32 }, { "press_attack", 70 },
               { "press_release", 20 }, { "press_knee", 0 },
               { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 5 },
               { "master_attack", 25 }, { "master_sustain", -20 }, { "master_hpf", 35 },
               { "master_high_gain", 1.5f } });

        // ------------------------------------------------------------------ Punch
        add ("Front Row", "Punch",
             { { "layer_bypass", 0 }, { "layer_sub_level", -14 }, { "layer_sub_freq", 50 }, { "layer_sub_decay", 160 },
               { "layer_noise_level", -30 }, { "layer_noise_type", 1 }, { "layer_noise_tone", 70 },
               { "press_bypass", 0 }, { "press_type", 0 }, { "press_amount", 40 }, { "press_attack", 70 },
               { "press_release", 35 }, { "press_mix", 60 },
               { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 9 },
               { "smash_bypass", 0 }, { "smash_drive", 4 },
               { "master_attack", 35 }, { "master_low_gain", 2 }, { "master_low_freq", 70 } });
        add ("Kick Snap", "Punch",
             { { "layer_bypass", 0 }, { "layer_sub_level", -10 }, { "layer_sub_freq", 48 }, { "layer_sub_drop", 12 },
               { "layer_sub_decay", 140 }, { "layer_sub_sens", 55 }, { "layer_noise_level", -48 },
               { "layer_target", -40 },
               { "press_bypass", 0 }, { "press_type", 2 }, { "press_amount", 35 }, { "press_attack", 75 },
               { "press_release", 25 }, { "press_focus_hi", 300 },
               { "master_attack", 40 }, { "master_sustain", -15 } });
        add ("Snare Crack", "Punch",
             { { "layer_bypass", 0 }, { "layer_sub_level", -48 }, { "layer_noise_sens", 60 }, { "layer_noise_type", 3 },
               { "layer_noise_tone", 55 }, { "layer_noise_decay", 120 }, { "layer_noise_level", -18 },
               { "heat_bypass", 0 }, { "heat_mode", 3 }, { "heat_drive", 10 }, { "heat_focus_lo", 400 },
               { "heat_focus_hi", 8000 }, { "heat_target", -50 }, { "heat_mix", 70 },
               { "master_attack", 30 }, { "master_mid_gain", 2 }, { "master_mid_freq", 2500 } });
        add ("Fat Attack", "Punch",
             { { "press_bypass", 0 }, { "press_type", 0 }, { "press_amount", 55 }, { "press_attack", 85 },
               { "press_release", 30 }, { "press_mix", 55 },
               { "heat_bypass", 0 }, { "heat_mode", 1 }, { "heat_drive", 12 }, { "heat_target", -40 },
               { "smash_bypass", 0 }, { "smash_curve", 1 }, { "smash_drive", 5 },
               { "master_attack", 45 }, { "master_low_gain", 2.5f } });
        add ("Thump Bus", "Punch",
             { { "layer_bypass", 0 }, { "layer_sub_level", -8 }, { "layer_sub_freq", 45 }, { "layer_sub_decay", 260 },
               { "layer_sub_growl", 20 }, { "layer_noise_level", -48 },
               { "press_bypass", 0 }, { "press_type", 5 }, { "press_amount", 40 }, { "press_attack", 60 },
               { "press_release", 100 }, { "press_sc_hpf", 120 },
               { "heat_bypass", 0 }, { "heat_mode", 2 }, { "heat_drive", 10 },
               { "master_low_gain", 2 }, { "master_low_freq", 60 }, { "master_hpf", 25 } });
        add ("Knock Box", "Punch",
             { { "layer_bypass", 0 }, { "layer_sub_level", -12 }, { "layer_sub_freq", 56 }, { "layer_sub_drop", 9 },
               { "layer_noise_type", 2 }, { "layer_noise_level", -26 }, { "layer_noise_tone", 45 },
               { "press_bypass", 0 }, { "press_type", 3 }, { "press_amount", 45 }, { "press_attack", 65 },
               { "press_release", 30 },
               { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 12 }, { "heat_tone", -20 },
               { "master_attack", 30 }, { "master_lpf", 14000 }, { "chain_order", 22 } });

        // ------------------------------------------------------------------ Glue
        add ("Bus Glue", "Glue",
             { { "press_bypass", 0 }, { "press_type", 5 }, { "press_amount", 45 }, { "press_attack", 60 },
               { "press_release", 100 }, { "press_sc_hpf", 100 },
               { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 4 } });
        add ("Opto Blanket", "Glue",
             { { "press_bypass", 0 }, { "press_type", 1 }, { "press_amount", 55 }, { "press_attack", 50 },
               { "press_release", 40 },
               { "heat_bypass", 0 }, { "heat_mode", 1 }, { "heat_drive", 6 }, { "heat_tone", -10 },
               { "master_air", 15 } });
        add ("Vari-Mu Warmth", "Glue",
             { { "press_bypass", 0 }, { "press_type", 4 }, { "press_amount", 55 }, { "press_attack", 45 },
               { "press_release", 50 },
               { "heat_bypass", 0 }, { "heat_mode", 1 }, { "heat_drive", 9 },
               { "master_low_gain", 1.5f }, { "master_high_gain", -1 } });
        add ("Diode Thick", "Glue",
             { { "press_bypass", 0 }, { "press_type", 3 }, { "press_amount", 55 }, { "press_attack", 50 },
               { "press_release", 45 },
               { "heat_bypass", 0 }, { "heat_mode", 2 }, { "heat_drive", 8 },
               { "master_mid_gain", 1.5f }, { "master_mid_freq", 700 } });
        add ("Parallel Smash", "Glue",
             { { "press_bypass", 0 }, { "press_type", 0 }, { "press_amount", 85 }, { "press_attack", 40 },
               { "press_release", 30 }, { "press_mix", 40 },
               { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 6 },
               { "master_attack", 15 } });
        add ("Room Glue", "Glue",
             { { "room_bypass", 0 }, { "room_type", 0 }, { "room_size", 35 }, { "room_decay", 0.7f },
               { "room_mix", 25 },
               { "press_bypass", 0 }, { "press_type", 5 }, { "press_amount", 50 }, { "press_attack", 40 },
               { "press_release", 100 },
               { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 5 }, { "chain_order", 9 } });

        // ------------------------------------------------------------------ Space
        add ("Tight Room", "Space",
             { { "room_bypass", 0 }, { "room_type", 0 }, { "room_size", 20 }, { "room_decay", 0.45f },
               { "room_predelay", 4 }, { "room_tone", 50 }, { "room_mix", 30 },
               { "press_bypass", 0 }, { "press_type", 5 }, { "press_amount", 25 }, { "press_release", 100 } });
        add ("Wide Chamber", "Space",
             { { "room_bypass", 0 }, { "room_type", 0 }, { "room_size", 85 }, { "room_decay", 1.6f },
               { "room_predelay", 18 }, { "room_tone", 45 }, { "room_width", 140 }, { "room_mix", 30 },
               { "room_focus_lo", 150 } });
        add ("Plate Shine", "Space",
             { { "room_bypass", 0 }, { "room_type", 1 }, { "room_size", 55 }, { "room_decay", 1.8f },
               { "room_predelay", 20 }, { "room_tone", 70 }, { "room_mix", 25 }, { "room_focus_lo", 400 },
               { "room_target", -40 },
               { "master_air", 20 } });
        add ("Spring Drip", "Space",
             { { "room_bypass", 0 }, { "room_type", 2 }, { "room_size", 50 }, { "room_decay", 2.0f },
               { "room_tone", 55 }, { "room_mix", 30 }, { "room_focus_lo", 250 },
               { "heat_bypass", 0 }, { "heat_mode", 1 }, { "heat_drive", 6 } });
        add ("Gated 80s", "Space",
             { { "room_bypass", 0 }, { "room_type", 3 }, { "room_size", 60 }, { "room_decay", 0.35f },
               { "room_tone", 60 }, { "room_mix", 45 }, { "room_focus_lo", 180 },
               { "press_bypass", 0 }, { "press_type", 2 }, { "press_amount", 45 }, { "press_attack", 60 },
               { "press_release", 25 }, { "chain_order", 15 } });
        add ("Vintage Verb", "Space",
             { { "room_bypass", 0 }, { "room_type", 4 }, { "room_size", 55 }, { "room_decay", 1.4f },
               { "room_predelay", 12 }, { "room_tone", 40 }, { "room_mix", 30 },
               { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 5 } });

        // ------------------------------------------------------------------ Lo-Fi
        add ("Dusty Tape", "Lo-Fi",
             { { "layer_bypass", 0 }, { "layer_sub_level", -48 }, { "layer_noise_type", 2 }, { "layer_noise_level", -24 },
               { "layer_noise_decay", 300 }, { "layer_noise_tone", 35 }, { "layer_target", 40 },
               { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 16 }, { "heat_tone", -40 },
               { "press_bypass", 0 }, { "press_type", 1 }, { "press_amount", 45 },
               { "master_lpf", 9000 }, { "master_hpf", 40 } });
        add ("Bitcrushed Loop", "Lo-Fi",
             { { "heat_bypass", 0 }, { "heat_mode", 5 }, { "heat_drive", 22 }, { "heat_tone", -20 }, { "heat_mix", 70 },
               { "press_bypass", 0 }, { "press_type", 5 }, { "press_amount", 40 }, { "press_release", 100 },
               { "master_lpf", 12000 } });
        add ("Lo-Fi Pitch", "Lo-Fi",
             { { "warp_bypass", 0 }, { "warp_mode", 1 }, { "warp_pitch", -2 }, { "warp_link", 1 },
               { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 12 }, { "heat_tone", -30 },
               { "master_lpf", 10000 } });
        add ("Radio Box", "Lo-Fi",
             { { "heat_bypass", 0 }, { "heat_mode", 3 }, { "heat_drive", 14 }, { "heat_tone", 20 },
               { "press_bypass", 0 }, { "press_type", 2 }, { "press_amount", 50 },
               { "master_hpf", 250 }, { "master_lpf", 5000 }, { "master_mid_gain", 4 }, { "master_mid_freq", 1500 } });
        add ("Cassette Crush", "Lo-Fi",
             { { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 20 }, { "heat_tone", -50 },
               { "press_bypass", 0 }, { "press_type", 4 }, { "press_amount", 50 },
               { "room_bypass", 0 }, { "room_type", 4 }, { "room_size", 30 }, { "room_decay", 0.6f },
               { "room_mix", 20 },
               { "master_lpf", 8000 }, { "master_low_gain", 2 } });
        add ("Crunchy Sampler", "Lo-Fi",
             { { "heat_bypass", 0 }, { "heat_mode", 5 }, { "heat_drive", 14 }, { "heat_tone", 10 },
               { "smash_bypass", 0 }, { "smash_curve", 1 }, { "smash_drive", 8 },
               { "press_bypass", 0 }, { "press_type", 0 }, { "press_amount", 45 }, { "press_mix", 70 },
               { "master_low_gain", 2 }, { "master_lpf", 13000 } });

        // ------------------------------------------------------------------ Heavy
        add ("Industrial", "Heavy",
             { { "heat_bypass", 0 }, { "heat_mode", 2 }, { "heat_drive", 18 },
               { "smash_bypass", 0 }, { "smash_curve", 1 }, { "smash_drive", 14 }, { "smash_clip_mode", 1 },
               { "smash_ceiling", -3 }, { "smash_trim", 2 },
               { "press_bypass", 0 }, { "press_type", 0 }, { "press_amount", 60 },
               { "room_bypass", 0 }, { "room_type", 0 }, { "room_size", 45 }, { "room_decay", 0.8f }, { "room_mix", 25 },
               { "chain_order", 16 } });
        add ("Distorted Bus", "Heavy",
             { { "heat_bypass", 0 }, { "heat_mode", 1 }, { "heat_drive", 22 }, { "heat_tone", 10 },
               { "press_bypass", 0 }, { "press_type", 3 }, { "press_amount", 55 },
               { "smash_bypass", 0 }, { "smash_drive", 8 },
               { "master_low_gain", 2 }, { "master_high_gain", 1 } });
        add ("Fuzz Breaks", "Heavy",
             { { "heat_bypass", 0 }, { "heat_mode", 4 }, { "heat_drive", 20 }, { "heat_mix", 60 },
               { "heat_focus_lo", 120 },
               { "press_bypass", 0 }, { "press_type", 0 }, { "press_amount", 50 }, { "press_mix", 70 },
               { "master_hpf", 30 } });
        add ("Diode Destroy", "Heavy",
             { { "heat_bypass", 0 }, { "heat_mode", 3 }, { "heat_drive", 28 }, { "heat_tone", -15 },
               { "smash_bypass", 0 }, { "smash_curve", 0 }, { "smash_drive", 10 }, { "smash_ceiling", -2 },
               { "press_bypass", 0 }, { "press_type", 2 }, { "press_amount", 55 } });
        add ("Sub Monster", "Heavy",
             { { "layer_bypass", 0 }, { "layer_sub_level", -4 }, { "layer_sub_freq", 42 }, { "layer_sub_decay", 450 },
               { "layer_sub_growl", 65 }, { "layer_sub_drop", 14 }, { "layer_noise_level", -48 },
               { "press_bypass", 0 }, { "press_type", 5 }, { "press_amount", 45 }, { "press_release", 100 },
               { "smash_bypass", 0 }, { "smash_drive", 6 },
               { "master_hpf", 25 } });
        add ("All Buttons", "Heavy",
             { { "press_bypass", 0 }, { "press_type", 0 }, { "press_all_buttons", 1 }, { "press_amount", 60 },
               { "press_attack", 60 }, { "press_release", 40 },
               { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 10 },
               { "master_attack", 20 } });
        add ("Brick Wall", "Heavy",
             { { "press_bypass", 0 }, { "press_type", 2 }, { "press_amount", 65 }, { "press_attack", 50 },
               { "press_release", 30 },
               { "smash_bypass", 0 }, { "smash_curve", 0 }, { "smash_drive", 12 }, { "smash_clip_mode", 0 },
               { "smash_ceiling", -1 },
               { "master_ceiling", -1 } });

        // ------------------------------------------------------------------ Wild
        add ("Formant Freak", "Wild",
             { { "warp_bypass", 0 }, { "warp_pitch", 0 }, { "warp_formant", 7 },
               { "heat_bypass", 0 }, { "heat_mode", 0 }, { "heat_drive", 6 } });
        add ("Octave Down", "Wild",
             { { "warp_bypass", 0 }, { "warp_pitch", -12 }, { "warp_formant", 0 }, { "warp_mix", 55 },
               { "warp_focus_hi", 6000 },
               { "press_bypass", 0 }, { "press_type", 5 }, { "press_amount", 35 }, { "press_release", 100 } });
        add ("Chipmunk Kit", "Wild",
             { { "warp_bypass", 0 }, { "warp_pitch", 7 }, { "warp_link", 1 }, { "warp_mix", 70 },
               { "room_bypass", 0 }, { "room_type", 1 }, { "room_decay", 1.0f }, { "room_mix", 20 } });
        add ("Spring Chaos", "Wild",
             { { "room_bypass", 0 }, { "room_type", 2 }, { "room_size", 90 }, { "room_decay", 4.0f },
               { "room_mix", 50 }, { "room_target", -80 },
               { "heat_bypass", 0 }, { "heat_mode", 4 }, { "heat_drive", 14 }, { "heat_mix", 50 },
               { "chain_order", 21 } });
        add ("Stair Steps", "Wild",
             { { "smash_bypass", 0 }, { "smash_curve", 4 }, { "smash_drive", 12 }, { "smash_bias", 20 },
               { "smash_mix", 60 },
               { "press_bypass", 0 }, { "press_type", 2 }, { "press_amount", 40 } });
        add ("Rectified Room", "Wild",
             { { "smash_bypass", 0 }, { "smash_curve", 3 }, { "smash_drive", 10 }, { "smash_mix", 55 },
               { "room_bypass", 0 }, { "room_type", 4 }, { "room_size", 70 }, { "room_decay", 2.2f },
               { "room_mix", 35 },
               { "warp_bypass", 0 }, { "warp_pitch", -5 }, { "warp_formant", 3 }, { "warp_mix", 40 } });
        return p;
    }
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = build();
    return presets;
}

const std::vector<std::string>& presetCategories()
{
    static const std::vector<std::string> c { "Polish", "Punch", "Glue", "Space", "Lo-Fi", "Heavy", "Wild" };
    return c;
}

int defaultPresetIndex() { return 0; }

int applyPreset (const FactoryPreset& preset, float* params)
{
    setDefaults (params);
    for (int m = 0; m < kNumModules; ++m)
        params[sharedParam (m, kSfBypass)] = 1.0f;
    int unknown = 0;
    for (const auto& kv : preset.values)
    {
        const int idx = findParam (kv.first);
        if (idx < 0)
        {
            ++unknown;
            continue;
        }
        params[idx] = sanitize (idx, kv.second);
    }
    return unknown;
}

} // namespace ds
