// Fixture for testConcurrentCalls -- a small varying transform whose
// per-lane result is a deterministic function of the input value, the
// uniform parameter `k`, and the lane index would NOT participate (so
// each thread's seed produces a known answer regardless of execution
// order or scheduling).

namespace concurrent_test
{

void
multiply_add(input varying float xIn,
             output varying float xOut,
             input uniform float k = 2.0)
{
    xOut = xIn * k + 1.0;
}

}
