///////////////////////////////////////////////////////////////////////////
// Copyright (c) 2013 Academy of Motion Picture Arts and Sciences 
// ("A.M.P.A.S."). Portions contributed by others as indicated.
// All rights reserved.
// 
// A worldwide, royalty-free, non-exclusive right to copy, modify, create
// derivatives, and use, in source and binary forms, is hereby granted, 
// subject to acceptance of this license. Performance of any of the 
// aforementioned acts indicates acceptance to be bound by the following 
// terms and conditions:
//
//  * Copies of source code, in whole or in part, must retain the 
//    above copyright notice, this list of conditions and the 
//    Disclaimer of Warranty.
//
//  * Use in binary form must retain the above copyright notice, 
//    this list of conditions and the Disclaimer of Warranty in the
//    documentation and/or other materials provided with the distribution.
//
//  * Nothing in this license shall be deemed to grant any rights to 
//    trademarks, copyrights, patents, trade secrets or any other 
//    intellectual property of A.M.P.A.S. or any contributors, except 
//    as expressly stated herein.
//
//  * Neither the name "A.M.P.A.S." nor the name of any other 
//    contributors to this software may be used to endorse or promote 
//    products derivative of or based on this software without express 
//    prior written permission of A.M.P.A.S. or the contributors, as 
//    appropriate.
// 
// This license shall be construed pursuant to the laws of the State of 
// California, and any disputes related thereto shall be subject to the 
// jurisdiction of the courts therein.
//
// Disclaimer of Warranty: THIS SOFTWARE IS PROVIDED BY A.M.P.A.S. AND 
// CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, 
// BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS 
// FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT ARE DISCLAIMED. IN NO 
// EVENT SHALL A.M.P.A.S., OR ANY CONTRIBUTORS OR DISTRIBUTORS, BE LIABLE 
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, RESITUTIONARY, 
// OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
// THE POSSIBILITY OF SUCH DAMAGE.
//
// WITHOUT LIMITING THE GENERALITY OF THE FOREGOING, THE ACADEMY 
// SPECIFICALLY DISCLAIMS ANY REPRESENTATIONS OR WARRANTIES WHATSOEVER 
// RELATED TO PATENT OR OTHER INTELLECTUAL PROPERTY RIGHTS IN THE ACADEMY 
// COLOR ENCODING SYSTEM, OR APPLICATIONS THEREOF, HELD BY PARTIES OTHER 
// THAN A.M.P.A.S., WHETHER DISCLOSED OR UNDISCLOSED.
///////////////////////////////////////////////////////////////////////////

#include "exr_file.hh"
#include <fstream>

//
// C99's restrict has no standard C++ spelling.  GCC and Clang expose it as
// __restrict__, MSVC as __restrict; anything else gets a no-op, since the
// qualifier is strictly an optimization hint.
//

#ifndef CTL_RESTRICT
    #if defined(_MSC_VER)
        #define CTL_RESTRICT __restrict
    #elif defined(__GNUC__) || defined(__clang__)
        #define CTL_RESTRICT __restrict__
    #else
        #define CTL_RESTRICT
    #endif
#endif


#if defined(HAVE_OPENEXR)
#include <ImfInputFile.h>
#include <ImfOutputFile.h>
#include <ImfRgbaFile.h>
#include <ImfArray.h>
#include <ImfHeader.h>
#include <ImfChannelList.h>
#include <Iex.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

void exr_read_standard_attributes(Imf::InputFile *file, format_t *format)
{
  format->exr_standard_attributes.dataWindow = file->header().dataWindow();
  format->exr_standard_attributes.displayWindow = file->header().displayWindow();
  format->exr_standard_attributes.pixelAspectRatio = file->header().pixelAspectRatio();
  format->exr_standard_attributes.screenWindowCenter = file->header().screenWindowCenter();
  format->exr_standard_attributes.screenWindowWidth = file->header().screenWindowWidth();
  format->exr_standard_attributes.lineOrder = file->header().lineOrder();
  format->exr_standard_attributes.compression = file->header().compression();

  format->is_exr_standard_attributes_set = true;
  return;
}

