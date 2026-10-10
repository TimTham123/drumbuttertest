#include "Params.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>

namespace ds
{
namespace
{
    struct Table
    {
        std::array<ParamSpec, kNumParams> specs {};
        std::deque<std::string> strings; // stable storage for generated ids/names
        std::array<std::array<int, 4>, kNumChainOrders> perms {};
        std::string chainChoices;

        const char* keep (const std::string& s)
        {
            strings.push_back (s);
            return strings.back().c_str();
        }

        void set (int idx, const char* id, const char* name, PType t, float mn, float mx, float df,
                  float centre = -1.0e9f, float step = 0.0f, const char* unit = "", const char* choices = "")
        {
            specs[(size_t) idx] = ParamSpec { id, name, t, mn, mx, df, centre, step, unit, choices };
        }
        void flt (int idx, const char* id, const char* name, float mn, float mx, float df, const char* unit,
                  float centre = -1.0e9f, float step = 0.0f)
        {
            set (idx, id, name, PType::Float, mn, mx, df, centre, step, unit, "");
        }
        void boo (int idx, const char* id, const char* name, bool df)
        {
            set (idx, id, name, PType::Bool, 0.0f, 1.0f, df ? 1.0f : 0.0f, -1.0e9f, 1.0f, "", "");
        }
        void cho (int idx, const char* id, const char* name, const char* choices, int df)
        {
            int n = 1;
            for (const char* c = choices; *c; ++c)
                if (*c == '|')
                    ++n;
            set (idx, id, name, PType::Choice, 0.0f, (float) (n - 1), (float) df, -1.0e9f, 1.0f, "", choices);
        }

