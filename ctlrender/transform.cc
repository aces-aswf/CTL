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

#include "transform.hh"
#include "dpx_file.hh"
#include "tiff_file.hh"
#include "exr_file.hh"
#include "aces_file.hh"
#include <CtlFunctionCall.h>
#ifdef CTL_GPU_BACKEND
#include <CtlMetalInterpreter.h>
#else
#include <CtlSimdInterpreter.h>
#endif
#include <CtlStdType.h>
#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <Iex.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
	#define strcasecmp _stricmp
#endif

extern int num_threads;
extern int no_batch_math;

// Note: CTLResult definition now lives in transform.hh so that
// ctlrender-metal's parity.cc can construct CTLResults lists directly.

CTLResult::CTLResult() :
		Ctl::RcObject()
{
	external = FALSE;
}

CTLResult::~CTLResult()
{
}


// This function is used to add to the result list parameters that
// are specified on the command line. A parameter that has been returned
// by a CTL function takes precedence
void add_parameter_value_to_ctl_results(CTLResults *ctl_results, const ctl_parameter_t &ctl_parameter)
{
	CTLResults::iterator results_iter;
	Ctl::ArrayTypePtr array;
	Ctl::DataTypePtr type;
	CTLResultPtr ctl_result;

	// lookup a named data element that matches the parameter name.
	for (results_iter = ctl_results->begin(); results_iter != ctl_results->end(); results_iter++)
	{
		if ((*results_iter)->data->name() == ctl_parameter.name)
		{
			break;
		}
	}

	if (results_iter != ctl_results->end())
	{
		// result data found
		ctl_result = *results_iter;

		// if result data was set by a CTL function (i.e. external == false), we preserve its value
		if (!ctl_result->external)
		{
			// Set by a CTL function. No way are we over writing this...
			return;
		}
		// It's a command line argument, so we (re)set it (this happens
		// if the user has specified the same parameter more than once
		// (either due to user error or by overriding a global value with
		// a local one).
	}
	else
	{
		// ctl result data not found, so we create a new ctl result and add it to the list (the ctl data value is added below)
		ctl_result = CTLResultPtr(new CTLResult);
		ctl_result->external = TRUE;
		ctl_results->push_back(ctl_result);
	}

	if (ctl_parameter.count == 1)
	{
		ctl_result->data = new Ctl::DataArg(ctl_parameter.name, new Ctl::StdFloatType(), 1);
		ctl_result->data->set(&(ctl_parameter.value[0]));
	}
	else
	{
		type = new Ctl::StdArrayType(new Ctl::StdFloatType(), ctl_parameter.count);
		ctl_result->data = new Ctl::DataArg(ctl_parameter.name, type, 1);
		for (uint8_t i = 0; i < ctl_parameter.count; i++)
		{
			ctl_result->data->set(&(ctl_parameter.value[i]), 0, 0, 1, "%d", i);
		}
	}
	ctl_results->push_back(ctl_result);
}

void set_ctl_function_argument_from_ctl_results(Ctl::FunctionArgPtr *arg, const CTLResults &ctl_results, size_t offset, size_t count)
{
	CTLResults::const_iterator results_iter;
	Ctl::TypeStoragePtr src;
	Ctl::FunctionArgPtr dst;

	dst = *arg;

	for (results_iter = ctl_results.begin(); results_iter != ctl_results.end(); results_iter++)
	{
		if ((*results_iter)->data->name() == dst->name() || (*results_iter)->alt_name == dst->name())
		{
			break;
		}
	}

	if (results_iter == ctl_results.end())
	{
		if (dst->hasDefaultValue())
		{
			dst->setDefaultValue();
		}
		else
		{
			THROW(Iex::ArgExc, "A value for the CTL input variable '" << dst->name() << "' does not exist, was not specified on the command line,\nand does not have a default value. A value can be provided at runtime using command line parameters. See '-help param'\n");
			throw(std::exception());
		}
		return;
	}

	src = (*results_iter)->data;
	if (!dst->isVarying())
	{
		if (offset == 0)
		{
			dst->copy(src, 0, 0, 1);
		}
		return;
	}
	else
	{
		if (!src->isVarying())
		{
			for (size_t i = 0; i < dst->elements(); i++)
			{
				dst->copy(src, 0, i, 1);
			}
		}
		else
		{
			dst->copy(src, offset, 0, count);
		}
	}
}

