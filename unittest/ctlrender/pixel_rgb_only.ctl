// RGB-only (depth 3) helper for -pixel tests.
void main(
    input varying float rIn, input varying float gIn, input varying float bIn,
    output varying float rOut, output varying float gOut, output varying float bOut)
{
    rOut = rIn + 0.1;
    gOut = gIn + 0.2;
    bOut = bIn + 0.3;
}
