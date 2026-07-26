// Divergent half of the testHelperRecognition fixture, plus the entry
// points the test calls.
//
// Every function here carries the name of a helper the code generator
// knows how to replace, but computes something else.  None of them may
// be recognized: if one is, a program's own definition is being
// discarded in favour of a built-in body, which is the failure this
// test exists to catch.
//
// The canonical bodies live in testHelperRecognitionCanon.ctl, because
// a module holds at most one namespace.

import "testHelperRecognitionCanon";

namespace divergent
{

// Clamps below at zero and leaves the top alone -- the opposite end
// from the canonical clip.
float clip (float v)
{
    if (v < 0.0)
	return 0.0;
    return v;
}

// Returns its second argument regardless.
float min (float a, float b)
{
    return b;
}

// Correct comparison, swapped returns.
float max (float a, float b)
{
    if (a > b)
	return b;
    else
	return a;
}

// Named min, but the body computes max.  This one *is* recognized, as
// max, because recognition follows the body.  Substituting the built-in
// max here is correct: it is what the body says.
float min_that_is_really_max (float a, float b)
{
    if (a > b)
	return a;
    else
	return b;
}

// Right shape, wrong constant.
float radians_to_degrees (float radians)
{
    return radians * 179.0 / M_PI;
}

// Canonical body with the negative case dropped.
float wrap_to_360 (float hue)
{
    float y = fmod (hue, 360.);
    return y;
}

// Written exactly the canonical way, but the min it calls is the
// divergent one above.  Must not be recognized: substituting the
// built-in clip would discard that min.
float clip_over_bad_min (float v)
{
    return min (v, 1.0);
}


//
// Entry points.
//

void run_divergent_clip (input varying float a, output varying float r)
{
    r = clip (a);
}

void run_divergent_min (input varying float a, input varying float b,
			output varying float r)
{
    r = min (a, b);
}

void run_divergent_max (input varying float a, input varying float b,
			output varying float r)
{
    r = max (a, b);
}

void run_min_that_is_really_max (input varying float a, input varying float b,
				 output varying float r)
{
    r = min_that_is_really_max (a, b);
}

void run_divergent_rad_to_deg (input varying float a, output varying float r)
{
    r = radians_to_degrees (a);
}

void run_divergent_wrap (input varying float a, output varying float r)
{
    r = wrap_to_360 (a);
}

void run_clip_over_bad_min (input varying float a, output varying float r)
{
    r = clip_over_bad_min (a);
}

void run_canonical_clip (input varying float a, output varying float r)
{
    r = canonical::clip (a);
}

void run_canonical_min (input varying float a, input varying float b,
			output varying float r)
{
    r = canonical::min (a, b);
}

}
