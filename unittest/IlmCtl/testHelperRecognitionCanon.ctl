// Canonical half of the testHelperRecognition fixture.
//
// These are the helper bodies as aces-core writes them in
// Lib.Academy.Utilities.ctl.  The code generator must recognize every
// one of them; if it stops, the optimization has quietly gone away and
// testHelperRecognition fails.
//
// A module holds at most one namespace, which is why the canonical and
// divergent halves of this fixture are separate files.
//
// Keep these bodies identical to the ones in testInlineHelpers.ctl.  If
// aces-core revises a helper, both fixtures and the implementations in
// CtlSimdSyntaxTree.cpp move together.

namespace canonical
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

int sign (float x)
{
    int y;
    if (x < 0)
    {
	y = -1;
    }
    else if (x > 0)
    {
	y = 1;
    }
    else
    {
	y = 0;
    }

    return y;
}

float copysign (float x, float y)
{
    return sign (y) * fabs (x);
}

float radians_to_degrees (float radians)
{
    return radians * 180.0 / M_PI;
}

float degrees_to_radians (float degrees)
{
    return degrees / 180.0 * M_PI;
}

float wrap_to_360 (float hue)
{
    float y = fmod (hue, 360.);
    if (y < 0.)
    {
	y = y + 360.;
    }
    return y;
}

}
