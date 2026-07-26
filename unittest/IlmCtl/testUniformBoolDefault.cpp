///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

//
// Regression tests for `input uniform bool` default-value handling.
// See testUniformBoolDefault.ctl for the fixture functions and the
// historical bug that motivated this test.
//

#include "testUniformBoolDefault.h"

#include <CtlFunctionCall.h>
#include <CtlSimdInterpreter.h>
#include <CtlType.h>

#include <cassert>
#include <iostream>
#include <vector>

using namespace Ctl;

namespace {

const char *MODULE_NAME = "testUniformBoolDefault";

//
// Helper: load the fixture module exactly once for this test.
//
SimdInterpreter &
sharedInterp ()
{
    static SimdInterpreter *interp = []
    {
        auto *ip = new SimdInterpreter;
        ip->loadModule (MODULE_NAME);
        return ip;
    } ();
    return *interp;
}

//
// Test: uniform bool with default=true.
//
//   - When setDefaultValue() is called, the function sees flag=true and
//     returns 1.0.
//   - When the host explicitly writes false, the function sees the
//     override (returns 0.0) -- the default does not silently stomp it.
//
void
testDefaultTrue ()
{
    std::cout << "  default=true, setDefaultValue() applied\n";
    {
        FunctionCallPtr fc = sharedInterp().newFunctionCall (
            "ub_test::ub_default_true");
        fc->inputArg(0)->setDefaultValue();   // flag becomes true
        fc->callFunction (1);

        float r = -42.0f;
        fc->outputArg(0)->get (&r, sizeof(float), 0, 1);
        assert (r == 1.0f);
    }

    std::cout << "  default=true, host overrides to false\n";
    {
        FunctionCallPtr fc = sharedInterp().newFunctionCall (
            "ub_test::ub_default_true");
        bool falseValue = false;
        fc->inputArg(0)->set (&falseValue, sizeof(bool), 0, 1);
        fc->callFunction (1);

        float r = -42.0f;
        fc->outputArg(0)->get (&r, sizeof(float), 0, 1);
        assert (r == 0.0f);
    }
}

//
// Test: uniform bool with default=false (the ACES ODT pattern that
// originally surfaced the bug).
//
void
testDefaultFalse ()
{
    std::cout << "  default=false, setDefaultValue() applied\n";
    {
        FunctionCallPtr fc = sharedInterp().newFunctionCall (
            "ub_test::ub_default_false");
        fc->inputArg(0)->setDefaultValue();   // flag becomes false
        fc->callFunction (1);

        float r = -42.0f;
        fc->outputArg(0)->get (&r, sizeof(float), 0, 1);
        assert (r == 0.0f);
    }

    std::cout << "  default=false, host overrides to true\n";
    {
        FunctionCallPtr fc = sharedInterp().newFunctionCall (
            "ub_test::ub_default_false");
        bool trueValue = true;
        fc->inputArg(0)->set (&trueValue, sizeof(bool), 0, 1);
        fc->callFunction (1);

        float r = -42.0f;
        fc->outputArg(0)->get (&r, sizeof(float), 0, 1);
        assert (r == 1.0f);
    }
}

//
// Test: two adjacent uniform bools.  Verifies that defaults and
// overrides for each are independent -- no cross-contamination via
// alignment or stride.
//
void
testTwoBools ()
{
    std::cout << "  two bools, both defaults (T+F gives 1.0)\n";
    {
        FunctionCallPtr fc = sharedInterp().newFunctionCall (
            "ub_test::ub_two_bools");
        fc->inputArg(0)->setDefaultValue();  // flagA becomes true
        fc->inputArg(1)->setDefaultValue();  // flagB becomes false

        fc->callFunction (1);
        float r = -42.0f;
        fc->outputArg(0)->get (&r, sizeof(float), 0, 1);
        assert (r == 1.0f);
    }

    std::cout << "  two bools, override flagB to true (T+T gives 3.0)\n";
    {
        FunctionCallPtr fc = sharedInterp().newFunctionCall (
            "ub_test::ub_two_bools");
        fc->inputArg(0)->setDefaultValue();
        bool trueValue = true;
        fc->inputArg(1)->set (&trueValue, sizeof(bool), 0, 1);

        fc->callFunction (1);
        float r = -42.0f;
        fc->outputArg(0)->get (&r, sizeof(float), 0, 1);
        assert (r == 3.0f);
    }

    std::cout << "  two bools, override flagA to false (F+F gives 0.0)\n";
    {
        FunctionCallPtr fc = sharedInterp().newFunctionCall (
            "ub_test::ub_two_bools");
        bool falseValue = false;
        fc->inputArg(0)->set (&falseValue, sizeof(bool), 0, 1);
        fc->inputArg(1)->setDefaultValue();

        fc->callFunction (1);
        float r = -42.0f;
        fc->outputArg(0)->get (&r, sizeof(float), 0, 1);
        assert (r == 0.0f);
    }
}

//
// Test: uniform bool combined with a varying float input.  The whole
// point of "uniform" is that the bool value broadcasts to every SIMD
// lane.  This is the test that would catch the originally-reported
// "random per-pixel" symptom.
//
void
testUniformBroadcastsAcrossLanes ()
{
    std::cout << "  uniform bool broadcasts across all SIMD lanes\n";

    const std::size_t N = 8;        // typical SIMD width

    {
        // flag default=true: every lane should see flag=true and return
        // its own `value`.
        FunctionCallPtr fc = sharedInterp().newFunctionCall (
            "ub_test::ub_with_varying");

        // Promote per-pixel inputs to varying.  The uniform bool stays
        // uniform -- that's the path under test.
        fc->inputArg(0)->setVarying (true);                  // value
        fc->inputArg(1)->setDefaultValue();                  // flag becomes true
        fc->outputArg(0)->setVarying (true);                 // r

        std::vector<float> values (N);
        for (std::size_t i = 0; i < N; ++i)
            values[i] = static_cast<float> (i + 1);          // 1..8
        fc->inputArg(0)->set (values.data(), sizeof(float), 0, N);

        fc->callFunction (N);

        std::vector<float> out (N, -42.0f);
        fc->outputArg(0)->get (out.data(), sizeof(float), 0, N);

        for (std::size_t i = 0; i < N; ++i)
        {
            // With flag=true, every lane returns its `value`.  If the
            // historical bug were live, some lanes would return 0.0.
            assert (out[i] == values[i]);
        }
    }

    {
        // Same fixture, host overrides flag to false.  Every lane should
        // return 0.0.
        FunctionCallPtr fc = sharedInterp().newFunctionCall (
            "ub_test::ub_with_varying");

        fc->inputArg(0)->setVarying (true);
        bool falseValue = false;
        fc->inputArg(1)->set (&falseValue, sizeof(bool), 0, 1);
        fc->outputArg(0)->setVarying (true);

        std::vector<float> values (N);
        for (std::size_t i = 0; i < N; ++i)
            values[i] = static_cast<float> (i + 1);
        fc->inputArg(0)->set (values.data(), sizeof(float), 0, N);

        fc->callFunction (N);

        std::vector<float> out (N, -42.0f);
        fc->outputArg(0)->get (out.data(), sizeof(float), 0, N);

        for (std::size_t i = 0; i < N; ++i)
            assert (out[i] == 0.0f);
    }
}

} // namespace

void
testUniformBoolDefault ()
{
    std::cout << "Testing input uniform bool default-value handling\n";
    try
    {
        testDefaultTrue ();
        testDefaultFalse ();
        testTwoBools ();
        testUniformBroadcastsAcrossLanes ();
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR -- testUniformBoolDefault: " << e.what()
                  << std::endl;
        assert (false);
    }
    std::cout << "ok\n";
}