        Table()
        {
            // chain permutations in lexicographic order
            std::array<int, 4> p { 0, 1, 2, 3 };
            int k = 0;
            do
            {
                perms[(size_t) k++] = p;
            } while (std::next_permutation (p.begin(), p.end()));
            static const char* slotNames[4] = { "Warp", "Room", "Press", "Heat" };
            for (int i = 0; i < kNumChainOrders; ++i)
            {
                if (i > 0)
                    chainChoices += "|";
                for (int j = 0; j < 4; ++j)
                {
                    if (j > 0)
                        chainChoices += ">";
                    chainChoices += slotNames[perms[(size_t) i][(size_t) j]];
                }
            }

            flt (kInGain, "in_gain", "Input", -24.0f, 24.0f, 0.0f, "dB", -1.0e9f, 0.01f);
            flt (kOutGain, "out_gain", "Output", -24.0f, 24.0f, 0.0f, "dB", -1.0e9f, 0.01f);
            flt (kDryWet, "dry_wet", "Dry/Wet", 0.0f, 100.0f, 100.0f, "%");
            flt (kIntensity, "intensity", "Intensity", 0.0f, 200.0f, 100.0f, "%");
            cho (kOversampling, "oversampling", "Oversampling", "1x|2x|4x", 1);
            boo (kBypass, "bypass", "Bypass", false);
            cho (kChainOrder, "chain_order", "Chain Order", chainChoices.c_str(), kDefaultChainOrder);

            for (int m = 0; m < kNumModules; ++m)
            {
                const std::string pre = moduleIdPrefix (m);
                std::string disp = moduleName (m);
                std::transform (disp.begin() + 1, disp.end(), disp.begin() + 1, [] (char c) { return (char) std::tolower (c); });
                boo (sharedParam (m, kSfBypass), keep (pre + "_bypass"), keep (disp + " Bypass"), false);
                flt (sharedParam (m, kSfMix), keep (pre + "_mix"), keep (disp + " Mix"), 0.0f, 100.0f,
                     m == kModRoom ? 30.0f : 100.0f, "%");
                flt (sharedParam (m, kSfFocusLo), keep (pre + "_focus_lo"), keep (disp + " Focus Lo"), 20.0f, 5000.0f,
                     20.0f, "Hz", 300.0f);
                flt (sharedParam (m, kSfFocusHi), keep (pre + "_focus_hi"), keep (disp + " Focus Hi"), 200.0f, 20000.0f,
                     20000.0f, "Hz", 2000.0f);
                flt (sharedParam (m, kSfTarget), keep (pre + "_target"), keep (disp + " Target"), -100.0f, 100.0f, 0.0f,
                     "%");
            }

            flt (kLayerSubSens, "layer_sub_sens", "Sub Sens", 0.0f, 100.0f, 50.0f, "%");
            flt (kLayerSubFreq, "layer_sub_freq", "Sub Freq", 30.0f, 120.0f, 52.0f, "Hz", 60.0f);
            flt (kLayerSubDrop, "layer_sub_drop", "Sub Drop", 0.0f, 24.0f, 7.0f, "st");
            flt (kLayerSubDecay, "layer_sub_decay", "Sub Decay", 20.0f, 1000.0f, 220.0f, "ms", 150.0f);
            flt (kLayerSubGrowl, "layer_sub_growl", "Growl", 0.0f, 100.0f, 0.0f, "%");
            flt (kLayerSubLevel, "layer_sub_level", "Sub Level", -48.0f, 12.0f, -12.0f, "dB");
            flt (kLayerNoiseSens, "layer_noise_sens", "Noise Sens", 0.0f, 100.0f, 50.0f, "%");
            cho (kLayerNoiseType, "layer_noise_type", "Noise Type", "White|Pink|Crackle|Band", kNoisePink);
            flt (kLayerNoiseTone, "layer_noise_tone", "Noise Tone", 0.0f, 100.0f, 50.0f, "%");
            flt (kLayerNoiseDecay, "layer_noise_decay", "Noise Decay", 10.0f, 800.0f, 90.0f, "ms", 90.0f);
            flt (kLayerNoiseLevel, "layer_noise_level", "Noise Level", -48.0f, 6.0f, -24.0f, "dB");

            flt (kWarpPitch, "warp_pitch", "Pitch", -24.0f, 24.0f, 0.0f, "st", -1.0e9f, 0.01f);
            flt (kWarpFormant, "warp_formant", "Formant", -24.0f, 24.0f, 0.0f, "st", -1.0e9f, 0.01f);
            boo (kWarpLink, "warp_link", "Link", false);
            cho (kWarpMode, "warp_mode", "Warp Mode", "HQ|Lo-Fi", kWarpHQ);

            cho (kRoomType, "room_type", "Room Type", "Room|Plate|Spring|Gated|Vintage", kRoomRoom);
            flt (kRoomSize, "room_size", "Size", 0.0f, 100.0f, 40.0f, "%");
            flt (kRoomDecay, "room_decay", "Decay", 0.1f, 8.0f, 0.8f, "s", 1.0f);
            flt (kRoomPredelay, "room_predelay", "Pre-delay", 0.0f, 200.0f, 8.0f, "ms", 30.0f);
            flt (kRoomTone, "room_tone", "Tone", 0.0f, 100.0f, 55.0f, "%");
            flt (kRoomWidth, "room_width", "Width", 0.0f, 150.0f, 100.0f, "%");

            cho (kPressType, "press_type", "Press Type", "FET|Opto|VCA|Diode|Vari-Mu|Glue", kPressGlue);
            flt (kPressAmount, "press_amount", "Amount", 0.0f, 100.0f, 35.0f, "%");
            flt (kPressAttack, "press_attack", "Attack", 0.0f, 100.0f, 50.0f, "%");
            flt (kPressRelease, "press_release", "Release", 0.0f, 100.0f, 40.0f, "%");
            flt (kPressScHpf, "press_sc_hpf", "SC HPF", 20.0f, 500.0f, 80.0f, "Hz", 100.0f);
            boo (kPressMakeup, "press_makeup", "Auto Makeup", true);
            cho (kPressKnee, "press_knee", "Knee", "Hard|Soft", 1);
            boo (kPressAllButtons, "press_all_buttons", "All Buttons", false);

            cho (kHeatMode, "heat_mode", "Heat Mode", "Tape|Tube|Transformer|Diode|Fuzz|Crush", kHeatTape);
            flt (kHeatDrive, "heat_drive", "Heat Drive", 0.0f, 36.0f, 6.0f, "dB");
            flt (kHeatTone, "heat_tone", "Heat Tone", -100.0f, 100.0f, 0.0f, "%");
            flt (kHeatTrim, "heat_trim", "Heat Trim", -24.0f, 12.0f, 0.0f, "dB");

            cho (kSmashCurve, "smash_curve", "Curve", "Smooth|Crunch|Fold|Rectify|Stairs", kCurveSmooth);
            flt (kSmashDrive, "smash_drive", "Smash Drive", 0.0f, 36.0f, 0.0f, "dB");
            flt (kSmashBias, "smash_bias", "Bias", -100.0f, 100.0f, 0.0f, "%");
            cho (kSmashClipMode, "smash_clip_mode", "Clip Mode", "Soft|Hard", 0);
            flt (kSmashCeiling, "smash_ceiling", "Ceiling", -24.0f, 0.0f, 0.0f, "dB");
            flt (kSmashTrim, "smash_trim", "Smash Trim", -24.0f, 12.0f, 0.0f, "dB");

            flt (kMasterLowGain, "master_low_gain", "Low", -12.0f, 12.0f, 0.0f, "dB");
            flt (kMasterLowFreq, "master_low_freq", "Low Freq", 30.0f, 400.0f, 90.0f, "Hz", 100.0f);
            flt (kMasterMidGain, "master_mid_gain", "Mid", -12.0f, 12.0f, 0.0f, "dB");
            flt (kMasterMidFreq, "master_mid_freq", "Mid Freq", 200.0f, 8000.0f, 1200.0f, "Hz", 1200.0f);
            flt (kMasterHighGain, "master_high_gain", "High", -12.0f, 12.0f, 0.0f, "dB");
            flt (kMasterHighFreq, "master_high_freq", "High Freq", 2000.0f, 16000.0f, 8000.0f, "Hz", 6000.0f);
            flt (kMasterHpf, "master_hpf", "HPF", 10.0f, 500.0f, 10.0f, "Hz", 60.0f);
            flt (kMasterLpf, "master_lpf", "LPF", 1000.0f, 22000.0f, 22000.0f, "Hz", 5000.0f);
            flt (kMasterAir, "master_air", "Air", 0.0f, 100.0f, 0.0f, "%");
            flt (kMasterAttack, "master_attack", "Attack", -100.0f, 100.0f, 0.0f, "%");
            flt (kMasterSustain, "master_sustain", "Sustain", -100.0f, 100.0f, 0.0f, "%");
            boo (kMasterClip, "master_clip", "Master Clip", true);
            flt (kMasterCeiling, "master_ceiling", "Ceiling", -12.0f, 0.0f, -0.3f, "dB", -1.0e9f, 0.01f);
        }
    };

