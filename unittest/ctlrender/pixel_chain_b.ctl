// Helper for -pixel chain tests: G *= 2, A *= 0.5, pass R/B through.
void main(
    input varying float rIn, input varying float gIn,
    input varying float bIn, input varying float aIn,
    output varying float rOut, output varying float gOut,
    output varying float bOut, output varying float aOut)
{
    rOut = rIn;
    gOut = gIn * 2.0;
    bOut = bIn;
    aOut = aIn * 0.5;
}
