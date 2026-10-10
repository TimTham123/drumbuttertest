#include "TestUtil.h"

#include <algorithm>
#include <complex>
#include <cstdarg>
#include <cstdio>
#include <fstream>

namespace tu
{
namespace
{
    struct Row
    {
        std::string area, test, measured, criterion;
        int status; // 1 pass, 0 fail, -1 info
    };
    struct Table
    {
        std::string title;
        std::vector<std::string> header;
        std::vector<std::vector<std::string>> rows;
    };
    std::vector<Row>& rows()
    {
        static std::vector<Row> r;
        return r;
    }
    std::vector<std::string>& notes()
    {
        static std::vector<std::string> n;
        return n;
    }
    std::vector<Table>& tables()
    {
        static std::vector<Table> t;
        return t;
    }
    std::string& outDir()
    {
        static std::string d = "test_output";
        return d;
    }

    void fftInPlace (std::vector<std::complex<double>>& a)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = -2.0 * 3.14159265358979323846 / (double) len;
            const std::complex<double> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0, 0.0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k];
                    const auto v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }
} // namespace

void addResult (const std::string& area, const std::string& test, const std::string& measured,
                const std::string& criterion, bool pass)
{
    rows().push_back ({ area, test, measured, criterion, pass ? 1 : 0 });
    std::printf ("[%s] %-10s %-46s %s  (%s)\n", pass ? "PASS" : "FAIL", area.c_str(), test.c_str(), measured.c_str(),
                 criterion.c_str());
    std::fflush (stdout);
}

void addInfo (const std::string& area, const std::string& test, const std::string& measured)
{
    rows().push_back ({ area, test, measured, "-", -1 });
    std::printf ("[INFO] %-10s %-46s %s\n", area.c_str(), test.c_str(), measured.c_str());
    std::fflush (stdout);
}

void addNote (const std::string& note) { notes().push_back (note); }

void addTable (const std::string& title, const std::vector<std::string>& header,
               const std::vector<std::vector<std::string>>& r)
{
    tables().push_back ({ title, header, r });
}

int numFailures()
{
    int f = 0;
    for (const auto& r : rows())
        if (r.status == 0)
            ++f;
    return f;
}

int numChecks()
{
    int c = 0;
    for (const auto& r : rows())
        if (r.status >= 0)
            ++c;
    return c;
}

std::string reportMarkdown (const std::string& header)
{
    std::string s = header;
    s += fmt ("\n**Summary: %d checks, %d passed, %d failed.**\n\n", numChecks(), numChecks() - numFailures(),
              numFailures());
    s += "| Area | Test | Measured | Criterion | Result |\n|---|---|---|---|---|\n";
    for (const auto& r : rows())
        s += "| " + r.area + " | " + r.test + " | " + r.measured + " | " + r.criterion + " | "
             + (r.status == 1 ? "PASS" : (r.status == 0 ? "**FAIL**" : "info")) + " |\n";
    for (const auto& t : tables())
    {
        s += "\n### " + t.title + "\n\n|";
        for (const auto& h : t.header)
            s += " " + h + " |";
        s += "\n|";
        for (size_t i = 0; i < t.header.size(); ++i)
            s += "---|";
        s += "\n";
        for (const auto& row : t.rows)
        {
            s += "|";
            for (const auto& c : row)
                s += " " + c + " |";
            s += "\n";
        }
    }
    if (! notes().empty())
    {
        s += "\n### Notes\n\n";
        for (const auto& n : notes())
            s += "- " + n + "\n";
    }
    return s;
}

std::string fmt (const char* f, ...)
{
    char buf[2048];
    va_list ap;
    va_start (ap, f);
    std::vsnprintf (buf, sizeof (buf), f, ap);
    va_end (ap);
    return buf;
}

void setOutputDir (const std::string& dir) { outDir() = dir; }
std::string outPath (const std::string& file) { return outDir() + "/" + file; }

