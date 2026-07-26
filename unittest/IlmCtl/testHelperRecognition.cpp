///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

//
// The code generator compiles a CTL function that computes one of a few
// well-known operations (min, max, clip, ...) into a direct call to a
// built-in implementation, skipping the interpreted body.  CTL reserves
// none of those names, so which definitions qualify is decided by
// reading the body, not the name.
//
// This test covers both directions of that decision:
//
//   - a definition whose body computes something else keeps its own
//     behaviour, even when it is named clip or min;
//
//   - a definition written the canonical way is still recognized, so the
//     optimization cannot quietly stop happening.
//
// The first half is the one that matters for correctness, and it is the
// half testInlineHelpers cannot cover: that fixture holds the canonical
// bodies, so it only ever exercises the case where substituted and
// interpreted agree.
//

#include <testHelperRecognition.h>
#include <CtlSimdInterpreter.h>
#include <CtlFunctionCall.h>
#include <CtlSymbolTable.h>
#include <iostream>
#include <cmath>
#include <cstdlib>

using namespace Ctl;
using namespace std;


//
// assert() compiles away under NDEBUG, which is how the Release builds
// are configured, and a test that cannot fail is worse than no test.
//

#define REQUIRE(x)                                                      \
    do {                                                                \
        if (!(x))                                                       \
        {                                                               \
            cerr << "FAILED: " << #x                                    \
                 << " at " << __FILE__ << ":" << __LINE__ << endl;      \
            exit (1);                                                   \
        }                                                               \
    } while (0)


namespace {

//
// The symbol table is protected on Interpreter, which is right for the
// public API but leaves no way to ask what the code generator decided.
// Deriving is enough, and keeps the interface unchanged.
//

class TestInterpreter: public SimdInterpreter
{
  public:

    SymbolTable &	symbols ()	{return symtab();}
};


//
// Codegen hints recorded on a function's symbol.  Mirrors the enum in
// CtlSimdSyntaxTree.cpp; -1 means nothing was recognized.
//

const int kNone	       = -1;
const int kMin	       = 0;
const int kMax	       = 1;
const int kClip	       = 2;
const int kRadToDeg    = 6;
const int kDegToRad    = 7;
const int kCopysign    = 9;
const int kWrapTo360   = 10;
const int kSign	       = 100;


void
expectHint (TestInterpreter &interp, const char *name, int expected)
{
    SymbolInfoPtr info = interp.symbols().lookupSymbol (name);

    REQUIRE (info);

    if (info->codeGenHint() != expected)
    {
        cerr << "FAILED: " << name << " has codeGenHint "
             << info->codeGenHint() << ", expected " << expected << endl;
        exit (1);
    }
}


//
// Run a one-argument entry point over a set of inputs and compare
// against what the CTL body in the fixture is supposed to produce.
//

void
expectUnary (TestInterpreter &interp,
             const char *fnName,
             const char *humanName,
             float (*expect)(float),
             const float *inputs,
             int n)
{
    cout << "  " << humanName << endl;

    FunctionCallPtr fn = interp.newFunctionCall (fnName);
    FunctionArgPtr aArg = fn->findInputArg ("a");
    FunctionArgPtr rArg = fn->findOutputArg ("r");
    REQUIRE (aArg && rArg);

    float *aData = (float *)(aArg->data());
    for (int i = 0; i < n; ++i)
        aData[i] = inputs[i];

    fn->callFunction (n);

    const float *r = (const float *)(rArg->data());
    for (int i = 0; i < n; ++i)
    {
        const float want = expect (inputs[i]);
        if (r[i] != want)
        {
            cerr << "FAILED: " << humanName << " in=" << inputs[i]
                 << " got=" << r[i] << " expected=" << want << endl;
            exit (1);
        }
    }
}


void
expectBinary (TestInterpreter &interp,
              const char *fnName,
              const char *humanName,
              float (*expect)(float, float),
              const float (*inputs)[2],
              int n)
{
    cout << "  " << humanName << endl;

    FunctionCallPtr fn = interp.newFunctionCall (fnName);
    FunctionArgPtr aArg = fn->findInputArg ("a");
    FunctionArgPtr bArg = fn->findInputArg ("b");
    FunctionArgPtr rArg = fn->findOutputArg ("r");
    REQUIRE (aArg && bArg && rArg);

    float *aData = (float *)(aArg->data());
    float *bData = (float *)(bArg->data());
    for (int i = 0; i < n; ++i)
    {
        aData[i] = inputs[i][0];
        bData[i] = inputs[i][1];
    }

    fn->callFunction (n);

    const float *r = (const float *)(rArg->data());
    for (int i = 0; i < n; ++i)
    {
        const float want = expect (inputs[i][0], inputs[i][1]);
        if (r[i] != want)
        {
            cerr << "FAILED: " << humanName
                 << " a=" << inputs[i][0] << " b=" << inputs[i][1]
                 << " got=" << r[i] << " expected=" << want << endl;
            exit (1);
        }
    }
}


//
// What the divergent bodies in the fixture actually compute.
//

float divergentClip (float v)		{ return v < 0.0f ? 0.0f : v; }
float divergentMin (float, float b)	{ return b; }
float divergentMax (float a, float b)	{ return a > b ? b : a; }
float reallyMax (float a, float b)	{ return a > b ? a : b; }
float divergentRadToDeg (float r)	{ return r * 179.0f / 3.14159265358979323846f; }
float divergentWrap (float h)		{ return fmodf (h, 360.0f); }
float clipOverBadMin (float)		{ return 1.0f; }
float canonicalClip (float v)		{ return v < 1.0f ? v : 1.0f; }


const float kUnary[] = {
    5.0f, -3.0f, 0.5f, 0.0f, -0.0f, 1.0f, -1.0f,
    400.0f, -400.0f, 359.5f, -359.5f, 720.0f,
};

const float kBinary[][2] = {
    { 2.0f, 9.0f }, { 9.0f, 2.0f }, { -1.0f, 1.0f }, { 1.0f, -1.0f },
    { 0.0f, 0.0f }, { 7.0f, 7.0f }, { -5.0f, -2.0f },
};

const int kUnaryCount  = (int)(sizeof (kUnary) / sizeof (kUnary[0]));
const int kBinaryCount = (int)(sizeof (kBinary) / sizeof (kBinary[0]));


//
// A body that computes something else must keep its own behaviour.
//

void
testDivergentBodiesAreNotReplaced (TestInterpreter &interp)
{
    cout << " definitions that compute something else are left alone"
         << endl;

    expectHint (interp, "divergent::clip",		kNone);
    expectHint (interp, "divergent::min",		kNone);
    expectHint (interp, "divergent::max",		kNone);
    expectHint (interp, "divergent::radians_to_degrees",kNone);
    expectHint (interp, "divergent::wrap_to_360",	kNone);

    //
    // clip_over_bad_min is written exactly the canonical way.  What
    // disqualifies it is the min it calls, which is not a min.  Without
    // that check it would compile into the built-in clip and the
    // divergent min would be discarded.
    //

    expectHint (interp, "divergent::clip_over_bad_min",	kNone);

    expectUnary (interp, "divergent::run_divergent_clip", "clip that clamps below zero",
                 divergentClip, kUnary, kUnaryCount);
    expectUnary (interp, "divergent::run_divergent_rad_to_deg", "rad_to_deg scaled by 179",
                 divergentRadToDeg, kUnary, kUnaryCount);
    expectUnary (interp, "divergent::run_divergent_wrap", "wrap_to_360 without the negative case",
                 divergentWrap, kUnary, kUnaryCount);
    expectUnary (interp, "divergent::run_clip_over_bad_min", "canonical clip over a divergent min",
                 clipOverBadMin, kUnary, kUnaryCount);

    expectBinary (interp, "divergent::run_divergent_min", "min that returns its second argument",
                  divergentMin, kBinary, kBinaryCount);
    expectBinary (interp, "divergent::run_divergent_max", "max with swapped returns",
                  divergentMax, kBinary, kBinaryCount);
}


//
// The name carries no weight in either direction: a function named min
// whose body computes max is recognized as max, and computes max.
//

void
testRecognitionFollowsTheBodyNotTheName (TestInterpreter &interp)
{
    cout << " recognition follows the body, not the name" << endl;

    expectHint (interp, "divergent::min_that_is_really_max", kMax);

    expectBinary (interp, "divergent::run_min_that_is_really_max",
                  "a function named min whose body computes max",
                  reallyMax, kBinary, kBinaryCount);
}


//
// The canonical bodies must still be recognized, or the optimization has
// gone away without anyone noticing.
//

void
testCanonicalBodiesAreStillRecognized (TestInterpreter &interp)
{
    cout << " canonical bodies are still recognized" << endl;

    expectHint (interp, "canonical::min",		 kMin);
    expectHint (interp, "canonical::max",		 kMax);
    expectHint (interp, "canonical::clip",		 kClip);
    expectHint (interp, "canonical::sign",		 kSign);
    expectHint (interp, "canonical::copysign",		 kCopysign);
    expectHint (interp, "canonical::radians_to_degrees", kRadToDeg);
    expectHint (interp, "canonical::degrees_to_radians", kDegToRad);
    expectHint (interp, "canonical::wrap_to_360",	 kWrapTo360);

    expectUnary (interp, "divergent::run_canonical_clip", "canonical clip",
                 canonicalClip, kUnary, kUnaryCount);
}


} // anonymous namespace


void
testHelperRecognition ()
{
    cout << endl;
    cout << "Testing which function definitions the code generator "
            "recognizes" << endl;

    TestInterpreter interp;
    interp.loadModule ("testHelperRecognition");

    testDivergentBodiesAreNotReplaced	     (interp);
    testRecognitionFollowsTheBodyNotTheName  (interp);
    testCanonicalBodiesAreStillRecognized    (interp);

    cout << "ok" << endl;
}
