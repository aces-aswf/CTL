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

#if !defined(CTLRENDER_TRANSFORM_INCLUDE)
#define CTLRENDER_TRANSFORM_INCLUDE

#include <list>
#include <string>
#include <cstring>
#include <map>
#include <memory>
#include <CtlRcPtr.h>
#include <CtlInterpreter.h>
#include <CtlFunctionCall.h>
#include <CtlType.h>
#include <dpx.hh>
#include "main.hh"

namespace Ctl { class SimdInterpreter; class MetalInterpreter; }

// Per-process cache of parsed+codegen'd CTL modules, keyed on
// ctl_operation_t::filename.  Populated lazily by run_ctl_transform();
// lets a multi-file batch pay module load (150-500 ms for aces_combined)
// once per distinct CTL script instead of once per (input file x op).
// Each CTL script gets its own SimdInterpreter so that unqualified
// symbol lookups (e.g. newFunctionCall("main")) do not collide across
// scripts in the same pipeline.
struct InterpreterCache
{
    std::map<std::string, std::unique_ptr<Ctl::SimdInterpreter>> byFilename;
    InterpreterCache();
    ~InterpreterCache();

    // Eagerly parse+codegen `filename` into byFilename.  Main-thread only.
    // Call once per distinct ctl script before any parallel dispatch; after
    // that, worker threads can read byFilename without synchronization
    // because run_ctl_transform's find will always hit an existing entry.
    void preWarm(const char *filename);
};

#ifdef CTL_GPU_BACKEND
// Metal-side analogue of InterpreterCache.  Each distinct CTL file gets
// its own MetalInterpreter so that `main` in file A doesn't collide with
// `main` in file B at loadFile time (CTL language-level constraint: two
// top-level symbols of the same name in one interpreter's global scope
// raise "Name already defined in current scope").  Without this split,
// a multi -ctl chain on ctlrender-metal fails at the second loadFile.
//
// Tradeoff vs single shared MetalInterpreter: we pay MSL-source compile
// (~800 ms cold for ACES v2) once per distinct CTL file instead of once
// per process.  In the common single -ctl case there is exactly one
// entry and behaviour is identical to the pre-fix path.
struct MetalInterpreterCache
{
    std::map<std::string, std::unique_ptr<Ctl::MetalInterpreter>> byFilename;
    MetalInterpreterCache();
    ~MetalInterpreterCache();

    // Return the MetalInterpreter for `filename`, lazily creating it and
    // calling loadFile() the first time.  Module name is the filename's
    // basename without extension (matches run_ctl_transform's convention).
    Ctl::MetalInterpreter & get(const char *filename);
};
#endif

// structure to capture a CTL parameter.
// A parameter consists of a name and up to 4 floating point values.
// The count member holds the number of floating point values for this parameter
struct ctl_parameter_t
{
	ctl_parameter_t()
	{
		name = 0;
		count = 0;
		memset(value, 0, sizeof(value));
	}

	const char *name;
	uint8_t count;
	float value[4];
};

typedef std::list<ctl_parameter_t> CTLParameters;

// structure to capture the CTL filename plus parameters for the CTL
struct ctl_operation_t
{
	ctl_operation_t()
	{
		filename = 0;
	}

	const char *filename;
	CTLParameters local;
};

typedef std::list<ctl_operation_t> CTLOperations;

// Holds the named data (input pixel plane or CTL-returned output) the
// render pipeline passes between the file I/O layer and the CTL
// interpreter. The definition lives in the header because Ctl::RcPtr
// requires the full type for ref-count + delete; ctlrender-metal's
// parity.cc constructs CTLResults lists directly.
class CTLResult: public Ctl::RcObject
{
public:
	CTLResult();
	virtual ~CTLResult();

	Ctl::TypeStoragePtr data;
	bool external;
	std::string alt_name;
};
typedef Ctl::RcPtr<CTLResult> CTLResultPtr;
typedef std::list<CTLResultPtr> CTLResults;

CTLResultPtr mkresult(const char *name, const char *alt_name,
                      const ctl::dpx::fb<float> &fb, size_t offset);
void mkimage(ctl::dpx::fb<float> *image_buffer, const CTLResults &ctl_results,
             format_t *image_format);
void add_parameter_value_to_ctl_results(CTLResults *ctl_results,
                                        const ctl_parameter_t &ctl_parameter);
void set_ctl_function_argument_from_ctl_results(Ctl::FunctionArgPtr *arg,
                                                const CTLResults &ctl_results,
                                                size_t offset, size_t count);
void set_ctl_results_from_ctl_function_argument(CTLResults *ctl_results,
                                                const Ctl::FunctionArgPtr &arg,
                                                size_t offset, size_t count,
                                                size_t total);
void run_ctl_transform(Ctl::Interpreter &interpreter,
                       const ctl_operation_t &ctl_operation,
                       CTLResults *ctl_results, size_t count);

void transform(const char *inputFile, const char *outputFile,
		       float input_scale, float output_scale,
		       format_t *format,
               Compression *compression,
		       const CTLOperations &ops, const CTLParameters &global,
#ifdef CTL_GPU_BACKEND
		       MetalInterpreterCache *cache = NULL);
#else
		       InterpreterCache *cache);
#endif

// Apply a CTL operation chain to a caller-owned, pre-populated 1xN
// framebuffer and mutate it in place.  No file I/O.  Used by ctlrender's
// -pixel CLI mode: the caller fills image_buffer with RGB or RGBA
// values from the command line, we run the chain, the caller reads the
// result back out for stdout printing.  The depth of the input fb (3 or
// 4) is preserved; each CTL script's main signature determines which of
// rOut/gOut/bOut/aOut are emitted.
void transform_pixels(const CTLOperations &ops,
                      const CTLParameters &global,
                      ctl::dpx::fb<float> *image_buffer,
#ifdef CTL_GPU_BACKEND
                      MetalInterpreterCache *cache = NULL
#else
                      InterpreterCache *cache = NULL
#endif
                      );

#ifdef CTL_GPU_BACKEND
// Pipeline-split entry points for the Metal batch driver: the monolithic
// transform() above decodes, computes, and encodes in one call.  Running a
// directory of input images with `ctlrender-metal` serially leaves the
// GPU idle during CPU-bound EXR decode/encode.  Splitting the three
// phases lets main.cc run them on separate threads with bounded hand-off
// queues, overlapping decode(N+1) || GPU(N) || encode(N-1).
void transform_metal_decode(const char *inputFile,
                            float input_scale,
                            format_t *format,
                            ctl::dpx::fb<float> *image_buffer);

// The compute stage looks up the right MetalInterpreter per CTL
// operation through the cache, so multiple -ctl files each get their
// own interpreter and their `main`s don't collide.
void transform_metal_compute(MetalInterpreterCache &cache,
                             const CTLOperations &ops,
                             const CTLParameters &global,
                             format_t *format,
                             ctl::dpx::fb<float> *image_buffer);

void transform_metal_encode(const char *outputFile,
                            float output_scale,
                            format_t *format,
                            Compression *compression,
                            ctl::dpx::fb<float> *image_buffer);
#endif

#endif
