// Fixture module for ctltest v0.1 self-tests: small scalar + array +
// struct signatures exercising the Marshal and Oracle paths.

namespace st_scalar
{

float
identity_f(float x)
{
    return x;
}

float
add_f(float a, float b)
{
    return a + b;
}

int
add_i(int a, int b)
{
    return a + b;
}

bool
both(bool a, bool b)
{
    return a && b;
}

float
with_default(float x, float k = 2.0)
{
    return x * k;
}

void
square_v3(float v[3], output float out[3])
{
    out[0] = v[0] * v[0];
    out[1] = v[1] * v[1];
    out[2] = v[2] * v[2];
}

struct Point
{
    float x;
    float y;
};

Point
make_point(float x, float y)
{
    Point p = { x, y };
    return p;
}

float
point_dist2(Point p)
{
    return p.x * p.x + p.y * p.y;
}

// Round-trip a float through a half to expose nearest-even half rounding
// on the output side; lets selftest verify Oracle rounds authored expected
// to nearest-even half before comparing.
half
to_half(float x)
{
    half h = x;
    return h;
}

// 1D LUT summing pass -- exercises array-of-scalar table marshaling.
// size is intentionally fixed at 8 so YAML tests can author the values
// inline without hitting the large-table threshold.
float
lut_sum8(float lut[8])
{
    float s = 0.0;
    for (int i = 0; i < 8; i = i + 1)
        s = s + lut[i];
    return s;
}

// 3x3 matrix transpose -- exercises nested-array (table) marshaling on
// both the input and output sides. Requires the v1.1 path-parser fix to
// address cells like "m/i/j" correctly.
void
transpose3x3(float m[3][3], output float out[3][3])
{
    for (int i = 0; i < 3; i = i + 1)
        for (int j = 0; j < 3; j = j + 1)
            out[i][j] = m[j][i];
}

// Struct with a member array -- exercises struct to array to scalar path.
struct Tri
{
    float v[3];
    int   n;
};

Tri
make_tri(float a, float b, float c, int n)
{
    Tri t;
    t.v[0] = a;
    t.v[1] = b;
    t.v[2] = c;
    t.n    = n;
    return t;
}

float
tri_sum(Tri t)
{
    return t.v[0] + t.v[1] + t.v[2];
}

// Array of structs -- exercises array to struct to scalar path. Returns
// sum of x across the four points so the oracle can check the round-trip.
float
quad_sum_x(Point q[4])
{
    return q[0].x + q[1].x + q[2].x + q[3].x;
}

} // namespace st_scalar