void set_ctl_results_from_ctl_function_argument(CTLResults *ctl_results, const Ctl::FunctionArgPtr &arg, size_t offset, size_t count, size_t total)
{
	CTLResults::iterator results_iter;
	CTLResultPtr ctl_result;

	for (results_iter = ctl_results->begin(); results_iter != ctl_results->end(); results_iter++)
	{
		if ((*results_iter)->data->name() == arg->name())
		{
			break;
		}
	}

//	fprintf(stderr, "copying %d@%d (total %d) of %s\n", count, offset, total, arg->name().c_str());
	if (!arg->isVarying())
	{
		// For constant return arguments we only do this the first time
		// through
		if (offset != 0)
		{
			return;
		}
		total = 1;
		count = 1;
	}

	// Since the ctl_results list gets rebuilt after the end of ctl_run_transform
	// we don't have to worry about having this function getting called
	// twice with the FunctionArgPtr having different types (for a given
	// output argument name).
	if (results_iter != ctl_results->end())
	{
		ctl_result = *results_iter;

		std::string inputName;
		if (arg->name() == "rOut")
		{
			inputName = "rIn";
		}
		else if (arg->name() == "gOut")
		{
			inputName = "gIn";
		}
		else if (arg->name() == "bOut")
		{
			inputName = "bIn";
		}
		else if (arg->name() == "aOut")
		{
			inputName = "aIn";
		}

		if (!inputName.empty())
		{
			for (results_iter = ctl_results->begin(); results_iter != ctl_results->end(); results_iter++)
			{
				if ((*results_iter)->data->name() == inputName)
				{
					break;
				}
			}

			if (results_iter != ctl_results->end())
			{
				CTLResultPtr ctl_result_output_to_input;
				ctl_result_output_to_input = *results_iter;
				ctl_result_output_to_input->data->copy(arg, 0, offset, count);
			}

		}
	}
	else
	{
		ctl_result = CTLResultPtr(new CTLResult());
		ctl_result->data = new Ctl::DataArg(arg->name(), arg->type(), total);
		ctl_results->push_back(ctl_result);

		std::string inputName;
		if (arg->name() == "rOut")
		{
			inputName = "rIn";
		}
		else if (arg->name() == "gOut")
		{
			inputName = "gIn";
		}
		else if (arg->name() == "bOut")
		{
			inputName = "bIn";
		}
		else if (arg->name() == "aOut")
		{
			inputName = "aIn";
		}

		if (!inputName.empty())
		{
			CTLResultPtr ctl_result_input;

			ctl_result_input = CTLResultPtr(new CTLResult());
			ctl_result_input->data = new Ctl::DataArg(inputName, arg->type(), total);
			ctl_results->push_back(ctl_result_input);
			ctl_result_input->data->copy(arg, 0, offset, count);
		}
	}

	ctl_result->data->copy(arg, 0, offset, count);
}

#ifndef CTL_GPU_BACKEND
// Out-of-line so the std::map<..., std::unique_ptr<SimdInterpreter>> member's
// destructor is instantiated in this TU where SimdInterpreter is complete.
InterpreterCache::InterpreterCache() = default;
InterpreterCache::~InterpreterCache() = default;

void InterpreterCache::preWarm(const char *filename)
{
    auto &slot = byFilename[std::string(filename)];
    if (!slot)
    {
        slot.reset(new Ctl::SimdInterpreter);
        slot->setUseBatchedMath(!no_batch_math);
        slot->loadFile(filename);
    }
}
#endif

#ifdef CTL_GPU_BACKEND
// Out-of-line so the std::map<..., std::unique_ptr<MetalInterpreter>> member's
// destructor is instantiated in this TU where MetalInterpreter is complete.
MetalInterpreterCache::MetalInterpreterCache() = default;
MetalInterpreterCache::~MetalInterpreterCache() = default;

Ctl::MetalInterpreter &
MetalInterpreterCache::get(const char *filename)
{
    auto &slot = byFilename[std::string(filename)];
    if (!slot)
    {
        // Derive the module name (basename without extension) so
        // loadFile registers the file under a stable name and
        // moduleIsLoaded() is cheap on repeat calls.
        std::string mod(filename);
        size_t s = mod.find_last_of("/\\");
        if (s != std::string::npos) mod = mod.substr(s + 1);
        size_t d = mod.find_last_of('.');
        if (d != std::string::npos) mod.resize(d);

        slot.reset(new Ctl::MetalInterpreter);
        if (!slot->moduleIsLoaded(mod))
            slot->loadFile(filename, mod);
    }
    return *slot;
}
#endif

#ifdef CTL_GPU_BACKEND
void run_ctl_transform(Ctl::Interpreter &shared_interpreter,
                       const ctl_operation_t &ctl_operation,
                       CTLResults *ctl_results, size_t count)
#else
void run_ctl_transform(const ctl_operation_t &ctl_operation,
                       CTLResults *ctl_results, size_t count,
                       InterpreterCache *cache)
