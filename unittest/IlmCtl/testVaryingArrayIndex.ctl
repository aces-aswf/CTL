// Fixture for testVaryingArrayIndex -- a function that gathers from a
// constant lookup table using a varying integer index.  Forces the
// SIMD interpreter to use the array-index reference register
// constructor (SimdReg::SimdReg(SimdReg&, const SimdReg& indices,
// const SimdBoolMask&, ...)) which is otherwise only exercised by
// large CTL programs and is uncovered by direct C++ tests.

namespace vai_test
{

void
gather (output varying float result,
        input varying int idx)
{
    const float squares[8] = {0.0, 1.0, 4.0, 9.0, 16.0, 25.0, 36.0, 49.0};
    result = squares[idx];
}

void
gather_with_offset (output varying float result,
                    input varying int idx,
                    input uniform float offset)
{
    const float squares[8] = {0.0, 1.0, 4.0, 9.0, 16.0, 25.0, 36.0, 49.0};
    result = squares[idx] + offset;
}

}
