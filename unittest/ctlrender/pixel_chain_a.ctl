// Helper for -pixel chain tests: R += 0.25, pass through GBA.
void main(
    input varying float rIn, input varying float gIn,
    input varying float bIn, input varying float aIn,
    output varying float rOut, output varying float gOut,
    output varying float bOut, output varying float aOut)
{
    rOut = rIn + 0.25;
    gOut = gIn;
    bOut = bIn;
    aOut = aIn;
}