#endif
{
	Ctl::Interpreter *interpreter = nullptr;
	Ctl::FunctionCallPtr fn;
	Ctl::FunctionArgPtr arg;
	CTLResults::iterator results_iter;
	char *name = NULL;
	char *module;
	char *slash;
	char *dot;
	CTLResults new_ctl_results;

	try
	{

		name = (char *) alloca(strlen(ctl_operation.filename)+1);
		memset(name, 0, strlen(ctl_operation.filename) + 1);
		strcpy(name, ctl_operation.filename);

#ifdef WIN32
		char *backslash = strrchr(name, '\\');
		char *forwardslash = strrchr(name, '/');
		slash = backslash != NULL ? backslash : forwardslash;
#else
		slash = strrchr(name, '/');
#endif
		if (slash == NULL)
		{
			module = name;
		}
		else
		{
			module = slash + 1;
		}

		dot = strrchr(module, '.');
		if (dot != NULL)
		{
			*dot = 0;
		}

#ifdef CTL_GPU_BACKEND
        //
        // Use the CTL file's basename as the module name so repeated
        // calls against a shared interpreter (hoisted out of the
        // per-file loop in main()) can skip re-parsing and re-compiling
        // the same source. Without an explicit module name, loadFile
        // generates a random one every call and would re-register all
        // top-level symbols, which either collides ("already defined
        // in current scope") or silently recompiles the MSL every file.
        //
        if (!shared_interpreter.moduleIsLoaded(module))
            shared_interpreter.loadFile(ctl_operation.filename, module);
        interpreter = &shared_interpreter;
#else
        // Reuse any cached, already-loaded interpreter for this script.
        // Each distinct ctl_operation.filename gets its own interpreter so
        // that unqualified newFunctionCall("main") lookups do not collide
        // across scripts in the same pipeline.
        {
            auto it = cache->byFilename.find(ctl_operation.filename);
            if (it == cache->byFilename.end())
            {
                auto fresh = std::unique_ptr<Ctl::SimdInterpreter>(
                    new Ctl::SimdInterpreter);
                fresh->setUseBatchedMath(!no_batch_math);
                fresh->loadFile(ctl_operation.filename);
                it = cache->byFilename.emplace(
                    std::string(ctl_operation.filename),
                    std::move(fresh)).first;
            }
            interpreter = it->second.get();
        }
#endif

        try
        {
            // It's probably broken that you can't get a list of the function
            // calls from a file. It's an article of faith that the primary
            // function of a ctl script is named the same as the base ctl script
            // name (without '.ctl' extension). We deal with this by looking
            // for a 'main' function, and failing that, a function named whatever
            // the ctl file is named. This is probably not ideal. The 'main'
            // function convention is used by 'toxik'
            fn = interpreter->newFunctionCall(std::string("main"));
        }
        catch (const Iex::ArgExc &e)
        {
            // XXX CTL library needs to be changed so that we have a better
            // XXX 'function not exists' exception.
            if (verbosity > 1) {
                fprintf(stderr, "No function named main() found, trying <module_name> (%s) instead\n", module);
            }
        }

        try {
            if (fn.refcount() == 0)
            {
                fn = interpreter->newFunctionCall(std::string(module));
            }
        } catch (...) {
			char message_text[512] = {'\0'};
			snprintf( message_text, 512, "CTL file must contain either a main or <module_name> (%s) function", module);
            THROW(Iex::ArgExc, message_text);
        }

		if (fn->returnValue()->type().cast<Ctl::VoidType>().refcount() == 0)
		{
			THROW(Iex::ArgExc, "CTL main (or <module_name>) function must return a 'void'");
		}

		if (verbosity > 1)
		{
			fprintf(stderr, "   ctl script file: %s\n", ctl_operation.filename);
			fprintf(stderr, "     function name: %s\n", fn->name().c_str());

			for (size_t i = 0; i < fn->numInputArgs(); i++)
			{
				arg = fn->inputArg(i);
				if (i == 0)
				{
					fprintf(stderr, "   input arguments:\n");
				}
				fprintf(stderr, "%18s: %s", arg->name().c_str(), arg->type()->asString().c_str());

				if (arg->isVarying())
				{
					fprintf(stderr, " (varying)");
				}

				if (arg->hasDefaultValue())
				{
					fprintf(stderr, " (defaulted)");
				}

				fprintf(stderr, "\n");
			}

			for (size_t i = 0; i < fn->numOutputArgs(); i++)
			{
				arg = fn->outputArg(i);
				if (i == 0)
				{
					fprintf(stderr, "  output arguments:\n");
				}

				fprintf(stderr, "%18s: %s", arg->name().c_str(), arg->type()->asString().c_str());

				if (arg->isVarying())
				{
					fprintf(stderr, " (varying)");
				}

				if (arg->hasDefaultValue())
				{
					fprintf(stderr, " (defaulted)");
				}

				fprintf(stderr, "\n");
			}
			fprintf(stderr, "\n");
		}

		//	fprintf(stderr, "%d samples to go.\n", count);

		const size_t max_samples = interpreter->maxSamples();
		auto process_tile = [&](Ctl::FunctionCallPtr tile_fn, size_t offset, size_t pass)
		{
			for (size_t i = 0; i < tile_fn->numInputArgs(); i++)
			{
				Ctl::FunctionArgPtr tile_arg = tile_fn->inputArg(i);
				set_ctl_function_argument_from_ctl_results(&tile_arg, *ctl_results, offset, pass);
			}
			tile_fn->callFunction(pass);
			for (size_t i = 0; i < tile_fn->numOutputArgs(); i++)
			{
				set_ctl_results_from_ctl_function_argument(&new_ctl_results, tile_fn->outputArg(i), offset, pass, count);
			}
		};

		// First tile runs serially on fn. This call is what builds out
		// new_ctl_results' entries (list structure mutation); later tiles
		// only update disjoint slices of existing entries, so the list
		// structure is stable during any parallel phase below.
		size_t offset = 0;
		if (offset < count)
		{
			size_t pass = max_samples;
			if (pass > (count - offset)) pass = (count - offset);
			process_tile(fn, offset, pass);
			offset += pass;
		}

		// Determine worker count. num_threads<=0 means autoselect; 1 keeps
		// the current single-threaded behavior for bit-exact reproducibility.
		size_t worker_count = 1;
		if (num_threads > 1)
		{
			worker_count = static_cast<size_t>(num_threads);
		}
		else if (num_threads <= 0)
		{
			unsigned hw = std::thread::hardware_concurrency();
			if (hw > 1) worker_count = hw;
		}

		if (offset < count && worker_count > 1)
		{
			// Pre-compute the remaining tile boundaries so workers just
			// fetch_add an index into this vector.
			std::vector<std::pair<size_t, size_t> > tiles; // (offset, pass)
			for (size_t o = offset; o < count; )
			{
				size_t p = max_samples;
				if (p > (count - o)) p = (count - o);
				tiles.emplace_back(o, p);
				o += p;
			}

			// Cap workers to tile count -- more threads than tiles wastes
			// spawn cost for threads that do no work.
			if (worker_count > tiles.size()) worker_count = tiles.size();

			// One FunctionCall per worker. Main thread is worker 0 and
			// reuses `fn`; others get a fresh newFunctionCall. The
			// Interpreter serializes newFunctionCall() internally, so
			// creating them before spawn is safe.
			std::vector<Ctl::FunctionCallPtr> worker_fns;
			worker_fns.reserve(worker_count);
			worker_fns.push_back(fn);
			for (size_t w = 1; w < worker_count; w++)
			{
				Ctl::FunctionCallPtr wfn = interpreter->newFunctionCall(fn->name());
				worker_fns.push_back(wfn);

				// Prime each fresh FunctionCall with any uniform / default
				// inputs. The per-tile loop only sets varying inputs each
				// tile; uniform inputs are copied once at offset=0 and
				// then persist. Workers that only ever see offset>0 tiles
				// would otherwise have uninitialised uniforms.
				for (size_t i = 0; i < wfn->numInputArgs(); i++)
				{
					Ctl::FunctionArgPtr warg = wfn->inputArg(i);
					set_ctl_function_argument_from_ctl_results(&warg, *ctl_results, 0, max_samples);
				}
			}

			std::atomic<size_t> next_tile(0);
			std::atomic<bool>   aborted(false);
			std::mutex          err_mutex;
			std::exception_ptr  first_err;

			auto worker = [&](size_t widx)
			{
				try
				{
					Ctl::FunctionCallPtr wfn = worker_fns[widx];
					while (!aborted.load(std::memory_order_relaxed))
					{
						size_t idx = next_tile.fetch_add(1, std::memory_order_relaxed);
						if (idx >= tiles.size()) return;
						process_tile(wfn, tiles[idx].first, tiles[idx].second);
					}
				}
				catch (...)
				{
					std::lock_guard<std::mutex> lock(err_mutex);
					if (!first_err) first_err = std::current_exception();
					aborted.store(true, std::memory_order_relaxed);
				}
			};

			std::vector<std::thread> pool;
			pool.reserve(worker_count - 1);
			for (size_t w = 1; w < worker_count; w++) pool.emplace_back(worker, w);
			worker(0);
			for (auto &t : pool) t.join();

			if (first_err) std::rethrow_exception(first_err);
		}
		else
		{
			// Single-threaded path (bit-exact with pre-Phase-B behaviour).
			while (offset < count)
			{
				size_t pass = max_samples;
				if (pass > (count - offset)) pass = (count - offset);
				process_tile(fn, offset, pass);
				offset += pass;
			}
		}
		*ctl_results = new_ctl_results;
	}
	catch (...)
	{
//		if(name!=NULL) {
//			free(name);
//		}
		throw;
	}
}


