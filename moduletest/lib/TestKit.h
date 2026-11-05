#ifndef CTLTEST_TEST_KIT_H
#define CTLTEST_TEST_KIT_H

// TestKit -- a minimal CTL-native assertion surface.
//
// CTL authors can write test_*.ctl files that `import "testkit";` and call
// testkit::expect_near_f(...) / testkit::fail(...). At runtime the framework
// registers those names as SimdCFuncs against a per-thread assertion buffer;
// the Runner drains the buffer after each CTL call and translates assertions
// into Diagnostics.
//
// Public surface deliberately tiny for v0.5: `fail(string)` and
// `expect_near_f(float, float, float)`. More expect_* variants will land once
// ACES authors have a concrete need; each new one is ~20 lines of glue.

#include <string>
#include <vector>

namespace Ctl { class SimdInterpreter; }

namespace ctltest {

// One recorded assertion from a ctl_native test run. Passing assertions are
// kept so diagnostics can show what ran, not just what failed.
struct TestAssertion {
    enum class Kind { ExpectNearF, ExpectTrue, Fail };

    Kind        kind = Kind::Fail;
    bool        passed = false;
    std::string message;      // Fail kind (unexpected exception path) only.
    double      actual = 0.0; // ExpectNearF only
    double      expected = 0.0;
    double      abs_tol = 0.0;
    double      abs_err = 0.0;
};

// Factory: construct a SimdInterpreter with the testkit C funcs pre-registered
// and the "testkit" CTL module preloaded (no path entry required). Callers own
// the returned pointer. v0.5 always uses this factory for every interpreter to
// keep init costs folded into one path; the testkit symbols are only visible
// to modules that explicitly `import "testkit"`.
//
// Thread-safe (one-shot std::call_once gate around the internal type fixture).
Ctl::SimdInterpreter* newTestInterpreter();

// Drain and clear the calling thread's assertion buffer. Typical flow:
//   clearAssertions();
//   fn->callFunction(1);
//   std::vector<TestAssertion> asserts = drainAssertions();
std::vector<TestAssertion> drainAssertions();
void                       clearAssertions();

} // namespace ctltest

#endif
