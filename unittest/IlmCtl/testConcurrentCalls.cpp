///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

#include <CtlSimdInterpreter.h>
#include <CtlFunctionCall.h>
#include <CtlType.h>
#include <testConcurrentCalls.h>
#include <testRequire.h>

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdio>

using namespace Ctl;
using namespace std;


namespace {

// Per-thread workload: own FunctionCall, own buffers, own seed.
struct Worker
{
    FunctionCallPtr	func;
    int			threadId;
    size_t		nSamples;
    float		k;            // uniform multiplier
    vector<float>	input;        // per-thread varying input
    vector<float>	output;       // per-thread varying output (read-back)
    bool		ok;
    string		failMessage;
};


void
workerBody (Worker *w,
            atomic<int> *startCount,
            int totalThreads)
{
    try
    {
	// Find input/output args.
	FunctionArgPtr xInArg  = w->func->findInputArg("xIn");
	FunctionArgPtr xOutArg = w->func->findOutputArg("xOut");
	FunctionArgPtr kArg    = w->func->findInputArg("k");
	if (!xInArg || !xOutArg || !kArg)
	{
	    w->ok = false;
	    w->failMessage = "missing arg in function signature";
	    return;
	}

	// Populate inputs with thread-distinct values.
	float *xIn = (float*)(xInArg->data());
	for (size_t i = 0; i < w->nSamples; ++i)
	    xIn[i] = w->input[i];

	float *kPtr = (float*)(kArg->data());
	kPtr[0] = w->k;

	// Release-barrier: every worker hits this point, then waits for
	// the last one to arrive before calling callFunction.  Maximizes
	// the window where multiple threads are simultaneously inside
	// the SIMD interpreter.
	startCount->fetch_add(1, memory_order_acq_rel);
	while (startCount->load(memory_order_acquire) < totalThreads)
	    this_thread::yield();

	w->func->callFunction(w->nSamples);

	// Read back outputs to the worker's own buffer (so the main
	// thread can verify after join).
	float *xOut = (float*)(xOutArg->data());
	w->output.assign(xOut, xOut + w->nSamples);

	w->ok = true;
    }
    catch (const exception &e)
    {
	w->ok = false;
	w->failMessage = string("exception: ") + e.what();
    }
    catch (...)
    {
	w->ok = false;
	w->failMessage = "unknown exception";
    }
}


void
runOneIteration (SimdInterpreter &interp,
                 const string &funcName,
                 int nThreads,
                 size_t nSamples,
                 int iterIdx)
{
    // newFunctionCall mutates interpreter state, so create on the main
    // thread before spawning workers (concurrent newFunctionCall would
    // be a separate invariant to test).
    vector<Worker> workers(nThreads);
    for (int t = 0; t < nThreads; ++t)
    {
	workers[t].func = interp.newFunctionCall(funcName);
	workers[t].threadId = t;
	workers[t].nSamples = nSamples;
	workers[t].k = 1.0f + 0.5f * static_cast<float>(t);
	workers[t].input.resize(nSamples);
	workers[t].ok = false;
	for (size_t i = 0; i < nSamples; ++i)
	    workers[t].input[i] =
		static_cast<float>(t * 1000) + static_cast<float>(i);
    }

    atomic<int> startCount(0);
    vector<thread> threads;
    threads.reserve(nThreads);
    for (int t = 0; t < nThreads; ++t)
    {
	threads.emplace_back(workerBody, &workers[t], &startCount, nThreads);
    }
    for (auto &th : threads)
	th.join();

    // Verify: each lane equals input * k + 1.0 with that thread's k.
    for (int t = 0; t < nThreads; ++t)
    {
	if (!workers[t].ok)
	{
	    fprintf(stderr,
		    "iter %d, thread %d failed: %s\n",
		    iterIdx, t, workers[t].failMessage.c_str());
	    REQUIRE(false && "worker reported failure");
	}
	for (size_t i = 0; i < nSamples; ++i)
	{
	    float expected = workers[t].input[i] * workers[t].k + 1.0f;
	    float got = workers[t].output[i];
	    if (expected != got)
	    {
		fprintf(stderr,
		        "iter %d, thread %d, lane %zu: "
		        "expected %.7f got %.7f (in=%.7f, k=%.7f)\n",
		        iterIdx, t, i, expected, got,
		        workers[t].input[i], workers[t].k);
		REQUIRE(false && "per-lane output diverges from serial expectation");
	    }
	}
    }
}

} // anonymous namespace


void
testConcurrentCalls ()
{
    cout << endl;
    cout << "Testing concurrent FunctionCall::callFunction "
	 << "across multiple threads" << endl;

    SimdInterpreter interp;
    interp.loadModule("testConcurrentCalls");
    const string funcName = "concurrent_test::multiply_add";

    // Repeat each shape so a sometimes-fires race has multiple chances.
    const int kIterations = 8;
    struct Shape { int threads; size_t samples; };
    Shape shapes[] = {
	{1,  64},      // single-thread sanity
	{2,  64},      // tiny
	{4,  1024},    // sub-tile
	{8,  4096},    // mid-range
	{8,  8192},    // == interp.maxSamples()
    };

    const size_t cap = interp.maxSamples();
    for (size_t s = 0; s < sizeof(shapes)/sizeof(shapes[0]); ++s)
    {
	if (shapes[s].samples > cap)
	{
	    cerr << "test bug: shape " << s << " sample count "
		 << shapes[s].samples << " exceeds maxSamples=" << cap
		 << endl;
	    REQUIRE(false);
	}
	cout << "  threads=" << shapes[s].threads
	     << " samples=" << shapes[s].samples
	     << " (" << kIterations << " iterations)" << endl;
	for (int it = 0; it < kIterations; ++it)
	{
	    runOneIteration(interp, funcName,
			    shapes[s].threads, shapes[s].samples, it);
	}
    }

    cout << "ok" << endl;
}
