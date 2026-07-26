///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

#include <CtlSimdInterpreter.h>
#include <CtlFunctionCall.h>
#include <testInlineHelpers.h>
#include <testRequire.h>

#include <iostream>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <limits>

using namespace Ctl;
using namespace std;


// Pins the codegen substitutions in CtlSimdSyntaxTree.cpp.  The fixture
// (testInlineHelpers.ctl) defines the Lib.Academy.Utilities helpers
// verbatim from aces-core, which is what makes them eligible for
// substitution; this test runs each one against IEEE-754 specials and
// confirms bit-equality (with NaN-payload tolerance) against a reference
// impl that mirrors the inline C++.  A divergence here means the inline
// body has drifted from the CTL body.
//
// This test cannot tell whether a substitution happened, since a
// canonical body computes the same thing either way.  testHelperRecognition
// checks that directly, in both directions.

namespace {

uint32_t
bits (float f)
{
    uint32_t u;
    memcpy (&u, &f, sizeof (u));
    return u;
}


bool
bitwise_equal (float a, float b)
{
    return bits (a) == bits (b);
}


// IEEE-754: NaN bit-patterns are not architecturally canonicalised in a
// way we can rely on across libm and SIMD paths.  Treat any two NaNs as
// equal -- the exact payload is not part of the contract being tested.
bool
nan_or_eq (float a, float b)
{
    if (std::isnan (a) && std::isnan (b)) return true;
    return bitwise_equal (a, b);
}


const float kSpecials[] = {
    0.0f,
    -0.0f,
    1.0f,
    -1.0f,
    0.5f,
    -0.5f,
    std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::quiet_NaN(),
    std::numeric_limits<float>::min(),
    std::numeric_limits<float>::denorm_min(),
    std::numeric_limits<float>::max(),
};
const int kNSpecials = sizeof (kSpecials) / sizeof (kSpecials[0]);


// Reference impls -- must mirror the Inline*Float bodies in
// CtlSimdSyntaxTree.cpp exactly.  Updates here and there move together.

float ref_min (float a, float b) { return a < b ? a : b; }
float ref_max (float a, float b) { return a > b ? a : b; }
float ref_clip (float v)         { return v < 1.0f ? v : 1.0f; }

float ref_rad_to_deg (float r)
{
    return r * 180.0f / 3.14159265358979323846f;
}

float ref_deg_to_rad (float d)
{
    return d / 180.0f * 3.14159265358979323846f;
}

// CTL `sign(0) = 0`, so `copysign(x, 0) = 0` (not std::copysign's +x).
float ref_copysign (float x, float y)
{
    const float s = (y < 0.0f) ? -1.0f : (y > 0.0f ? 1.0f : 0.0f);
    return s * std::fabs (x);
}

float ref_wrap_to_360 (float h)
{
    float y = std::fmod (h, 360.0f);
    return y < 0.0f ? y + 360.0f : y;
}


void
runUnary (SimdInterpreter &interp,
          const char *fnName, const char *humanName,
          float (*ref)(float),
          const float *inputs, int n)
{
    cout << "  " << humanName << ": bit-equal across "
         << n << " edge inputs" << endl;

    FunctionCallPtr fn = interp.newFunctionCall (fnName);
    FunctionArgPtr aArg = fn->findInputArg ("a");
    FunctionArgPtr rArg = fn->findOutputArg ("r");
    REQUIRE (aArg && rArg);

    float *aData = (float *)(aArg->data());
    for (int i = 0; i < n; ++i) aData[i] = inputs[i];

    fn->callFunction (n);

    const float *rOut = (const float *)(rArg->data());
    int divergences = 0;
    for (int i = 0; i < n; ++i)
    {
        const float expect = ref (inputs[i]);
        if (!nan_or_eq (rOut[i], expect))
        {
            cerr << "  " << humanName << " divergence: in=" << inputs[i]
                 << " (0x" << std::hex << bits (inputs[i]) << std::dec << ")"
                 << "  got=" << rOut[i]
                 << " (0x" << std::hex << bits (rOut[i]) << std::dec << ")"
                 << "  ref=" << expect
                 << " (0x" << std::hex << bits (expect) << std::dec << ")"
                 << endl;
            ++divergences;
        }
    }
    REQUIRE (divergences == 0);
}


void
runBinary (SimdInterpreter &interp,
           const char *fnName, const char *humanName,
           float (*ref)(float, float),
           const float *aIn, const float *bIn, int n)
{
    cout << "  " << humanName << ": bit-equal across "
         << n << " edge pairs" << endl;

    FunctionCallPtr fn = interp.newFunctionCall (fnName);
    FunctionArgPtr aArg = fn->findInputArg ("a");
    FunctionArgPtr bArg = fn->findInputArg ("b");
    FunctionArgPtr rArg = fn->findOutputArg ("r");
    REQUIRE (aArg && bArg && rArg);

    float *aData = (float *)(aArg->data());
    float *bData = (float *)(bArg->data());
    for (int i = 0; i < n; ++i) { aData[i] = aIn[i]; bData[i] = bIn[i]; }

    fn->callFunction (n);

    const float *rOut = (const float *)(rArg->data());
    int divergences = 0;
    for (int i = 0; i < n; ++i)
    {
        const float expect = ref (aIn[i], bIn[i]);
        if (!nan_or_eq (rOut[i], expect))
        {
            cerr << "  " << humanName << " divergence: a=" << aIn[i]
                 << " b=" << bIn[i]
                 << "  got=" << rOut[i]
                 << " (0x" << std::hex << bits (rOut[i]) << std::dec << ")"
                 << "  ref=" << expect
                 << " (0x" << std::hex << bits (expect) << std::dec << ")"
                 << endl;
            ++divergences;
        }
    }
    REQUIRE (divergences == 0);
}


void
testMinMax (SimdInterpreter &interp)
{
    // Cross-product of every kSpecials value with every other.
    const int N = kNSpecials * kNSpecials;
    std::vector<float> aIn (N), bIn (N);
    for (int i = 0; i < kNSpecials; ++i)
        for (int j = 0; j < kNSpecials; ++j)
        {
            aIn[i * kNSpecials + j] = kSpecials[i];
            bIn[i * kNSpecials + j] = kSpecials[j];
        }
    runBinary (interp, "inline_test::run_min", "min(a,b)",
               ref_min, aIn.data(), bIn.data(), N);
    runBinary (interp, "inline_test::run_max", "max(a,b)",
               ref_max, aIn.data(), bIn.data(), N);
}


void
testClip (SimdInterpreter &interp)
{
    // Both above- and below-1 cases plus IEEE specials.
    const float in[] = {
        -1.5f, -1.0f, -0.5f, 0.0f, -0.0f, 0.5f, 0.99999994f,
        1.0f, 1.00000012f, 1.5f, 100.0f,
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
    };
    runUnary (interp, "inline_test::run_clip", "clip(v)",
              ref_clip, in,
              (int)(sizeof(in) / sizeof(in[0])));
}


void
testRadDeg (SimdInterpreter &interp)
{
    const float in[] = {
        0.0f, -0.0f,
        3.14159265358979323846f,        // pi
        -3.14159265358979323846f,
        1.5707963267948966f,             // pi/2
        6.283185307179586f,              // 2pi
        180.0f, -180.0f, 360.0f, -360.0f,
        1e-30f, 1e30f,
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
    };
    const int N = (int)(sizeof(in) / sizeof(in[0]));
    runUnary (interp, "inline_test::run_rad_to_deg", "radians_to_degrees(r)",
              ref_rad_to_deg, in, N);
    runUnary (interp, "inline_test::run_deg_to_rad", "degrees_to_radians(d)",
              ref_deg_to_rad, in, N);
}


void
testCopysign (SimdInterpreter &interp)
{
    // KEY edge: y = +/-0 must produce 0, NOT +/-|x| (CTL `sign(0) = 0`).
    // This is the documented divergence from C99 std::copysign.
    const float aIn[] = {
        1.0f, -1.0f,  2.5f, -2.5f, 0.0f, -0.0f,
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
        1.0f, 1.0f, 1.0f, 1.0f,    // pair with y in {+/-0, NaN, Inf}
    };
    const float bIn[] = {
        1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,
        0.0f, -0.0f,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
    };
    const int N = (int)(sizeof(aIn) / sizeof(aIn[0]));
    runBinary (interp, "inline_test::run_copysign", "copysign(x,y)",
               ref_copysign, aIn, bIn, N);

    // Spot-check the contractual divergence from std::copysign.
    {
        FunctionCallPtr fn = interp.newFunctionCall ("inline_test::run_copysign");
        FunctionArgPtr aArg = fn->findInputArg ("a");
        FunctionArgPtr bArg = fn->findInputArg ("b");
        FunctionArgPtr rArg = fn->findOutputArg ("r");
        REQUIRE (aArg && bArg && rArg);
        ((float*)aArg->data())[0] = 5.0f;
        ((float*)bArg->data())[0] = 0.0f;
        fn->callFunction (1);
        const float got = ((const float *)(rArg->data()))[0];
        cout << "  copysign(5, 0) = " << got
             << " (CTL contract is 0; std::copysign would be 5)" << endl;
        REQUIRE (got == 0.0f);
    }
}


void
testWrapTo360 (SimdInterpreter &interp)
{
    // Wraparound coverage on both sides of zero, plus exact 360 multiples
    // and IEEE specials.  fmod's sign rules vary by libm; this fixture
    // pins CTL behaviour to: result in [0, 360).
    const float in[] = {
        0.0f, -0.0f,
        180.0f, -180.0f,
        360.0f, -360.0f, 720.0f, -720.0f,
        0.5f, -0.5f,
        359.5f, -359.5f, 360.5f, -360.5f,
        1e-7f, -1e-7f,
        1e6f, -1e6f,
        std::numeric_limits<float>::quiet_NaN(),
    };
    runUnary (interp, "inline_test::run_wrap_to_360", "wrap_to_360(h)",
              ref_wrap_to_360, in,
              (int)(sizeof(in) / sizeof(in[0])));
}


} // anonymous namespace


void
testInlineHelpers ()
{
    cout << endl;
    cout << "Testing SimdCallNode inline-helper substitutions" << endl;

    SimdInterpreter interp;
    interp.loadModule ("testInlineHelpers");

    testMinMax    (interp);
    testClip      (interp);
    testRadDeg    (interp);
    testCopysign  (interp);
    testWrapTo360 (interp);

    cout << "ok" << endl;
}