// Creates a new ctl result object from the image buffer (fb parameter) passed in
// Copies a new CTL result (block of data) from the framebuffer fb.
CTLResultPtr mkresult(const char *name, const char *alt_name, const ctl::dpx::fb<float> &fb, size_t offset)
{
	CTLResultPtr new_result = CTLResultPtr(new CTLResult());

	// DataArg represents a raw block of data
	// XXX - seems like last argument to Ctl::DataArg should be (fb.pixels() + offset) because the argument represents the data size (i.e. number of bytes)
	new_result->data = Ctl::DataArgPtr(new Ctl::DataArg(name, Ctl::DataTypePtr(new Ctl::StdFloatType()), fb.pixels()));

	if (alt_name != NULL)
	{
		new_result->alt_name = alt_name;
	}

	new_result->data->set(fb.ptr() + offset, sizeof(float) * fb.depth(), 0, fb.pixels());

	return new_result;
}

void mkimage(ctl::dpx::fb<float> *image_buffer, const CTLResults &ctl_results, format_t *image_format)
{
	enum have_channel_e
	{
		// These need to be in the order that you want them in the file...
		have_x = 0,
		have_y = 1,
		have_z = 2,
		have_r = 3,
		have_g = 4,
		have_b = 5,
		have_a = 6,
		have_xout = 8,
		have_yout = 9,
		have_zout = 10,
		have_rout = 11,
		have_gout = 12,
		have_bout = 13,
		have_aout = 14,
		mask_x = 1 << have_x,
		mask_y = 1 << have_y,
		mask_z = 1 << have_z,
		mask_r = 1 << have_r,
		mask_g = 1 << have_g,
		mask_b = 1 << have_b,
		mask_a = 1 << have_a,
		mask_xout = 1 << have_xout,
		mask_yout = 1 << have_yout,
		mask_zout = 1 << have_zout,
		mask_rout = 1 << have_rout,
		mask_gout = 1 << have_gout,
		mask_bout = 1 << have_bout,
		mask_aout = 1 << have_aout,
		have_none = 33,
	};

	CTLResults::const_iterator results_iter;
	CTLResultPtr channels[16];
	int channels_mask;
	have_channel_e channel;
	const char *channel_name;
	uint8_t on_channel;
	uint8_t channel_count;
	uint8_t c;
	CTLResultPtr ctl_result;

	// These need to be in the order for preferred output formats...
	// The DPX colorimetric is in the top 8 bits
	int tests[] =
	{
			(51 << 24) | (mask_rout | mask_gout | mask_bout | mask_aout), (50 << 24)
			| (mask_rout | mask_gout | mask_bout), (158 << 24)
			| (mask_xout | mask_yout | mask_zout | mask_aout), (157 << 24)
			| (mask_xout | mask_yout | mask_zout), (159 << 24)
			| (mask_yout | mask_aout), (6 << 24) | (mask_yout), (162 << 24)
			| (mask_gout | mask_aout), (2 << 24) | (mask_gout), (161 << 24)
			| (mask_bout | mask_aout), (3 << 24) | (mask_bout), (160 << 24)
			| (mask_rout | mask_aout), (1 << 24) | (mask_rout), (4 << 24)
			| (mask_aout),

	        (51 << 24) | (mask_r | mask_g | mask_b | mask_a), (50 << 24)
			| (mask_r | mask_g | mask_b), (158 << 24)
			| (mask_x | mask_y | mask_z | mask_a), (159 << 24)
			| (mask_y | mask_a), (6 << 24) | (mask_y), (162 << 24)
			| (mask_g | mask_a), (2 << 24) | (mask_g), (161 << 24)
			| (mask_b | mask_a), (3 << 24) | (mask_b), (160 << 24)
			| (mask_r | mask_a), (1 << 24) | (mask_r), (4 << 24) | (mask_a),

	        0
	};

	channels_mask = 0;
	for (results_iter = ctl_results.begin(); results_iter != ctl_results.end(); results_iter++)
	{
		ctl_result = *results_iter;
		if (!ctl_result->data->isVarying() && ctl_result->data->elements() != image_buffer->pixels())
		{
			continue;
		}
		channel = have_none;
		channel_name = ctl_result->data->name().c_str();
		if (0)
		{
		}
//		else if(!strcasecmp("X", channel_name)) { channel=have_x; } 
//		else if(!strcasecmp("Y", channel_name)) { channel=have_y; } 
//		else if(!strcasecmp("Z", channel_name)) { channel=have_z; } 
//		else if(!strcasecmp("A", channel_name)) { channel=have_a; } 
//		else if(!strcasecmp("R", channel_name)) { channel=have_r; } 
//		else if(!strcasecmp("G", channel_name)) { channel=have_g; } 
//		else if(!strcasecmp("B", channel_name)) { channel=have_b; } 
//		else if(!strcasecmp("outX", channel_name)) { channel=have_xout; } 
//		else if(!strcasecmp("outY", channel_name)) { channel=have_yout; } 
//		else if(!strcasecmp("outZ", channel_name)) { channel=have_zout; } 
		else if (!strcasecmp("aOut", channel_name))
		{
			channel = have_aout;
		}
		else if (!strcasecmp("rOut", channel_name))
		{
			channel = have_rout;
		}
		else if (!strcasecmp("gOut", channel_name))
		{
			channel = have_gout;
		}
		else if (!strcasecmp("bOut", channel_name))
		{
			channel = have_bout;
		}

		if (channel == have_none)
		{
			continue;
		}

		if (ctl_result->data->type().cast<Ctl::HalfType>().refcount() == 0
				&& ctl_result->data->type().cast<Ctl::FloatType>().refcount() == 0)
		{
			THROW(Iex::ArgExc, "CTL script not providing half or float as the output data type.");
		}
		channels[channel] = ctl_result;
		channels_mask = channels_mask | (1 << channel);
	}

	for (c = 0; tests[c] != 0; c++)
	{
		if ((channels_mask & tests[c] & 0x00ffffff) == (tests[c] & 0x00ffffff))
		{
			channels_mask = tests[c];
			break;
		}
	}

	if (tests[c] == 0)
	{
		THROW(Iex::ArgExc, "Unable to determine what channels from the CTL script output should be saved.");
	}

	channel_count = 0;
	for (c = 0; c < 24; c++)
	{
		if (channels_mask & (1 << c))
		{
			channel_count++;
		}
	}

	if (image_buffer->depth() != channel_count)
	{
		image_buffer->init(image_buffer->width(), image_buffer->height(), channel_count);
	}

	on_channel = 0;
	for (c = 0; c < 24; c++)
	{
		if (channels_mask & (1 << c))
		{
			channels[c]->data->get(image_buffer->ptr() + on_channel, sizeof(float) * channel_count, 0, image_buffer->pixels());
			on_channel++;
		}
	}

	if (image_format->descriptor == 0)
	{
		image_format->descriptor = (channels_mask & 0xff000000) >> 24;
	}
}

