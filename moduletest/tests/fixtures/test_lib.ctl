// CTL-native test fixture for the ctl_native self-test.
//
// Each `test_*` zero-arg void is one ctl_native test case. The body calls
// testkit::expect_* helpers; the framework collects results after the call.

ctlversion 1;

import "testkit";

namespace test_lib
{

// Passing case: three float comparisons, two of which demonstrate that
// the checks fire with different tolerances.
void
test_roundtrip_passes()
{
    testkit::expect_near_f(0.25 + 0.25, 0.5, 1e-6);
    testkit::expect_near_f(1.0 / 3.0, 0.33333334, 1e-6);
    testkit::expect_true(1.0 < 2.0);
}

// Exercises a property-style check: a loop aggregating expect_* calls.
void
test_loop_sum()
{
    float acc = 0.0;
    for (int i = 0; i < 10; i = i + 1)
        acc = acc + 1.0;
    testkit::expect_near_f(acc, 10.0, 0.0);
}

} // namespace test_lib
