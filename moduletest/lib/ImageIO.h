#ifndef CTLTEST_IMAGE_IO_H
#define CTLTEST_IMAGE_IO_H

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace ctltest {

class ImageIOError: public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Minimal float32 planar image: one named channel to width*height floats.
// Pixel at (x, y) in channel c is `channels[c][y*width + x]`.
//
// Derived from the read path in ctlrender/exr_file.cc, intentionally
// keeping only what v0.3 needs (no format_t, no dpx::fb, no compression
// selection). Reads half or float sources; always returns float32.
struct Image {
    int width  = 0;
    int height = 0;
    std::map<std::string, std::vector<float>> channels;

    size_t pixelCount() const { return static_cast<size_t>(width) * static_cast<size_t>(height); }
    bool   empty()      const { return width == 0 || height == 0 || channels.empty(); }
};

// Read every float/half channel present in `path`. Throws ImageIOError with
// diagnostic text on any I/O or decode failure.
Image readImage(const std::string& path);

// Write `img` as a float32 EXR. The channel order in the header follows the
// map's natural (alphabetical) ordering; consumers that need RGBA ordering
// should preconstruct the map with keys R,G,B,A.
void writeImage(const std::string& path, const Image& img);

} // namespace ctltest

#endif