void writeWav (const std::string& path, const Stereo& s, double sr, int bits)
{
    std::ofstream f (path, std::ios::binary);
    const uint32_t n = (uint32_t) s.size();
    const uint16_t ch = 2, bps = (uint16_t) bits;
    const uint32_t byteRate = (uint32_t) sr * ch * bps / 8;
    const uint16_t align = (uint16_t) (ch * bps / 8);
    const uint32_t dataBytes = n * align;
    auto w32 = [&f] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto w16 = [&f] (uint16_t v) { f.write ((const char*) &v, 2); };
    f.write ("RIFF", 4);
    w32 (36 + dataBytes);
    f.write ("WAVEfmt ", 8);
    w32 (16);
    w16 (1);
    w16 (ch);
    w32 ((uint32_t) sr);
    w32 (byteRate);
    w16 (align);
    w16 (bps);
    f.write ("data", 4);
    w32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
    {
        for (int c = 0; c < 2; ++c)
        {
            const double v = std::max (-1.0, std::min (1.0, (double) (c == 0 ? s.L[i] : s.R[i])));
            if (bits == 24)
            {
                const int32_t q = (int32_t) std::lround (v * 8388607.0);
                const char b[3] = { (char) (q & 0xff), (char) ((q >> 8) & 0xff), (char) ((q >> 16) & 0xff) };
                f.write (b, 3);
            }
            else
            {
                const int16_t q = (int16_t) std::lround (v * 32767.0);
                f.write ((const char*) &q, 2);
            }
        }
    }
}

double toDb (double lin) { return lin > 1.0e-15 ? 20.0 * std::log10 (lin) : -300.0; }

static void rangeOf (const Buf& b, size_t& start, size_t& len)
{
    start = std::min (start, b.size());
    if (len == 0 || start + len > b.size())
        len = b.size() - start;
}

double rms (const Buf& b, size_t start, size_t len)
{
    rangeOf (b, start, len);
    if (len == 0)
        return 0.0;
    double s = 0.0;
    for (size_t i = start; i < start + len; ++i)
        s += (double) b[i] * b[i];
    return std::sqrt (s / (double) len);
}

double peakAbs (const Buf& b, size_t start, size_t len)
{
    rangeOf (b, start, len);
    double p = 0.0;
    for (size_t i = start; i < start + len; ++i)
        p = std::max (p, (double) std::fabs (b[i]));
    return p;
}

double mean (const Buf& b, size_t start, size_t len)
{
    rangeOf (b, start, len);
    if (len == 0)
        return 0.0;
    double s = 0.0;
    for (size_t i = start; i < start + len; ++i)
        s += b[i];
    return s / (double) len;
}

bool allFinite (const Buf& b)
{
    for (float v : b)
        if (! std::isfinite (v))
            return false;
    return true;
}

size_t countSubnormals (const Buf& b)
{
    size_t c = 0;
    for (float v : b)
        if (std::fpclassify (v) == FP_SUBNORMAL)
            ++c;
    return c;
}

double maxAbsDiff (const Buf& a, const Buf& b, size_t start, size_t len)
{
    const size_t n = std::min (a.size(), b.size());
    if (len == 0 || start + len > n)
        len = n > start ? n - start : 0;
    double m = 0.0;
    for (size_t i = start; i < start + len; ++i)
        m = std::max (m, (double) std::fabs ((double) a[i] - (double) b[i]));
    return m;
}

std::vector<double> powerSpectrum (const float* x, int n)
{
    std::vector<std::complex<double>> a ((size_t) n);
    double wsum = 0.0, w2sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = 2.0 * 3.14159265358979323846 * i / (double) n;
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        wsum += w;
        w2sum += w * w;
        a[(size_t) i] = std::complex<double> (x[i] * w, 0.0);
    }
    fftInPlace (a);
    std::vector<double> P ((size_t) (n / 2 + 1));
    // normalised so that summing the bins of a tone gives its mean-square value
    // (A^2/2 for a sine of amplitude A) and summing a band gives band power
    const double sc = 2.0 / wsum;
    const double enbw = (double) n * w2sum / (wsum * wsum);
    for (int k = 0; k <= n / 2; ++k)
        P[(size_t) k] = std::norm (a[(size_t) k] * sc) * 0.5 / enbw;
    return P;
}