// Currently we have no thread support. This would be nice but we will
// deal with it on a per-input-file basis. The format is passed in as 
// a pointer since there are fields in it that may be filled out by the
// reader / writer and those will probably want to migrate back to the
// calling function.
void transform(const char *inputFile, const char *outputFile,
		       float input_scale, float output_scale,
		       format_t *image_format,
               Compression *compression,
		       const CTLOperations &ctl_operations,
		       const CTLParameters &global_parameters,
#ifdef CTL_GPU_BACKEND
		       MetalInterpreterCache *cache)
#else
		       InterpreterCache *cache)
#endif
{
	CTLOperations::const_iterator operations_iter;
	ctl_operation_t ctl_operation;
	CTLParameters::const_iterator parameters_iter;
	uint8_t i;
	uint32_t j;
	std::string error;
	ctl::dpx::fb<float> image_buffer;

	if (verbosity > 1)
	{
		fprintf(stdout, "       source file: %s\n", inputFile);
		fprintf(stdout, "  destination file: %s\n", outputFile);
		fprintf(stderr, "destination format: %s\n", image_format->ext);
		fprintf(stderr, "       input scale: ");
		if (input_scale == 0.0)
		{
			fprintf(stderr, "default\n");
		}
		else
		{
			fprintf(stderr, "%f\n", input_scale);
		}
		fprintf(stderr, "      output scale: ");
		if (output_scale == 0.0)
		{
			fprintf(stderr, "default\n");
		}
		else
		{
			fprintf(stderr, "%f\n", output_scale);
		}
		if (verbosity > 2)
		{
			i = 0;
			for (parameters_iter = global_parameters.begin(); parameters_iter != global_parameters.end(); parameters_iter++)
			{
				if (i == 0)
				{
					fprintf(stderr, " global parameters:\n");
					i++;
				}
				fprintf(stderr, "%18s:", parameters_iter->name);
				for (i = 0; i < parameters_iter->count; i++)
				{
					fprintf(stderr, " %f", parameters_iter->value[i]);
				}
				fprintf(stderr, "\n");
			}
			for (operations_iter = ctl_operations.begin(); operations_iter != ctl_operations.end(); operations_iter++)
			{
				ctl_operation = *operations_iter;
				fprintf(stderr, "   ctl script file: %s\n", ctl_operation.filename);
				if (verbosity > 3)
				{
					i = 0;
					for (parameters_iter = ctl_operation.local.begin(); parameters_iter != ctl_operation.local.end(); parameters_iter++)
					{
						if (i == 0)
						{
							fprintf(stderr, "  local parameters:\n");
							i++;
						}
						fprintf(stderr, "%18s:", parameters_iter->name);
						for (i = 0; i < parameters_iter->count; i++)
						{
							fprintf(stderr, " %f", parameters_iter->value[i]);
						}
						fprintf(stderr, "\n");
					}
				}
			}
		}
		fprintf(stderr, "\n");
	}

	if (!dpx_read(inputFile, input_scale, &image_buffer, image_format) &&
		!exr_read(inputFile, input_scale, &image_buffer, image_format) &&
		!tiff_read(inputFile, input_scale, &image_buffer, image_format))
	{
		fprintf(stderr, "unable to read file %s (unknown format).\n", inputFile);
		exit(1);
	}

	if (output_scale != 0.0)
	{
		output_scale = output_scale / 1.0;
	}
	if (image_format->bps == 0)
	{
		image_format->bps = image_format->src_bps;
	}

	CTLResults ctl_results;

	if (image_buffer.depth() > 0)
	{
		ctl_results.push_back(mkresult("rIn", "c00In", image_buffer, 0));
	}
	if (image_buffer.depth() > 1)
	{
		ctl_results.push_back(mkresult("gIn", "c01In", image_buffer, 1));
	}
	if (image_buffer.depth() > 2)
	{
		ctl_results.push_back(mkresult("bIn", "c02In", image_buffer, 2));
	}
	if (image_buffer.depth() > 3)
	{
		ctl_results.push_back(mkresult("aIn", "c03In", image_buffer, 3));
	}

	char name[16];

	for (j = 4; j < image_buffer.depth(); j++)
	{
		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name) - 1, "c%02dIn", j);
		ctl_results.push_back(mkresult(name, NULL, image_buffer, j));
	}

