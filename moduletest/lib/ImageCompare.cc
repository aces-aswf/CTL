#include "ImageCompare.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>

namespace ctltest {

namespace {

uint32_t bits_f(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

uint32_t ulpDistance(float a, float b) {
    if (std::signbit(a) != std::signbit(b)) {
        if (a == b) return 0;  // 0.0 vs -0.0
        return UINT32_MAX;
    }
    uint32_t ab = bits_f(a);
    uint32_t bb = bits_f(b);
    return ab > bb ? ab - bb : bb - ab;
}

// Resolve per-channel tolerance by falling through base to per_channel[name].
Tolerance resolveChannelTolerance(const Tolerance& base, const std::string& ch) {
    auto it = base.per_channel.find(ch);
    if (it == base.per_channel.end()) return base;
    Tolerance t = base;
    if (it->second.abs) t.abs = it->second.abs;
    if (it->second.rel) t.rel = it->second.rel;
    if (it->second.ulp) t.ulp = it->second.ulp;
    return t;
}

bool withinTolerance(float expected, float got, const Tolerance& tol,
                     double& absErr, double& relErr, uint32_t& ulpErr)
{
    absErr = std::fabs(static_cast<double>(expected) - static_cast<double>(got));
    relErr = (expected != 0.0f) ? absErr / std::fabs(static_cast<double>(expected))
                                : (got == 0.0f ? 0.0 : std::numeric_limits<double>::infinity());
    ulpErr = ulpDistance(expected, got);

    if (!tol.abs && !tol.rel && !tol.ulp) {
        return expected == got;
    }
    if (tol.abs && absErr <= *tol.abs) return true;
    if (tol.rel && relErr <= *tol.rel) return true;
    if (tol.ulp && ulpErr <= static_cast<uint32_t>(*tol.ulp)) return true;
    return false;
}

} // namespace

ImageCompareResult compareImages(const Image& expected,
                                 const Image& actual,
                                 const Tolerance& baseTolerance,
                                 int max_failing_pixels,
                                 size_t diagCap)
{
    ImageCompareResult r;

    if (expected.width != actual.width || expected.height != actual.height) {
        Diagnostic d;
        d.path = "<image.dimensions>";
        std::ostringstream os;
        os << expected.width << "x" << expected.height;
        d.expectedText = os.str();
        std::ostringstream gs;
        gs << actual.width << "x" << actual.height;
        d.gotText = gs.str();
        d.toleranceSource = "image";
        r.failures.push_back(std::move(d));
        r.passed = false;
        return r;
    }

    // Missing / extra channel check.
    for (const auto& kv : expected.channels) {
        if (!actual.channels.count(kv.first)) {
            Diagnostic d;
            d.path = std::string("<channel ") + kv.first + ">";
            d.expectedText = "present in reference";
            d.gotText = "missing from actual";
            d.toleranceSource = "image";
            r.failures.push_back(std::move(d));
            r.passed = false;
        }
    }
    for (const auto& kv : actual.channels) {
        if (!expected.channels.count(kv.first)) {
            Diagnostic d;
            d.path = std::string("<channel ") + kv.first + ">";
            d.expectedText = "absent from reference";
            d.gotText = "extra channel in actual";
            d.toleranceSource = "image";
            r.failures.push_back(std::move(d));
            r.passed = false;
        }
    }
    if (!r.passed) return r;

    const size_t pixelCount = expected.pixelCount();

    for (const auto& kv : expected.channels) {
        const std::string& ch = kv.first;
        const Tolerance tol   = resolveChannelTolerance(baseTolerance, ch);
        const auto& e = kv.second;
        const auto& a = actual.channels.at(ch);
        for (size_t i = 0; i < pixelCount; ++i) {
            double absErr = 0.0, relErr = 0.0;
            uint32_t ulpErr = 0;
            if (withinTolerance(e[i], a[i], tol, absErr, relErr, ulpErr)) continue;
            ++r.mismatchCount;
            if (r.failures.size() < diagCap) {
                Diagnostic d;
                std::ostringstream os;
                const int x = static_cast<int>(i % static_cast<size_t>(expected.width));
                const int y = static_cast<int>(i / static_cast<size_t>(expected.width));
                os << ch << "[" << x << "," << y << "]";
                d.path = os.str();
                {
                    std::ostringstream es;
                    es << e[i];
                    d.expectedText = es.str();
                }
                {
                    std::ostringstream gs;
                    gs << a[i];
                    d.gotText = gs.str();
                }
                d.abs_err = absErr;
                d.rel_err = relErr;
                d.ulp_err = static_cast<int>(std::min<uint32_t>(ulpErr,
                    static_cast<uint32_t>(std::numeric_limits<int>::max())));
                d.applied         = tol;
                d.toleranceSource = tol.per_channel.count(ch) ? "image.per_channel" : "test.tolerance";
                r.failures.push_back(std::move(d));
            }
        }
    }

    if (max_failing_pixels < 0) {
        r.passed = (r.mismatchCount == 0);
    } else {
        r.passed = (r.mismatchCount <= static_cast<size_t>(max_failing_pixels));
    }
    return r;
}

void writeFailureArtifacts(const std::string& refPath,
                           const Image& expected,
                           const Image& actual)
{
    // "<ref>.actual.exr" and "<ref>.diff.exr" -- keep sidecars next to the ref.
    const std::string actualPath = refPath + ".actual.exr";
    const std::string diffPath   = refPath + ".diff.exr";

    writeImage(actualPath, actual);

    Image diff;
    diff.width  = expected.width;
    diff.height = expected.height;
    for (const auto& kv : expected.channels) {
        const std::string& ch = kv.first;
        auto ait = actual.channels.find(ch);
        if (ait == actual.channels.end()) continue;
        std::vector<float> d(kv.second.size(), 0.0f);
        for (size_t i = 0; i < d.size() && i < ait->second.size(); ++i) {
            d[i] = ait->second[i] - kv.second[i];
        }
        diff.channels.emplace(ch, std::move(d));
    }
    writeImage(diffPath, diff);
}

} // namespace ctltest
