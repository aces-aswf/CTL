#ifndef CTLTEST_IMAGE_COMPARE_H
#define CTLTEST_IMAGE_COMPARE_H

#include "CaseModel.h"
#include "ImageIO.h"
#include "Result.h"

namespace ctltest {

// Per-pixel, per-channel diff between two float32 images. Tolerance is sharpened
// by `baseTolerance.per_channel[chName]` when present, and an overall cap on
// reported mismatches is controlled by `max_failing_pixels`:
//   <0 : no cap, every failure becomes a Diagnostic
//    0 : any single mismatch fails (legacy meaning matches "tight diff")
//   >0 : up to N mismatches are considered tolerated; >N fails
//
// On failure, at most `diagCap` diagnostics are emitted (per-channel coords
// and values) so large-image diffs don't explode the report.
struct ImageCompareResult {
    bool passed = true;
    size_t mismatchCount = 0;       // total per-pixel-per-channel mismatches
    std::vector<Diagnostic> failures;
};

ImageCompareResult compareImages(const Image& expected,
                                 const Image& actual,
                                 const Tolerance& baseTolerance,
                                 int max_failing_pixels,
                                 size_t diagCap = 16);

// If compare failed, write the actual image and a diff image next to the
// reference (e.g. ref.exr to ref.actual.exr, ref.diff.exr). Channel layout of
// the diff: same channel names as the reference, containing (actual-expected).
void writeFailureArtifacts(const std::string& refPath,
                           const Image& expected,
                           const Image& actual);

} // namespace ctltest

#endif