bool exr_read(const char *name, float scale, ctl::dpx::fb<float> *pixels,
              format_t *format) {
	std::ifstream ins;
	unsigned int magic, endian;

	ins.open(name);

    if (!ins.good())
    {
      fprintf(stderr, "WARNING on line %d of file %s in function %s(): unable to open file %s\n",
        __LINE__, __FILE__, __FUNCTION__, name);
      return false;
    }

	ins.read((char *)&magic, sizeof(magic));
	endian=0x01020304;
	if(((unsigned char *)(&endian))[0]==0x01) {
		if(magic!=0x762f3101) {
			return 0;
		}
	} else {
		if(magic!=0x01312f76) {
			return 0;
		}
	}
	//////////////////////////
    
    Imf::InputFile file(name);
    // read the exr standard attributes for potential later use in exr_write()
    exr_read_standard_attributes(&file, format);
    Imath::Box2i dw = format->exr_standard_attributes.dataWindow;
    
    if (file.header().channels().begin().channel().type == Imf::HALF)
        format->src_bps=16;
    else
        format->src_bps=32;
        
    int width = dw.max.x - dw.min.x + 1;
    int height = dw.max.y - dw.min.y + 1;

    bool has_alpha = file.header().channels().findChannel("A");

    pixels->init(width, height, has_alpha ? 4 : 3);
    Imf::PixelType pixelType = Imf::FLOAT;
    
    int xstride = sizeof (*pixels->ptr()) * pixels->depth();
    int ystride = sizeof (*pixels->ptr()) * pixels->depth() * pixels->width();
    
    Imf::FrameBuffer frameBuffer;
    frameBuffer.insert ("R",
                        Imf::Slice::Make (pixelType,
                                    (char *) pixels->ptr(),
                                    dw,
                                    xstride, ystride,
                                    1, 1,
                                    0.0));
    
    frameBuffer.insert ("G",
                        Imf::Slice::Make (pixelType,
                                    (char *) (pixels->ptr()+1),
                                    dw,
                                    xstride, ystride,
                                    1, 1,
                                    0.0));
    
    frameBuffer.insert ("B",
                        Imf::Slice::Make (pixelType,
                                    (char *) (pixels->ptr()+2),
                                    dw,
                                    xstride, ystride,
                                    1, 1,
                                    0.0));
    
    if (has_alpha){
        frameBuffer.insert ("A",
                            Imf::Slice::Make (pixelType,
                                        (char *) (pixels->ptr()+3),
                                        dw,
                                        xstride, ystride,
                                        1, 1,
                                        1.0));
    }
    
    file.setFrameBuffer(frameBuffer);
    file.readPixels(dw.min.y, dw.max.y);
    
	if(scale==0.0 || scale==1.0) {
		return 1;
	}

	float * CTL_RESTRICT p = pixels->ptr();
	const uint64_t n = pixels->count();
	for (uint64_t i = 0; i < n; i++)
		p[i] *= scale;
	return 1;
}

