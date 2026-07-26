///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

// Emit an RGB FLOAT EXR with a deterministic gradient.  Used to build
// parallel-half test fixtures larger than the 128KiB checked-in EXRs
// can provide.  NO_COMPRESSION so the bytes are stable across runs.
//
// Usage: gen_large_fixture <output.exr> <width> <height>

#include <cstdio>
#include <cstdlib>
#include <vector>

#include <ImfOutputFile.h>
#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>


int
main (int argc, char *argv[])
{
    if (argc != 4)
    {
	std::fprintf(stderr,
	             "usage: %s <output.exr> <width> <height>\n", argv[0]);
	return 1;
    }
    const char *outPath = argv[1];
    int width  = std::atoi(argv[2]);
    int height = std::atoi(argv[3]);
    if (width <= 0 || height <= 0)
    {
	std::fprintf(stderr,
	             "width and height must be positive integers\n");
	return 1;
    }

    const size_t n = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<float> r(n), g(n), b(n);

    const float invMaxX = (width  > 1) ? 1.0f / float(width  - 1) : 0.0f;
    const float invMaxY = (height > 1) ? 1.0f / float(height - 1) : 0.0f;
    for (int y = 0; y < height; ++y)
    {
	const float ny = float(y) * invMaxY;
	for (int x = 0; x < width; ++x)
	{
	    const float nx = float(x) * invMaxX;
	    const size_t i = size_t(y) * size_t(width) + size_t(x);
	    r[i] = nx;
	    g[i] = ny;
	    b[i] = (nx + ny) * 0.5f;
	}
    }

    Imf::Header header(width, height);
    header.channels().insert("R", Imf::Channel(Imf::FLOAT));
    header.channels().insert("G", Imf::Channel(Imf::FLOAT));
    header.channels().insert("B", Imf::Channel(Imf::FLOAT));
    header.compression() = Imf::NO_COMPRESSION;

    Imf::FrameBuffer fb;
    const int xstride = sizeof(float);
    const int ystride = sizeof(float) * width;
    fb.insert("R", Imf::Slice(Imf::FLOAT, (char*)r.data(), xstride, ystride));
    fb.insert("G", Imf::Slice(Imf::FLOAT, (char*)g.data(), xstride, ystride));
    fb.insert("B", Imf::Slice(Imf::FLOAT, (char*)b.data(), xstride, ystride));

    try
    {
	Imf::OutputFile out(outPath, header);
	out.setFrameBuffer(fb);
	out.writePixels(height);
    }
    catch (const std::exception &e)
    {
	std::fprintf(stderr, "OpenEXR write failed: %s\n", e.what());
	return 1;
    }
    return 0;
}