double tonePowerDb (const std::vector<double>& P, double f, double sr, int n, int hw)
{
    const int k0 = (int) std::lround (f * n / sr);
    double s = 0.0;
    for (int k = std::max (0, k0 - hw); k <= std::min ((int) P.size() - 1, k0 + hw); ++k)
        s += P[(size_t) k];
    // dB relative to a full-scale sine (mean square 0.5)
    return 10.0 * std::log10 (std::max (2.0 * s, 1.0e-30));
}

double peakFrequency (const float* x, int n, double sr, double fmin, double fmax)
{
    auto P = powerSpectrum (x, n);
    const int k0 = std::max (1, (int) (fmin * n / sr));
    const int k1 = std::min ((int) P.size() - 2, (int) (fmax * n / sr));
    int kb = k0;
    for (int k = k0; k <= k1; ++k)
        if (P[(size_t) k] > P[(size_t) kb])
            kb = k;
    const double a = std::log (P[(size_t) kb - 1] + 1e-30), b = std::log (P[(size_t) kb] + 1e-30),
                 c = std::log (P[(size_t) kb + 1] + 1e-30);
    const double d = 0.5 * (a - c) / (a - 2 * b + c);
    return (kb + d) * sr / n;
}

std::vector<double> thirdOctaveLevels (const Buf& x, double sr, double fLo, double fHi, std::vector<double>* centres)
{
    int n = 1;
    while (n * 2 <= (int) x.size() && n < (1 << 18))
        n *= 2;
    // average power spectra over consecutive segments
    const int seg = std::min (n, 16384);
    std::vector<double> P ((size_t) (seg / 2 + 1), 0.0);
    int count = 0;
    for (size_t off = 0; off + (size_t) seg <= x.size(); off += (size_t) seg / 2, ++count)
    {
        auto p = powerSpectrum (x.data() + off, seg);
        for (size_t k = 0; k < P.size(); ++k)
            P[k] += p[k];
    }
    std::vector<double> out;
    if (centres)
        centres->clear();
    for (double fc = fLo; fc <= fHi * 1.0001; fc *= std::pow (2.0, 1.0 / 3.0))
    {
        const double a = fc / std::pow (2.0, 1.0 / 6.0), b = fc * std::pow (2.0, 1.0 / 6.0);
        double s = 0.0;
        for (int k = (int) std::ceil (a * seg / sr); k <= (int) std::floor (b * seg / sr) && k < (int) P.size(); ++k)
            s += P[(size_t) k];
        out.push_back (10.0 * std::log10 (std::max (s / std::max (1, count), 1e-30)));
        if (centres)
            centres->push_back (fc);
    }
    return out;
}

double maxSecondDiff (const Buf& x, size_t start, size_t len)
{
    double m = 0.0;
    for (size_t i = std::max<size_t> (start, 2); i < std::min (x.size(), start + len); ++i)
        m = std::max (m, std::fabs ((double) x[i] - 2.0 * x[i - 1] + x[i - 2]));
    return m;
}

Stereo renderEngine (ds::Engine& e, double sr, const Stereo& in, const float* params, int blockSize,
                     const Automation& automation, bool monoIn)
{
    std::vector<float> p (params, params + ds::kNumParams);
    e.setParameters (p.data());
    e.prepare (sr, std::max (blockSize, 1));
    e.setParameters (p.data());
    e.reset();
    Stereo out (in.size());
    const size_t n = in.size();
    for (size_t pos = 0; pos < n;)
    {
        const int m = (int) std::min<size_t> ((size_t) blockSize, n - pos);
        if (automation)
        {
            automation ((int64_t) pos, p.data());
            e.setParameters (p.data());
        }
        e.process (in.L.data() + pos, monoIn ? nullptr : in.R.data() + pos, out.L.data() + pos, out.R.data() + pos, m);
        pos += (size_t) m;
    }
    return out;
}

void presetParams (const std::string& name, float* params)
{
    for (const auto& pr : ds::factoryPresets())
        if (pr.name == name)
        {
            ds::applyPreset (pr, params);
            return;
        }
    ds::setDefaults (params);
}

} // namespace tu
