// Fixture module for ctltest v0.3 image self-tests.
// Per-pixel RGB transform with per-channel scalar gain.

namespace st_image
{

void
rgb_gain(
    float R, float G, float B,
    output float R_out, output float G_out, output float B_out)
{
    R_out = R * 0.5;
    G_out = G * 0.25;
    B_out = B * 0.125;
}

} // namespace st_image
