// Fixture module for ctltest v0.2 sweep self-tests.
// Small scalar signatures -- one-per-row batched through runBatch.

namespace st_sweep
{

float
scale_add(float x, float k, float b)
{
    return x * k + b;
}

void
clamp_and_abs(float x, output float clamped, output float absv)
{
    if (x < 0.0) clamped = 0.0;
    else if (x > 1.0) clamped = 1.0;
    else clamped = x;
    if (x < 0.0) absv = -x;
    else absv = x;
}

} // namespace st_sweep