#ifdef CTL_GPU_BACKEND
	//
	// Per-file MetalInterpreter cache: each distinct CTL script gets
	// its own interpreter so their `main`s don't collide at loadFile.
	// MSL source compile (~800 ms cold on ACES v2) is still amortized
	// across input images within the same .ctl file.  Fall back to a
	// call-local cache when no shared one was supplied so direct
	// callers (outside the batch driver) keep working.
	//
	std::unique_ptr<MetalInterpreterCache> owned_cache;
	if (cache == NULL)
	{
		owned_cache.reset(new MetalInterpreterCache());
		cache = owned_cache.get();
	}
#endif

	for (operations_iter = ctl_operations.begin(); operations_iter != ctl_operations.end(); operations_iter++)
	{
		ctl_operation = *operations_iter;
		for (parameters_iter = global_parameters.begin(); parameters_iter != global_parameters.end(); parameters_iter++)
		{
			add_parameter_value_to_ctl_results(&ctl_results, *parameters_iter);
		}
		for (parameters_iter = ctl_operation.local.begin(); parameters_iter != ctl_operation.local.end(); parameters_iter++)
		{
			add_parameter_value_to_ctl_results(&ctl_results, *parameters_iter);
		}

		// Output is used to pass output parameters from script to the next.
#ifdef CTL_GPU_BACKEND
		{
			// Per-file MetalInterpreter so that two `-ctl` files both
			// defining `main` don't collide in one interpreter's scope.
			Ctl::MetalInterpreter &interp = cache->get(ctl_operation.filename);
			run_ctl_transform(interp, *operations_iter, &ctl_results, image_buffer.pixels());
		}
#else
		run_ctl_transform(*operations_iter, &ctl_results, image_buffer.pixels(), cache);
#endif
	}

	mkimage(&image_buffer, ctl_results, image_format);

	if (output_scale != 0.0)
	{
		output_scale = output_scale / 1.0;
	}
	if (image_format->squish)
	{
		image_buffer.swizzle(0, TRUE);
	}

