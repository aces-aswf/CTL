// Float-typed variant of image_with_param.ctl.  Used by tests that
// compose in/out-scale flags with in-chain gain math: half-typed
// fixtures quantize between ops, breaking byte parity for otherwise-
// equivalent invocations.  Float-typed lets the algebra cancel cleanly.
void main
    (output varying float rOut,
     output varying float gOut,
     output varying float bOut,
     input varying float rIn,
     input varying float gIn,
     input varying float bIn,
     input uniform float gain = 1.0)
{
    rOut = rIn * gain;
    gOut = gIn * gain;
    bOut = bIn * gain;
}