void exr_write(const char *name, float scale, const ctl::dpx::fb<float> &pixels,
               format_t *format, Compression *compression)
{
    if (format->bps != 32 && format->bps != 16) {
        THROW(Iex::ArgExc, "EXR files only support 16 or 32 bps at the moment.");
    }

    bool is_half = format->bps == 16 ? true : false;

    int depth = pixels.depth();
    int width = pixels.width();
    int height = pixels.height();
    float const* pixelPtr = pixels.ptr();

    // Do any scaling on a full float buffer
    ctl::dpx::fb<float> scaled_pixels;
    if (scale != 0.0 && scale != 1.0) {
        scaled_pixels.init(width, height, depth);
        scaled_pixels.alpha(1.0);

        const float* fIn = pixels.ptr();
        float* out = scaled_pixels.ptr();

        for (uint64_t i = 0; i < pixels.count(); i++) {
            *(out++) = *(fIn++) / scale;
        }
        pixelPtr = scaled_pixels.ptr();
    }

    // Generate header
    Imf::PixelType pixelType = is_half ? Imf::HALF : Imf::FLOAT;

    Imf::Header header(width, height);

    if (format->is_exr_standard_attributes_set) {
      header.dataWindow() = format->exr_standard_attributes.dataWindow;
      header.displayWindow() = format->exr_standard_attributes.displayWindow;
      header.pixelAspectRatio() = format->exr_standard_attributes.pixelAspectRatio;
      header.screenWindowCenter() = format->exr_standard_attributes.screenWindowCenter;
      header.screenWindowWidth() = format->exr_standard_attributes.screenWindowWidth;
      header.lineOrder() = format->exr_standard_attributes.lineOrder;
      if (true == format->is_compression_set) {
        // the user specified a specific compression type
        header.compression() = (Imf::Compression)compression->exrCompressionScheme;
      }
      else {
        header.compression() = format->exr_standard_attributes.compression;
      }
    }     
    else {
      header.compression() = (Imf::Compression)compression->exrCompressionScheme;
    }
    Imath::Box2i dataWindow = header.dataWindow();

    header.channels().insert("R", Imf::Channel(pixelType));
    header.channels().insert("G", Imf::Channel(pixelType));
    header.channels().insert("B", Imf::Channel(pixelType));
    if (depth == 4) {
      header.channels().insert("A", Imf::Channel(pixelType));
    }

    Imf::OutputFile file(name, header);
    Imf::FrameBuffer frameBuffer;

    ctl::dpx::fb<half> half_pixels;
    if (is_half) {
        // Create new half buffer
        half_pixels.init(pixels.width(), pixels.height(), pixels.depth());
        half_pixels.alpha(1.0);

        // Parallelize float->half using a fetch_add work-queue (same
        // pattern as transform.cc's tile loop).  OpenEXR rejects a FLOAT
        // slice when the channel is HALF so we still do the conversion
        // ourselves; scalar-serial dominated wall-clock on large exr16
        // writes (~33M samples at 4K).  Work-stealing instead of equal
        // static chunks avoids the E-core tail on heterogeneous Apple
        // Silicon (12 P-cores + 4 E-cores on M4 Max): each thread grabs
        // a small chunk, so fast cores naturally process more chunks
        // than slow cores.  Byte-identical to a serial loop.
        const float* fIn = pixelPtr;
        half* out = half_pixels.ptr();
        const uint64_t n = pixels.count();
        unsigned nthreads = std::thread::hardware_concurrency();
        if (nthreads == 0) nthreads = 1;
        // Below this size, thread-spawn overhead dominates the work.
        const uint64_t kMinToThread = 128 * 1024;  // 512 KiB of floats
        if (n < kMinToThread) nthreads = 1;

        if (nthreads <= 1) {
            for (uint64_t i = 0; i < n; i++) out[i] = half(fIn[i]);
        } else {
            // Chunk sized so each core processes several chunks and any
            // laggard only owns one small tail chunk.
            const uint64_t kChunk = 64 * 1024;  // 256 KiB of floats
            std::atomic<uint64_t> next{0};
            auto worker = [fIn, out, n, &next, kChunk]() {
                while (true) {
                    uint64_t begin = next.fetch_add(kChunk,
                        std::memory_order_relaxed);
                    if (begin >= n) return;
                    uint64_t end = begin + kChunk;
                    if (end > n) end = n;
                    for (uint64_t i = begin; i < end; i++)
                        out[i] = half(fIn[i]);
                }
            };
            std::vector<std::thread> pool;
            pool.reserve(nthreads - 1);
            for (unsigned t = 1; t < nthreads; t++) pool.emplace_back(worker);
            worker();  // main thread participates
            for (auto &th : pool) th.join();
        }

        half const* halfPixelPtr = half_pixels.ptr();

        int xstride = sizeof(*halfPixelPtr) * depth;
        int ystride = sizeof(*halfPixelPtr) * depth * width;

        // Insert the half buffer into the framebuffer
        frameBuffer.insert("R", Imf::Slice::Make(pixelType, (char*)(halfPixelPtr + 0), dataWindow, xstride, ystride));
        frameBuffer.insert("G", Imf::Slice::Make(pixelType, (char*)(halfPixelPtr + 1), dataWindow, xstride, ystride));
        frameBuffer.insert("B", Imf::Slice::Make(pixelType, (char*)(halfPixelPtr + 2), dataWindow, xstride, ystride));
        if (depth == 4) {
          frameBuffer.insert("A", Imf::Slice::Make(pixelType, (char*)(halfPixelPtr + 3), dataWindow, xstride, ystride));
        }

    }
    else {
        // No conversion needed so insert the float buffer into the frambuffer
        int xstride = sizeof(*pixelPtr) * depth;
        int ystride = sizeof(*pixelPtr) * depth * width;

        frameBuffer.insert("R", Imf::Slice::Make(pixelType, (char*)(pixelPtr + 0), dataWindow, xstride, ystride));
        frameBuffer.insert("G", Imf::Slice::Make(pixelType, (char*)(pixelPtr + 1), dataWindow, xstride, ystride));
        frameBuffer.insert("B", Imf::Slice::Make(pixelType, (char*)(pixelPtr + 2), dataWindow, xstride, ystride));
        if (depth == 4) {
          frameBuffer.insert("A", Imf::Slice::Make(pixelType, (char*)(pixelPtr + 3), dataWindow, xstride, ystride));
        }

    }

    file.setFrameBuffer(frameBuffer);
    file.writePixels(dataWindow.max.y - dataWindow.min.y + 1);

    return;
}

#else

bool exr_read(const char *name, float scale,
              ctl::dpx::fb<float> *pixels,
              format_t *bpp) {
//    fprintf(stderr, "WARNING on line %d of file %s in function %s(): exr_read() function was called but OpenEXR library was not found\n",
//    __LINE__, __FILE__, __FUNCTION__);
	return FALSE;
}

void exr_write(const char *name, float scale,
               const ctl::dpx::fb<float> &pixels,
               format_t *format,
               Compression *compression) {
//    fprintf(stderr, "WARNING on line %d of file %s in function %s(): exr_write() function was called but OpenEXR library was not found\n",
//    __LINE__, __FILE__, __FUNCTION__);
}

#endif


