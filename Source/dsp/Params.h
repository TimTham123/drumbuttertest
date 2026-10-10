// DrumSpread DSP core - the single parameter table shared by plugin, presets and tests.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace ds
{
enum class PType
{
    Float,
    Bool,
    Choice
};

struct ParamSpec
{
    const char* id;
    const char* name;
    PType type;
    float minV, maxV, defV;
    float centre;        // knob-centre value for a log/skewed taper; <= minV means linear
    float step;          // 0 = continuous
    const char* unit;    // display unit
    const char* choices; // '|' separated (Choice only)
};

enum ModuleId : int
{
    kModLayer = 0,
    kModWarp,
    kModRoom,
    kModPress,
    kModHeat,
    kModSmash,
    kNumModules
};

/** The four reorderable module types (index into chain permutations). */
enum SlotType : int
{
    kSlotWarp = 0,
    kSlotRoom,
    kSlotPress,
    kSlotHeat,
    kNumSlots
};

enum SharedField : int
{
    kSfBypass = 0,
    kSfMix,
    kSfFocusLo,
    kSfFocusHi,
    kSfTarget,
    kNumSharedFields
};

enum ParamId : int
{
    kInGain = 0,
    kOutGain,
    kDryWet,
    kIntensity,
    kOversampling,
    kBypass,
    kChainOrder,
    kSharedBase, // shared per-module params start here (6 modules x 5)

    kLayerSubSens = kSharedBase + kNumModules * kNumSharedFields,
    kLayerSubFreq,
    kLayerSubDrop,
    kLayerSubDecay,
    kLayerSubGrowl,
    kLayerSubLevel,
    kLayerNoiseSens,
    kLayerNoiseType,
    kLayerNoiseTone,
    kLayerNoiseDecay,
    kLayerNoiseLevel,

    kWarpPitch,
    kWarpFormant,
    kWarpLink,
    kWarpMode,

    kRoomType,
    kRoomSize,
    kRoomDecay,
    kRoomPredelay,
    kRoomTone,
    kRoomWidth,

    kPressType,
    kPressAmount,
    kPressAttack,
    kPressRelease,
    kPressScHpf,
    kPressMakeup,
    kPressKnee,
    kPressAllButtons,

    kHeatMode,
    kHeatDrive,
    kHeatTone,
    kHeatTrim,

    kSmashCurve,
    kSmashDrive,
    kSmashBias,
    kSmashClipMode,
    kSmashCeiling,
    kSmashTrim,

    kMasterLowGain,
    kMasterLowFreq,
    kMasterMidGain,
    kMasterMidFreq,
    kMasterHighGain,
    kMasterHighFreq,
    kMasterHpf,
    kMasterLpf,
    kMasterAir,
    kMasterAttack,
    kMasterSustain,
    kMasterClip,
    kMasterCeiling,

    kNumParams
};

constexpr int sharedParam (int module, int field) { return kSharedBase + module * kNumSharedFields + field; }

/** Convenience indices for the shared per-module parameters. */
constexpr int kLayerBypass = sharedParam (kModLayer, kSfBypass), kLayerMix = sharedParam (kModLayer, kSfMix), kLayerFocusLo = sharedParam (kModLayer, kSfFocusLo), kLayerFocusHi = sharedParam (kModLayer, kSfFocusHi), kLayerTarget = sharedParam (kModLayer, kSfTarget);
constexpr int kWarpBypass = sharedParam (kModWarp, kSfBypass), kWarpMix = sharedParam (kModWarp, kSfMix), kWarpFocusLo = sharedParam (kModWarp, kSfFocusLo), kWarpFocusHi = sharedParam (kModWarp, kSfFocusHi), kWarpTarget = sharedParam (kModWarp, kSfTarget);
constexpr int kRoomBypass = sharedParam (kModRoom, kSfBypass), kRoomMix = sharedParam (kModRoom, kSfMix), kRoomFocusLo = sharedParam (kModRoom, kSfFocusLo), kRoomFocusHi = sharedParam (kModRoom, kSfFocusHi), kRoomTarget = sharedParam (kModRoom, kSfTarget);
constexpr int kPressBypass = sharedParam (kModPress, kSfBypass), kPressMix = sharedParam (kModPress, kSfMix), kPressFocusLo = sharedParam (kModPress, kSfFocusLo), kPressFocusHi = sharedParam (kModPress, kSfFocusHi), kPressTarget = sharedParam (kModPress, kSfTarget);
constexpr int kHeatBypass = sharedParam (kModHeat, kSfBypass), kHeatMix = sharedParam (kModHeat, kSfMix), kHeatFocusLo = sharedParam (kModHeat, kSfFocusLo), kHeatFocusHi = sharedParam (kModHeat, kSfFocusHi), kHeatTarget = sharedParam (kModHeat, kSfTarget);
constexpr int kSmashBypass = sharedParam (kModSmash, kSfBypass), kSmashMix = sharedParam (kModSmash, kSfMix), kSmashFocusLo = sharedParam (kModSmash, kSfFocusLo), kSmashFocusHi = sharedParam (kModSmash, kSfFocusHi), kSmashTarget = sharedParam (kModSmash, kSfTarget);

/** Module type id for a reorderable slot type. */
constexpr int moduleForSlot (int slotType) { return slotType + 1; }

const ParamSpec& paramSpec (int index);
/** Returns -1 if unknown. */
int findParam (const std::string& id);
void setDefaults (float* values);
/** Clamp to range, round choices/bools/steps. */
float sanitize (int index, float value);
std::vector<std::string> choiceNames (int index);

const char* moduleName (int module);    // "LAYER", ...
const char* moduleIdPrefix (int module); // "layer", ...

constexpr int kNumChainOrders = 24;
const std::array<int, 4>& chainPermutation (int index);
int chainIndexOf (const std::array<int, 4>& order);
std::string chainOrderName (int index);
constexpr int kDefaultChainOrder = 3; // Warp > Press > Heat > Room

// Choice enums (values stored as float indices)
enum NoiseType { kNoiseWhite = 0, kNoisePink, kNoiseCrackle, kNoiseBand, kNumNoiseTypes };
enum WarpMode { kWarpHQ = 0, kWarpLoFi };
enum RoomType { kRoomRoom = 0, kRoomPlate, kRoomSpring, kRoomGated, kRoomVintage, kNumRoomTypes };
enum PressType { kPressFET = 0, kPressOpto, kPressVCA, kPressDiode, kPressVariMu, kPressGlue, kNumPressTypes };
enum HeatMode { kHeatTape = 0, kHeatTube, kHeatTransformer, kHeatDiode, kHeatFuzz, kHeatCrush, kNumHeatModes };
enum SmashCurve { kCurveSmooth = 0, kCurveCrunch, kCurveFold, kCurveRectify, kCurveStairs, kNumCurves };

/** Oversampling factor from the choice index. */
inline int osFactorFromIndex (int idx) { return idx <= 0 ? 1 : (idx == 1 ? 2 : 4); }

} // namespace ds
