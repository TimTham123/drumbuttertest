// DrumSpread offline verification harness - utilities.
#pragma once

#include "Engine.h"
#include "FactoryPresets.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tu
{
using Buf = std::vector<float>;

struct Stereo
{
    Buf L, R;
    Stereo() = default;
    explicit Stereo (size_t n) : L (n, 0.0f), R (n, 0.0f) {}
    size_t size() const { return L.size(); }
};

// ---- report -----------------------------------------------------------------
void addResult (const std::string& area, const std::string& test, const std::string& measured,
                const std::string& criterion, bool pass);
void addInfo (const std::string& area, const std::string& test, const std::string& measured);
void addNote (const std::string& note);
void addTable (const std::string& title, const std::vector<std::string>& header,
               const std::vector<std::vector<std::string>>& rows);
int numFailures();
int numChecks();
std::string reportMarkdown (const std::string& header);
std::string fmt (const char* f, ...);

// ---- output -------------------------------------------------------------------
void setOutputDir (const std::string& dir);
std::string outPath (const std::string& file);
void writeWav (const std::string& path, const Stereo& s, double sr, int bits = 16);

// ---- measurements -------------------------------------------------------------
double toDb (double lin);
double rms (const Buf& b, size_t start = 0, size_t len = 0);
double peakAbs (const Buf& b, size_t start = 0, size_t len = 0);
double mean (const Buf& b, size_t start = 0, size_t len = 0);
bool allFinite (const Buf& b);
size_t countSubnormals (const Buf& b);
/** Max |a[i] - b[i + lag]| over the overlap. */
double maxAbsDiff (const Buf& a, const Buf& b, size_t start = 0, size_t len = 0);

/** Power spectrum (Blackman-Harris window, double precision); n must be a power of 2. */
std::vector<double> powerSpectrum (const float* x, int n);
/** Power (dB, relative to full-scale sine) around frequency f (+-halfWidth bins). */
double tonePowerDb (const std::vector<double>& P, double f, double sr, int n, int halfWidth = 4);
/** Frequency of the strongest peak in [fmin, fmax] (Gaussian interpolation). */
double peakFrequency (const float* x, int n, double sr, double fmin, double fmax);
/** 1/3-octave band levels (dB) between fLo and fHi. */
std::vector<double> thirdOctaveLevels (const Buf& x, double sr, double fLo, double fHi, std::vector<double>* centres = nullptr);
/** Max |second difference| over a range. */
double maxSecondDiff (const Buf& x, size_t start, size_t len);

// ---- engine helpers -----------------------------------------------------------
using Automation = std::function<void (int64_t pos, float* params)>;
/** Renders 'in' through the engine. The engine is prepared and reset with the params. */
Stereo renderEngine (ds::Engine& e, double sr, const Stereo& in, const float* params, int blockSize,
                     const Automation& automation = {}, bool monoIn = false);
void presetParams (const std::string& name, float* params);

} // namespace tu
