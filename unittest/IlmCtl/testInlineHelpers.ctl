// Fixture for testInlineHelpers -- pins the SimdCallNode codegen
// substitution against drift in the canonical Lib.Academy.Utilities
// helper bodies.
//
// Bodies below are copied verbatim from
//   aces-core/lib/Lib.Academy.Utilities.ctl
// at sync time.  recognizeHelperBody() in CtlSimdSyntaxTree.cpp reads
// each definition's body and, when it computes one of the operations
// that has a built-in implementation, records that on the function's
// symbol; call sites then emit a SimdCCallInst routed through
// simdFunc1Arg/2Arg<Inline...Float>.  The C++ Inline bodies in that
// file are bit-identical to these CTL bodies.
//
// Because recognition reads the body, these bodies have to stay
// canonical for the substitution to happen at all.  testHelperRecognition
// covers the other side of that decision: definitions that compute
// something else keep their own behaviour.
//
// If aces-core revises a helper (e.g. wrap_to_360 starts handling
// negative angles differently), the CTL body in this fixture must be
// updated to match -- and the inline C++ in CtlSimdSyntaxTree.cpp
// audited for the same change.  The runtime tests will not catch
// fixture-vs-aces-core drift; that is a deliberate choice (this
// project does not depend on aces-core).

namespace inline_test
{

float min (float a, float b)
{
    if (a < b)
        return a;
    else
        return b;
}

float max (float a, float b)
{
    if (a > b)
        return a;
    else
        return b;
}

float clip (float v)
{
    return min (v, 1.0);
}

float radians_to_degrees (float radians)
{
    return radians * 180.0 / M_PI;
}

float degrees_to_radians (float degrees)
{
    return degrees / 180.0 * M_PI;
}

int sign (float x)
{
    int y;
    if (x < 0)        y = -1;
    else if (x > 0)   y = 1;
    else              y = 0;
    return y;
}

float copysign (float x, float y)
{
    return sign (y) * fabs (x);
}

float wrap_to_360 (float hue)
{
    float y = fmod (hue, 360.);
    if (y < 0.)
        y = y + 360.;
    return y;
}


// Wrappers exposing each substituted helper to the C++ test harness.

void run_min (input varying float a, input varying float b,
              output varying float r)
{
    r = min (a, b);
}

void run_max (input varying float a, input varying float b,
              output varying float r)
{
    r = max (a, b);
}

void run_clip (input varying float a, output varying float r)
{
    r = clip (a);
}

void run_rad_to_deg (input varying float a, output varying float r)
{
    r = radians_to_degrees (a);
}

void run_deg_to_rad (input varying float a, output varying float r)
{
    r = degrees_to_radians (a);
}

void run_copysign (input varying float a, input varying float b,
                   output varying float r)
{
    r = copysign (a, b);
}

void run_wrap_to_360 (input varying float a, output varying float r)
{
    r = wrap_to_360 (a);
}

} // namespace inline_test
