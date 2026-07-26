// Image-input fixture with a uniform parameter override hook for the
// ctlrender CLI flag tests.  Mirrors the rIn/gIn/bIn convention that
// ctlrender uses for image-typed inputs.
void main
    (output varying half rOut,
     output varying half gOut,
     output varying half bOut,
     input varying half rIn,
     input varying half gIn,
     input varying half bIn,
     input uniform float gain = 1.0)
{
    rOut = rIn * gain;
    gOut = gIn * gain;
    bOut = bIn * gain;
}