    Table& table()
    {
        static Table t;
        return t;
    }
} // namespace

const ParamSpec& paramSpec (int index) { return table().specs[(size_t) index]; }

int findParam (const std::string& id)
{
    const auto& t = table();
    for (int i = 0; i < kNumParams; ++i)
        if (id == t.specs[(size_t) i].id)
            return i;
    return -1;
}

void setDefaults (float* values)
{
    for (int i = 0; i < kNumParams; ++i)
        values[i] = paramSpec (i).defV;
}

float sanitize (int index, float v)
{
    const auto& s = paramSpec (index);
    if (! std::isfinite (v))
        v = s.defV;
    v = std::min (s.maxV, std::max (s.minV, v));
    if (s.type != PType::Float)
        v = std::round (v);
    return v;
}

std::vector<std::string> choiceNames (int index)
{
    std::vector<std::string> out;
    const char* c = paramSpec (index).choices;
    std::string cur;
    for (; c && *c; ++c)
    {
        if (*c == '|')
        {
            out.push_back (cur);
            cur.clear();
        }
        else
            cur += *c;
    }
    if (! cur.empty() || ! out.empty())
        out.push_back (cur);
    return out;
}

const char* moduleName (int m)
{
    static const char* n[kNumModules] = { "LAYER", "WARP", "ROOM", "PRESS", "HEAT", "SMASH" };
    return (m >= 0 && m < kNumModules) ? n[m] : "";
}

const char* moduleIdPrefix (int m)
{
    static const char* n[kNumModules] = { "layer", "warp", "room", "press", "heat", "smash" };
    return (m >= 0 && m < kNumModules) ? n[m] : "";
}

const std::array<int, 4>& chainPermutation (int index)
{
    index = std::min (kNumChainOrders - 1, std::max (0, index));
    return table().perms[(size_t) index];
}

int chainIndexOf (const std::array<int, 4>& order)
{
    const auto& t = table();
    for (int i = 0; i < kNumChainOrders; ++i)
        if (t.perms[(size_t) i] == order)
            return i;
    return kDefaultChainOrder;
}

std::string chainOrderName (int index)
{
    auto names = choiceNames (kChainOrder);
    return (index >= 0 && index < (int) names.size()) ? names[(size_t) index] : std::string();
}

} // namespace ds