//    std::cout << image_format->ext << std::endl;
  if (!strncmp(image_format->ext, "aces", 3))
  {
      aces_write(outputFile, output_scale,
                 image_buffer.width(), image_buffer.height(), image_buffer.depth(),
                 image_buffer.ptr(), image_format);
  }
  else if (!strncmp(image_format->ext, "exr", 3))
	{
		exr_write(outputFile, output_scale, image_buffer, image_format, compression);
	}
	else if (!strncmp(image_format->ext, "adx", 3))
	{
		dpx_write(outputFile, output_scale, image_buffer, image_format);
	}
	else if (!strncmp(image_format->ext, "dpx", 3))
	{
		dpx_write(outputFile, output_scale, image_buffer, image_format);
	}
	else if (!strncmp(image_format->ext, "tiff", 3))
	{
		tiff_write(outputFile, output_scale, image_buffer, image_format);
	}
	else
	{
		fprintf(stderr, "unable to write a %s file (unknown format).\n", image_format->ext);
		exit(1);
	}
}

//-----------------------------------------------------------------------------
//
//  transform_pixels -- apply the CTL chain to a caller-owned, pre-
//  populated 1xN framebuffer and mutate it in place.  No file I/O.
//  Used by ctlrender's -pixel CLI mode.
//
//  Intentionally mirrors transform()'s compute loop but skips
//  decode (the caller has already filled image_buffer) and encode
//  (the caller reads the buffer back out for stdout printing).
//
//-----------------------------------------------------------------------------

void
transform_pixels(const CTLOperations &ctl_operations,
                 const CTLParameters &global_parameters,
                 ctl::dpx::fb<float> *image_buffer,
#ifdef CTL_GPU_BACKEND
                 MetalInterpreterCache *cache
#else
                 InterpreterCache *cache
#endif
                 )
{
    CTLResults ctl_results;

    if (image_buffer->depth() > 0)
        ctl_results.push_back(mkresult("rIn", "c00In", *image_buffer, 0));
    if (image_buffer->depth() > 1)
        ctl_results.push_back(mkresult("gIn", "c01In", *image_buffer, 1));
    if (image_buffer->depth() > 2)
        ctl_results.push_back(mkresult("bIn", "c02In", *image_buffer, 2));
    if (image_buffer->depth() > 3)
        ctl_results.push_back(mkresult("aIn", "c03In", *image_buffer, 3));

    char name[16];
    for (uint32_t j = 4; j < image_buffer->depth(); j++)
    {
        memset(name, 0, sizeof(name));
        snprintf(name, sizeof(name) - 1, "c%02dIn", j);
        ctl_results.push_back(mkresult(name, NULL, *image_buffer, j));
    }

#ifdef CTL_GPU_BACKEND
    std::unique_ptr<MetalInterpreterCache> owned_cache;
    if (cache == NULL)
    {
        owned_cache.reset(new MetalInterpreterCache());
        cache = owned_cache.get();
    }
#else
    std::unique_ptr<InterpreterCache> owned_cache;
    if (cache == NULL)
    {
        owned_cache.reset(new InterpreterCache());
        cache = owned_cache.get();
    }
#endif

    for (CTLOperations::const_iterator op = ctl_operations.begin();
         op != ctl_operations.end(); ++op)
    {
        for (CTLParameters::const_iterator p = global_parameters.begin();
             p != global_parameters.end(); ++p)
            add_parameter_value_to_ctl_results(&ctl_results, *p);
        for (CTLParameters::const_iterator p = op->local.begin();
             p != op->local.end(); ++p)
            add_parameter_value_to_ctl_results(&ctl_results, *p);

#ifdef CTL_GPU_BACKEND
        Ctl::MetalInterpreter &interp = cache->get(op->filename);
        run_ctl_transform(interp, *op, &ctl_results, image_buffer->pixels());
#else
        run_ctl_transform(*op, &ctl_results, image_buffer->pixels(), cache);
#endif
    }

    // mkimage only reads image_format->descriptor when descriptor is 0.
    // For -pixel mode we don't care about the descriptor -- a zero-
    // initialized format_t is safe.
    format_t dummy_format;
    memset(&dummy_format, 0, sizeof(dummy_format));
    mkimage(image_buffer, ctl_results, &dummy_format);
}


