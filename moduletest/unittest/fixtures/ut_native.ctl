// Unit-test fixture for ctl_native dispatch + testkit drain semantics.
// Used by ctltest_unit's testTestKitDrainAndCtlNative.

ctlversion 1;

import "testkit";

namespace ut_native
{

// All assertions hold; expect 2 passing TestAssertion entries.
void
test_pass()
{
    testkit::expect_near_f(1.0, 1.0, 0.0);
    testkit::expect_true(true);
}

// First assertion holds, second fails; expect 1 passing + 1 failing.
void
test_fail()
{
    testkit::expect_near_f(1.0, 1.0, 0.0);
    testkit::expect_near_f(0.0, 1.0, 1.0e-9);
}

} // namespace ut_native
