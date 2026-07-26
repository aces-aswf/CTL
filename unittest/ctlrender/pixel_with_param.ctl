// Helper for -pixel + -param tests: scales R by `scale` (default 1.0),
// passes G/B/A through.  Used to verify -param1 overrides reach the
// pixel path the same way they reach the image path.
void main(
    input varying float rIn, input varying float gIn,
    input varying float bIn, input varying float aIn,
    output varying float rOut, output varying float gOut,
    output varying float bOut, output varying float aOut,
    input uniform float scale = 1.0)
{
    rOut = rIn * scale;
    gOut = gIn;
    bOut = bIn;
    aOut = aIn;
}