#ifdef CTL_GPU_BACKEND
//-----------------------------------------------------------------------------
//
//  Metal pipeline-split entry points.  Mirror transform()'s decode /
//  compute / encode phases verbatim so the pipelined batch driver in
//  main.cc produces bit-identical output to the serial path.  Format
//  fields mutated during decode (e.g. `src_bps`) travel with the buffer
//  via the shared format_t the caller owns, so all three stages must
//  see the same `format` pointer for a given file.
//
//-----------------------------------------------------------------------------

void
transform_metal_decode(const char *inputFile,
                       float input_scale,
                       format_t *image_format,
                       ctl::dpx::fb<float> *image_buffer)
{
	if (!dpx_read(inputFile, input_scale, image_buffer, image_format) &&
		!exr_read(inputFile, input_scale, image_buffer, image_format) &&
		!tiff_read(inputFile, input_scale, image_buffer, image_format))
	{
		fprintf(stderr, "unable to read file %s (unknown format).\n", inputFile);
		exit(1);
	}

	if (image_format->bps == 0)
	{
		image_format->bps = image_format->src_bps;
	}
}

void
transform_metal_compute(MetalInterpreterCache &cache,
                        const CTLOperations &ctl_operations,
                        const CTLParameters &global_parameters,
                        format_t *image_format,
                        ctl::dpx::fb<float> *image_buffer)
{
	CTLResults ctl_results;

	if (image_buffer->depth() > 0)
		ctl_results.push_back(mkresult("rIn", "c00In", *image_buffer, 0));
	if (image_buffer->depth() > 1)
		ctl_results.push_back(mkresult("gIn", "c01In", *image_buffer, 1));
	if (image_buffer->depth() > 2)
		ctl_results.push_back(mkresult("bIn", "c02In", *image_buffer, 2));
	if (image_buffer->depth() > 3)
		ctl_results.push_back(mkresult("aIn", "c03In", *image_buffer, 3));

	char name[16];
	for (uint32_t j = 4; j < image_buffer->depth(); j++)
	{
		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name) - 1, "c%02dIn", j);
		ctl_results.push_back(mkresult(name, NULL, *image_buffer, j));
	}

	for (CTLOperations::const_iterator op = ctl_operations.begin();
	     op != ctl_operations.end(); ++op)
	{
		for (CTLParameters::const_iterator p = global_parameters.begin();
		     p != global_parameters.end(); ++p)
			add_parameter_value_to_ctl_results(&ctl_results, *p);
		for (CTLParameters::const_iterator p = op->local.begin();
		     p != op->local.end(); ++p)
			add_parameter_value_to_ctl_results(&ctl_results, *p);

		// Each distinct CTL file gets its own MetalInterpreter so
		// that `main` in file A doesn't collide with `main` in file B.
		Ctl::MetalInterpreter &interp = cache.get(op->filename);
		run_ctl_transform(interp, *op, &ctl_results, image_buffer->pixels());
	}

	mkimage(image_buffer, ctl_results, image_format);
}

void
transform_metal_encode(const char *outputFile,
                       float output_scale,
                       format_t *image_format,
                       Compression *compression,
                       ctl::dpx::fb<float> *image_buffer)
{
	if (output_scale != 0.0)
		output_scale = output_scale / 1.0;
	if (image_format->squish)
		image_buffer->swizzle(0, TRUE);

	if (!strncmp(image_format->ext, "aces", 3))
	{
		aces_write(outputFile, output_scale,
		           image_buffer->width(), image_buffer->height(), image_buffer->depth(),
		           image_buffer->ptr(), image_format);
	}
	else if (!strncmp(image_format->ext, "exr", 3))
	{
		exr_write(outputFile, output_scale, *image_buffer, image_format, compression);
	}
	else if (!strncmp(image_format->ext, "adx", 3))
	{
		dpx_write(outputFile, output_scale, *image_buffer, image_format);
	}
	else if (!strncmp(image_format->ext, "dpx", 3))
	{
		dpx_write(outputFile, output_scale, *image_buffer, image_format);
	}
	else if (!strncmp(image_format->ext, "tiff", 3))
	{
		tiff_write(outputFile, output_scale, *image_buffer, image_format);
	}
	else
	{
		fprintf(stderr, "unable to write a %s file (unknown format).\n", image_format->ext);
		exit(1);
	}
}
#endif // CTL_GPU_BACKEND
