///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

#include <CtlSimdInterpreter.h>
#include <CtlFunctionCall.h>
#include <testEdgeValues.h>
#include <testRequire.h>

#include <iostream>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <limits>

using namespace Ctl;
using namespace std;


namespace {

uint32_t
bits (float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}


bool
bitwise_equal (float a, float b)
{
    return bits(a) == bits(b);
}


// IEEE-754 specials we want to cover.  Keeping the set small but
// distinct: every combination of these operands probes a different
// precision/branch behaviour.
const float kSpecials[] = {
    0.0f,
    -0.0f,
    1.0f,
    -1.0f,
    std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::quiet_NaN(),
    std::numeric_limits<float>::min(),                     // smallest normal
    std::numeric_limits<float>::denorm_min(),              // smallest denormal
    std::numeric_limits<float>::max(),
};
const int kNSpecials = sizeof(kSpecials) / sizeof(kSpecials[0]);


void
testArithmetic (SimdInterpreter &interp)
{
    cout << "  arith(a,b): sum/diff/prod/quot bit-identical to IEEE-754 reference"
	 << endl;

    FunctionCallPtr fn = interp.newFunctionCall("edge_test::arith");
    FunctionArgPtr aArg    = fn->findInputArg("a");
    FunctionArgPtr bArg    = fn->findInputArg("b");
    FunctionArgPtr sumArg  = fn->findOutputArg("sum");
    FunctionArgPtr diffArg = fn->findOutputArg("diff");
    FunctionArgPtr prodArg = fn->findOutputArg("prod");
    FunctionArgPtr quotArg = fn->findOutputArg("quot");
    REQUIRE(aArg && bArg && sumArg && diffArg && prodArg && quotArg);

    const size_t N = static_cast<size_t>(kNSpecials) * kNSpecials;
    float *aData = (float*)(aArg->data());
    float *bData = (float*)(bArg->data());
    for (int i = 0; i < kNSpecials; ++i)
    {
	for (int j = 0; j < kNSpecials; ++j)
	{
	    aData[i * kNSpecials + j] = kSpecials[i];
	    bData[i * kNSpecials + j] = kSpecials[j];
	}
    }

    fn->callFunction(N);

    const float *sumOut  = (const float*)(sumArg->data());
    const float *diffOut = (const float*)(diffArg->data());
    const float *prodOut = (const float*)(prodArg->data());
    const float *quotOut = (const float*)(quotArg->data());

    int divergences = 0;
    for (int i = 0; i < kNSpecials; ++i)
    {
	for (int j = 0; j < kNSpecials; ++j)
	{
	    const size_t lane = static_cast<size_t>(i) * kNSpecials + j;
	    const float a = kSpecials[i], b = kSpecials[j];
	    const float refSum  = a + b;
	    const float refDiff = a - b;
	    const float refProd = a * b;
	    const float refQuot = a / b;

	    // NaN-payload tolerance: hardware NaN canonicalisation may emit
	    // a different bit pattern than the literal NaN we computed.
	    // Both being NaN is the IEEE-754 contract; the exact payload
	    // is not.
	    auto check = [&](const char *op, float got, float expect) {
		if (std::isnan(got) && std::isnan(expect)) return;
		if (!bitwise_equal(got, expect))
		{
		    cerr << "  divergence: " << op
			 << " a=" << a << " b=" << b
			 << "  got=" << got << " (0x" << std::hex << bits(got) << std::dec << ")"
			 << "  ref=" << expect << " (0x" << std::hex << bits(expect) << std::dec << ")"
			 << endl;
		    ++divergences;
		}
	    };

	    check("a+b", sumOut[lane],  refSum);
	    check("a-b", diffOut[lane], refDiff);
	    check("a*b", prodOut[lane], refProd);
	    check("a/b", quotOut[lane], refQuot);
	}
    }
    REQUIRE(divergences == 0);
}


void
testMaskedLoopRespectsBranchMask (SimdInterpreter &interp)
{
    cout << "  masked_loop: lanes branched off via `if(active)` must NOT iterate"
	 << endl;

    FunctionCallPtr fn = interp.newFunctionCall("edge_test::masked_loop");
    FunctionArgPtr nArg      = fn->findInputArg("n_in");
    FunctionArgPtr activeArg = fn->findInputArg("active");
    FunctionArgPtr resArg    = fn->findOutputArg("result");
    REQUIRE(nArg && activeArg && resArg);

    const size_t N = 8;
    int  *nIn = (int*) (nArg->data());
    bool *aIn = (bool*)(activeArg->data());
    for (size_t i = 0; i < N; ++i)
    {
	nIn[i] = 5;
	aIn[i] = (i % 2 == 0);
    }

    fn->callFunction(N);

    const int *res = (const int*)(resArg->data());
    for (size_t i = 0; i < N; ++i)
    {
	const int expected = aIn[i] ? 5 : 0;
	if (res[i] != expected)
	{
	    cerr << "  lane " << i << " active=" << aIn[i]
		 << " n_in=" << nIn[i]
		 << ": expected " << expected << " got " << res[i] << endl;
	    REQUIRE(false && "masked_loop: branch mask not respected");
	}
    }
}


void
testSeededMaskedLoop (SimdInterpreter &interp)
{
    cout << "  masked_loop_seeded: condition primed to TRUE for inactive lanes"
	 << endl;

    // Why: the masked_loop test above relies on inactive lanes' loop
    // condition being zero (arena-init default), which fails to
    // discriminate `loopMask &= condition` from `loopMask = condition`.
    // The .ctl fixture for this test primes the condition to true
    // BEFORE the outer mask filter, so inactive lanes have a non-zero
    // condition and the AND-vs-assign distinction becomes observable.
    FunctionCallPtr fn = interp.newFunctionCall("edge_test::masked_loop_seeded");
    FunctionArgPtr aArg    = fn->findInputArg("active");
    FunctionArgPtr seedArg = fn->findInputArg("seed");
    FunctionArgPtr resArg  = fn->findOutputArg("result");
    REQUIRE(aArg && seedArg && resArg);

    const size_t N = 8;
    bool *aData    = (bool*)(aArg->data());
    int  *seedData = (int*) (seedArg->data());
    for (size_t i = 0; i < N; ++i)
    {
	aData[i]    = (i % 2 == 0);
	seedData[i] = 10;
    }

    fn->callFunction(N);

    const int *res = (const int*)(resArg->data());
    for (size_t i = 0; i < N; ++i)
    {
	const int expected = aData[i] ? 5 : 0;
	if (res[i] != expected)
	{
	    cerr << "  lane " << i << " active=" << aData[i]
		 << " seed=" << seedData[i]
		 << ": expected " << expected << " got " << res[i] << endl;
	    REQUIRE(false && "masked_loop_seeded: loop ran for inactive lane");
	}
    }
}


void
testNestedBranchMaskHandling (SimdInterpreter &interp)
{
    cout << "  nested_branch: outer mask must propagate into inner branch"
	 << endl;

    FunctionCallPtr fn = interp.newFunctionCall("edge_test::nested_branch");
    FunctionArgPtr aArg = fn->findInputArg("a");
    FunctionArgPtr bArg = fn->findInputArg("b");
    FunctionArgPtr rArg = fn->findOutputArg("r");
    REQUIRE(aArg && bArg && rArg);

    const size_t N = 4;
    bool aIn[N] = {true,  false, true,  false};
    bool bIn[N] = {true,  true,  false, false};
    float expected[N] = {1.0f, -1.0f, 0.0f, -1.0f};

    bool *aData = (bool*)(aArg->data());
    bool *bData = (bool*)(bArg->data());
    for (size_t i = 0; i < N; ++i)
    {
	aData[i] = aIn[i];
	bData[i] = bIn[i];
    }

    fn->callFunction(N);

    const float *rOut = (const float*)(rArg->data());
    for (size_t i = 0; i < N; ++i)
    {
	if (rOut[i] != expected[i])
	{
	    cerr << "  lane " << i << " a=" << aIn[i] << " b=" << bIn[i]
		 << ": expected " << expected[i] << " got " << rOut[i]
		 << endl;
	    REQUIRE(false && "nested_branch: outer mask leaked into inner");
	}
    }
}


void
testMergedBranchAroundOuterMask (SimdInterpreter &interp)
{
    cout << "  merge_branch: branch-as-expression must respect outer mask"
	 << endl;

    FunctionCallPtr fn = interp.newFunctionCall("edge_test::merge_branch");
    FunctionArgPtr aArg = fn->findInputArg("a");
    FunctionArgPtr bArg = fn->findInputArg("b");
    FunctionArgPtr rArg = fn->findOutputArg("r");
    REQUIRE(aArg && bArg && rArg);

    const size_t N = 4;
    bool aIn[N] = {true, false, true,  false};
    bool bIn[N] = {true, true,  false, false};
    float expected[N] = {7.0f, -1.0f, 9.0f, -1.0f};

    bool *aData = (bool*)(aArg->data());
    bool *bData = (bool*)(bArg->data());
    for (size_t i = 0; i < N; ++i)
    {
	aData[i] = aIn[i];
	bData[i] = bIn[i];
    }

    fn->callFunction(N);

    const float *rOut = (const float*)(rArg->data());
    for (size_t i = 0; i < N; ++i)
    {
	if (rOut[i] != expected[i])
	{
	    cerr << "  lane " << i << " a=" << aIn[i] << " b=" << bIn[i]
		 << ": expected " << expected[i] << " got " << rOut[i]
		 << endl;
	    REQUIRE(false && "merge_branch: outer mask leaked into merge");
	}
    }
}


void
testCompareBranchWithNaN (SimdInterpreter &interp)
{
    cout << "  compare_branch(a,b): NaN comparisons must be false (IEEE-754)"
	 << endl;

    FunctionCallPtr fn = interp.newFunctionCall("edge_test::compare_branch");
    FunctionArgPtr aArg = fn->findInputArg("a");
    FunctionArgPtr bArg = fn->findInputArg("b");
    FunctionArgPtr rArg = fn->findOutputArg("r");
    REQUIRE(aArg && bArg && rArg);

    // Mix lanes where neither, one, or both operands are NaN.  Any
    // lane involving NaN must take the else branch (r=0.0); the
    // non-NaN lanes follow the strict less-than ordering.
    const size_t N = 6;
    float aIn[N] = { 0.0f,  1.0f,  std::nanf(""), 2.0f,         std::nanf(""), -1.0f };
    float bIn[N] = { 1.0f,  0.0f,  1.0f,          std::nanf(""), std::nanf(""), 1.0f };
    // Expected:    a<b?    a<b?   NaN<x=false    x<NaN=false   NaN<NaN=false  a<b?
    float expected[N] = {
	1.0f,   // 0 < 1
	0.0f,   // !(1 < 0)
	0.0f,   // NaN < 1: false (any NaN comparison is false)
	0.0f,   // 2 < NaN: false
	0.0f,   // NaN < NaN: false
	1.0f,   // -1 < 1
    };

    float *aData = (float*)(aArg->data());
    float *bData = (float*)(bArg->data());
    for (size_t i = 0; i < N; ++i)
    {
	aData[i] = aIn[i];
	bData[i] = bIn[i];
    }

    fn->callFunction(N);

    const float *rOut = (const float*)(rArg->data());
    for (size_t i = 0; i < N; ++i)
    {
	if (rOut[i] != expected[i])
	{
	    cerr << "  lane " << i << ": expected " << expected[i]
		 << " got " << rOut[i]
		 << " (a=" << aIn[i] << " b=" << bIn[i] << ")" << endl;
	    REQUIRE(false && "compare_branch divergence on NaN lane");
	}
    }
}

} // anonymous namespace


void
testEdgeValues ()
{
    cout << endl;
    cout << "Testing IEEE-754 edge values through SIMD interpreter" << endl;

    SimdInterpreter interp;
    interp.loadModule("testEdgeValues");

    testArithmetic(interp);
    testCompareBranchWithNaN(interp);
    testMaskedLoopRespectsBranchMask(interp);
    testSeededMaskedLoop(interp);
    testNestedBranchMaskHandling(interp);
    testMergedBranchAroundOuterMask(interp);

    cout << "ok" << endl;
}
