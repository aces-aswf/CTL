// Fixture for testEdgeValues -- exercises SIMD float arithmetic with
// IEEE-754 special values (NaN, Inf, denormals, signed zero).  The
// SIMD vector path and the scalar fallback must produce bit-identical
// outputs for every input combination, including non-finite values.

namespace edge_test
{

void
arith (input varying float a,
       input varying float b,
       output varying float sum,
       output varying float diff,
       output varying float prod,
       output varying float quot)
{
    sum  = a + b;
    diff = a - b;
    prod = a * b;
    quot = a / b;
}

void
compare_branch (input varying float a,
                input varying float b,
                output varying float r)
{
    // IEEE-754: NaN comparisons all return false.  This branch should
    // therefore take the else path on any lane where either operand is
    // NaN, regardless of the comparison operator used.
    if (a < b)
        r = 1.0;
    else
        r = 0.0;
}

void
masked_loop (input varying int n_in,
             input varying bool active,
             output varying int result)
{
    // Branch-around-loop: lanes with `active=false` must not enter the
    // while loop AT ALL.  This pins the SimdLoopInst's invariant that
    // the per-lane loop mask is the AND of the outer mask and the loop
    // condition -- not just the condition, which would silently iterate
    // for lanes the surrounding branch already filtered out.
    int count = 0;
    if (active)
    {
        int i = 0;
        while (i < n_in)
        {
            count = count + 1;
            i = i + 1;
        }
    }
    result = count;
}

void
masked_loop_seeded (input varying bool active,
                    input varying int seed,
                    output varying int result)
{
    // Stronger version of masked_loop that primes the loop's condition
    // register to TRUE for ALL lanes (including inactive ones) BEFORE
    // entering the masked region.  This forces the SimdLoopInst's
    // condition[i] to be non-zero for inactive lanes -- making the
    // mutation `loopMask[i] = condition[i]` (instead of `&=`) observable.
    //
    // For inactive lanes:
    //   - `keep_going = (seed > 0)` is assigned with the all-active
    //     mask before the if(active), so it's TRUE for inactive lanes
    //     too (when seed > 0)
    //   - inside the masked while loop, the original code AND-accumulates
    //     loopMask, so an inactive lane stays masked off
    //   - the mutation re-evaluates loopMask = condition each iteration,
    //     so an inactive lane re-enters the body whenever keep_going is
    //     true -- which it is, until the body itself sets it false
    //
    // Original output: result == 0 for inactive lanes.
    // Mutated output:  result == 5 for inactive lanes (loop ran).
    int counter = 0;
    bool keep_going = (seed > 0);
    if (active)
    {
        while (keep_going)
        {
            counter = counter + 1;
            keep_going = (counter < 5);
        }
    }
    result = counter;
}

void
nested_branch (input varying bool a,
               input varying bool b,
               output varying float r)
{
    // Two levels of branching: the inner `if (b)` runs only for lanes
    // where the outer `if (a)` selected them.  A SimdBranchInst
    // mutation that ignored the outer mask when constructing the
    // inner trueMask/falseMask would let the inner branch's body
    // run on lanes where a=false -- corrupting `r` for those lanes
    // (which should remain at the pre-branch sentinel value -1.0).
    r = -1.0;
    if (a)
    {
        if (b)
            r = 1.0;
        else
            r = 0.0;
    }
}

float
merge_inner (varying bool b)
{
    // Branch-as-expression: each side returns a value, the
    // SimdBranchInst merges the two paths into a single output reg.
    // Used by merge_branch below to exercise the neither-branch
    // memset path of the merge.
    if (b) return 7.0;
    else   return 9.0;
}

void
merge_branch (input varying bool a,
              input varying bool b,
              output varying float r)
{
    // Outer if filters lanes; inner branch-as-expression merges results
    // for the lanes that ran.  For lanes filtered out by the outer if,
    // the merge's "neither-branch" memset fills with zeros.  A mutation
    // that fills the neither-branch lanes with 0x01 (instead of 0x00)
    // would change the bit pattern of those lanes -- but since we then
    // overwrite with -1.0 in the else of the outer if, the regression
    // would only show up if the merge's intermediate result is read
    // back somewhere.  Test by making the outer-mask-filtered lanes
    // observable: they keep r = -1.0, while active lanes get 7 or 9.
    r = -1.0;
    if (a)
    {
        r = merge_inner(b);
    }
}

}
