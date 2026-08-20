// CTL-native escape-hatch: write tests in CTL when YAML can't express
// what you need. Each zero-arg `void test_*` function in this file is a
// separate test case, driven by the ctltest runner and reported the
// same way YAML cases are.
//
// Use this escape hatch for:
//   - loops (sweep many inputs without authoring a CSV)
//   - random sampling / property checks
//   - assertions on intermediate values inside a multi-stage computation
//   - access to private module helpers not exported from the public API
//
// Keep the file basename starting with `test_`. Keep each test function
// short -- if you need to share setup across tests, put helpers in a
// separate CTL module and import it.

ctlversion 1;

import "testkit";
import "mymodule";

namespace test_mymodule
{

// ---- Simple roundtrip: lets you see the shape of assertions in reports.
void
test_identity_is_identity()
{
    testkit::expect_near_f(mymodule::identity_f(0.25), 0.25, 1e-7);
    testkit::expect_near_f(mymodule::identity_f(-1.5), -1.5, 1e-7);
    testkit::expect_true(mymodule::identity_f(0.0) == 0.0);
}

// ---- Property check: f(x, k) should equal x*k for a range of k.
void
test_scale_is_linear_in_k()
{
    float x = 3.7;
    for (int i = -5; i <= 5; i = i + 1)
    {
        float k = float(i) * 0.25;
        float got = mymodule::scale(x, k);
        float want = x * k;
        testkit::expect_near_f(got, want, 1e-6);
    }
}

// ---- Intermediate-value check: split a pipeline and assert on the
//      middle stage. Impossible to do from YAML because the midpoint
//      is not a public output.
void
test_tonescale_midpoint_in_range()
{
    float rgb[3] = { 0.18, 0.18, 0.18 };
    float linear[3];
    mymodule::encoding_to_linear(rgb, linear);

    testkit::expect_true(linear[0] > 0.03);
    testkit::expect_true(linear[0] < 0.04);

    float display[3];
    mymodule::linear_to_display(linear, display);
    testkit::expect_near_f(display[0], 0.18, 2e-4);
}

// ---- Deliberately failing test: uncomment to see how a failure renders.
// void
// test_this_should_fail()
// {
//     testkit::expect_near_f(1.0, 2.0, 1e-6);
// }

} // namespace test_mymodule
