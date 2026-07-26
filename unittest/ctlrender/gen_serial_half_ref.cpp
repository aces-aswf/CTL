///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

// Why a separate binary rather than `ctlrender -threads 1`: ctlrender's
// -threads flag controls the tile-parallel layer only; exr_file.cc's
// float->half always uses hardware_concurrency() workers above
// kMinToThread.  A true-serial reference has to come from outside
// that code path entirely.
//
// Usage: gen_serial_half_ref <input.exr> <output.exr> [compression]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <ImfInputFile.h>
#include <ImfOutputFile.h>
#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImathBox.h>
#include <half.h>


int
main (int argc, char *argv[])
{
    if (argc < 3 || argc > 4)
    {
	std::fprintf(stderr,
	             "usage: %s <input.exr> <output.exr> [none|zip|piz|...]\n",
	             argv[0]);
	return 1;
    }
    const char *inPath  = argv[1];
    const char *outPath = argv[2];
    const char *compName = (argc == 4) ? argv[3] : "none";

    Imf::Compression compression = Imf::NO_COMPRESSION;
    if      (std::strcmp(compName, "none") == 0)  compression = Imf::NO_COMPRESSION;
    else if (std::strcmp(compName, "zip")  == 0)  compression = Imf::ZIP_COMPRESSION;
    else if (std::strcmp(compName, "zips") == 0)  compression = Imf::ZIPS_COMPRESSION;
    else if (std::strcmp(compName, "piz")  == 0)  compression = Imf::PIZ_COMPRESSION;
    else
    {
	std::fprintf(stderr, "unknown compression: %s\n", compName);
	return 1;
    }

    try
    {
	Imf::InputFile in(inPath);
	const Imath::Box2i &dw = in.header().dataWindow();
	const int width  = dw.max.x - dw.min.x + 1;
	const int height = dw.max.y - dw.min.y + 1;
	const size_t n = static_cast<size_t>(width) * height;

	std::vector<float> rF(n), gF(n), bF(n);
	Imf::FrameBuffer rfb;
	rfb.insert("R", Imf::Slice(Imf::FLOAT, (char*)rF.data(),
	                           sizeof(float), sizeof(float)*width));
	rfb.insert("G", Imf::Slice(Imf::FLOAT, (char*)gF.data(),
	                           sizeof(float), sizeof(float)*width));
	rfb.insert("B", Imf::Slice(Imf::FLOAT, (char*)bF.data(),
	                           sizeof(float), sizeof(float)*width));
	in.setFrameBuffer(rfb);
	in.readPixels(dw.min.y, dw.max.y);

	std::vector<half> rH(n), gH(n), bH(n);
	for (size_t i = 0; i < n; ++i)
	{
	    rH[i] = half(rF[i]);
	    gH[i] = half(gF[i]);
	    bH[i] = half(bF[i]);
	}

	Imf::Header outHdr(width, height);
	outHdr.channels().insert("R", Imf::Channel(Imf::HALF));
	outHdr.channels().insert("G", Imf::Channel(Imf::HALF));
	outHdr.channels().insert("B", Imf::Channel(Imf::HALF));
	outHdr.compression() = compression;

	Imf::FrameBuffer wfb;
	wfb.insert("R", Imf::Slice(Imf::HALF, (char*)rH.data(),
	                           sizeof(half), sizeof(half)*width));
	wfb.insert("G", Imf::Slice(Imf::HALF, (char*)gH.data(),
	                           sizeof(half), sizeof(half)*width));
	wfb.insert("B", Imf::Slice(Imf::HALF, (char*)bH.data(),
	                           sizeof(half), sizeof(half)*width));

	Imf::OutputFile out(outPath, outHdr);
	out.setFrameBuffer(wfb);
	out.writePixels(height);
    }
    catch (const std::exception &e)
    {
	std::fprintf(stderr, "OpenEXR error: %s\n", e.what());
	return 1;
    }
    return 0;
}
