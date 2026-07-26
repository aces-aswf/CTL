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

#include <stdio.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <limits.h>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <stdlib.h>
#include <sys/stat.h>
#include <thread>
#include <vector>
#ifndef _WIN32
	#include <sys/param.h>
#endif
#include <errno.h>
#include <ImfThreading.h>
#include "transform.hh"
#ifdef CTL_GPU_BACKEND
#include "parity.hh"
#include "benchmark.hh"
#include <CtlMetalInterpreter.h>
#endif
#include <Iex.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#ifdef __linux__
#include <linux/limits.h>
#elif _WIN32
#include <windows.h>
#include <io.h>
#define PATH_MAX MAX_PATH
#ifndef S_ISDIR
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#endif

#ifndef S_ISREG
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#endif
#define F_OK 0

#else
#endif

#if !defined(TRUE)
#define TRUE 1
#endif
#if !defined(FALSE)
#define FALSE 0
#endif

double getfloat(const char *param, const char *name, ...)
{
	double result;
	char *end;

	end=NULL;
	result=strtod(param, &end);

	if (end != NULL && *end != 0)
	{
		va_list ap;
		va_start(ap, name);
		fprintf(stderr, "Unable to parse %s as a floating point number. Found as\n", param);
		vfprintf(stderr, name, ap);
		va_end(ap);
		exit(1);
	}

	return result;
}

ctl_parameter_t get_ctl_parameter(const char ***_argv, int *_argc, int start_argc, const char *type, int count)
{
	ctl_parameter_t new_ctl_param;
	const char **argv = *_argv;
	int argc = *_argc;

	memset(&new_ctl_param, 0, sizeof(new_ctl_param));

	argv++;
	argc--;

	new_ctl_param.name = argv[0];

	argv++;
	argc--;

	new_ctl_param.count = count;
	for (int i = 0; i < new_ctl_param.count; i++)
	{
		new_ctl_param.value[i] = getfloat(argv[0], "value %d of %s parameter %s (absolute parameter %d)", i + 1, type, new_ctl_param.name, start_argc - argc);
		argc--;
		argv++;
	}

	*_argv = argv - 1;
	*_argc = argc + 1;

	return new_ctl_param;
}

struct file_format_t
{
	const char *name;
	format_t output_fmt;
};

file_format_t allowed_formats[] =
{
	{ "exr",    format_t("exr",   0) },
    { "exr16",  format_t("exr",  16) },
    { "exr32",  format_t("exr",  32) },
    { "aces",   format_t("aces", 16) },
	{ "dpx",    format_t("dpx",   0) },
	{ "dpx8",   format_t("dpx",   8) },
	{ "dpx10",  format_t("dpx",  10) },
	{ "dpx12",  format_t("dpx",  12) },
	{ "dpx16",  format_t("dpx",  16) },
	{ "tif",    format_t("tif",   0) },
	{ "tiff",   format_t("tiff",  0) },
	{ "tiff32", format_t("tiff", 32) },
	{ "tiff16", format_t("tiff", 16) },
	{ "tiff8",  format_t("tiff",  8) },
	{ "tif32",  format_t("tif",  32) },
	{ "tif16",  format_t("tif",  16) },
	{ "tif8",   format_t("tif",   8) },
	{ NULL,     format_t()           }
};

const format_t &find_format(const char *fmt, const char *message = NULL)
{
	const file_format_t *current = allowed_formats;

	while (current->name != NULL)
	{
		if (!strcmp(current->name, fmt))
		{
			return current->output_fmt;
		}
		current++;
	}
	fprintf(stderr, "Unrecognized format '%s'%s", fmt, message ? message : ".");
	exit(1);
}

//-----------------------------------------------------------------------------
// Option matching.
//
// ctlrender accepts abbreviations of its long options ("-v" for "-verbose",
// "-form" for "-format"); minlen is the shortest unambiguous abbreviation.
//
// The argument has to be a prefix of the option.  Comparing a fixed minlen
// characters instead -- strncmp(argv[0], opt, minlen) -- also swallows
// unrelated arguments that happen to start the same way, which is how
// "-nonsense" ends up applied as "-noalpha" rather than reaching the
// unrecognized-option branch.
//-----------------------------------------------------------------------------

static bool opt_matches(const char *arg, const char *opt, size_t minlen)
{
	size_t arglen = strlen(arg);

	return arglen >= minlen && arglen <= strlen(opt) &&
	       !strncmp(arg, opt, arglen);
}

int verbosity = 1;

//-----------------------------------------------------------------------------
// Parallelism model: two orthogonal axes, composed as jobs x threads.
//
// -threads controls parallelism WITHIN a single transform().  One input
// image is split into tiles of maxSamples() lanes; worker threads pull
// tiles from an atomic counter and run the SIMD interpreter on each.
// Scales well for large images up to the memory-bandwidth ceiling
// (measured: ~6x on 4K, Phase B in benchmarks/report.md).  Not useful
// when there is only enough work to fill one tile.
//
// -jobs controls parallelism ACROSS input files.  N whole transforms run
// concurrently, each with its own FunctionCall / output buffer / image
// IO.  Scales near-linearly with core count up to the point I/O or
// memory bandwidth saturates, because each worker has its own working
// set and the two layers of the stack (decode / compute / encode)
// overlap naturally across files.
//
// They compose multiplicatively: total active workers ~ jobs x threads.
// On an N-core machine the right split depends on the batch shape:
//
//   1 input file          jobs=1, threads=N      (single-file parallelism
//                                                can only come from the
//                                                tile loop)
//   M files, M >= N       jobs=N, threads=1      (file parallelism
//                                                dominates; tile
//                                                threading adds
//                                                coordination cost for
//                                                no gain)
//   few files, M < N      jobs=M, threads=N/M    (split cores evenly;
//                                                both layers contribute)
//
// Both flags default to autodetect (0) and the auto logic below picks
// from this table using hardware_concurrency() and the input count.
// Explicit values are respected; an explicit -threads survives the
// jobs-resolver's even split.  Measured on a 16-core M4 Max, 100 x 2K
// ACES v2 to tiff8: the auto defaults match hand-tuned within noise
// (57.31 s vs 56.30 s for -jobs 16 -threads 1).
//-----------------------------------------------------------------------------

// Number of worker threads for the CPU per-tile dispatch loop.
//  0  => autodetect: hardware_concurrency() / resolved-file_jobs
//  1  => single-threaded (bit-exact with pre-threading behaviour)
//  >1 => fixed count
int num_threads = 0;

// Number of input files to process concurrently (file-level parallelism).
//  0  => autodetect (default): min(num_input_files,
//        hardware_concurrency()).  See the block comment above for how
//        this composes with num_threads.
//  1  => serial (bit-exact with pre-jobs behaviour)
//  >1 => fixed count
int file_jobs = 0;

// `-no-batch-math` toggle. When non-zero, every SimdInterpreter
// constructed by the renderer skips the Accelerate/SLEEF batched
// transcendental dispatch and runs the per-element scalar libm path
// instead. Useful as a same-binary reference baseline against which
// the optimized path can be diffed.
int no_batch_math = 0;

int main(int argc, const char **argv)
{
	try
	{
		// list of ctl filenames and associated parameters
		CTLOperations ctl_operations;

		CTLParameters global_ctl_parameters;
		ctl_operation_t new_ctl_operation;

		// list of input images on which to operate
		std::list<const char *> input_image_files;
		char output_path[PATH_MAX];

        Compression compression = Compression::compressionNamed("PIZ");
		format_t desired_format;
		format_t actual_format;
		float input_scale = 0.0;
		float output_scale = 0.0;
		bool force_overwrite_output_file = FALSE;
		bool noalpha = FALSE;
		// -pixel mode: caller supplied a single r,g,b[,a] pixel on the
		// command line instead of an input image.  Run the CTL chain on
		// that one pixel and print the result to stdout; skip all file
		// I/O (no input read, no output write).
		bool pixel_mode = false;
		int pixel_depth = 0;     // 3 (RGB) or 4 (RGBA)
		float pixel_values[4] = {0, 0, 0, 0};
#ifdef CTL_GPU_BACKEND
		bool parity_check = FALSE;
		int parity_exit_code = 0;
		int benchmark_iterations = 0; // 0 = benchmark disabled
		uint32_t parity_max_ulp = 0;   // 0 = bit-exact gate (default)
		// Three-stage CPU-decode / GPU-compute / CPU-encode pipelining
		// across a batch of input files. Default on; the activation
		// block below no-ops when the batch has <2 files, file_jobs>1,
		// or parity-check is active. Opt out with `-no-pipeline`.
		bool pipeline_mode = TRUE;
#endif

		int start_argc = argc;

		argc--;
		argv++;

		new_ctl_operation.filename = NULL;
		while (argc > 0)
		{
			if (opt_matches(argv[0], "-help", 2))
			{
				if (argc > 1)
				{
					usage(argv[1]);
				}
				else
				{
					usage(NULL);
				}
				exit(1);
			}
			else if (opt_matches(argv[0], "-input_scale", 2))
			{
				if (argc == 1)
				{
					fprintf(stderr,
							"The -input_scale option requires an "
							"additional option specifying a scale\nvalue for the "
							"input file. see '-help scale' for additional "
							"details.\n");
					exit(1);
				}
				else
				{
					char *end = NULL;
					input_scale = strtof(argv[1], &end);
					if (end != NULL && *end != 0)
					{
						fprintf(stderr,
								"Unable to parse '%s' as a floating "
								"point number for the '-input_scale'\nargument\n",
								argv[1]);
						exit(1);
					}
					argv++;
					argc--;
				}
			}
			else if (opt_matches(argv[0], "-output_scale", 2))
			{
				if (argc == 1)
				{
					fprintf(stderr,
							"The -output_scale option requires an "
							"additional option specifying a scale\nvalue for the "
							"output file. see '-help scale' for additional "
							"details.\n");
					exit(1);
				}
				else
				{
					char *end = NULL;
					output_scale = strtof(argv[1], &end);
					if (end != NULL && *end != 0)
					{
						fprintf(stderr,
								"Unable to parse '%s' as a floating "
								"point number for the '-output_scale'\nargument\n",
								argv[1]);
						exit(1);
					}
					argv++;
					argc--;
				}
			}
			else if (opt_matches(argv[0], "-ctl", 3))
			{
				if (argc == 1)
				{
					fprintf(stderr,
							"the -ctl option requires an additional "
							"option specifying a file containing a\nctl script.\n"
							"see '-help ctl' for more details.\n");
					exit(1);
				}

				if (new_ctl_operation.filename != NULL)
				{
					ctl_operations.push_back(new_ctl_operation);
				}
				new_ctl_operation.local.clear();
				new_ctl_operation.filename = argv[1];

				argv++;
				argc--;
			}
			else if (opt_matches(argv[0], "-format", 5))
			{
				if (argc == 1)
				{
					fprintf(stderr,
							"the -format option requires an additional "
							"argument specifying the destination file\nformat. "
							"this may be one of the following: 'dpx10', 'dpx16', "
							"'aces', 'tiff8',\n'tiff16', or 'exr'.\nSee '-help "
							"format' for more details.\n");
					exit(1);
				}
				desired_format = find_format(argv[1]," for parameter '-format'.\nSee '-help format' for more details.");
				argv++;
				argc--;
			}
            else if (opt_matches(argv[0], "-compression", 3))
            {
                if (argc == 1)
                {
                    fprintf(stderr,
                            "the -compression option requires an additional "
                            "argument specifying a compression scheme to be "
                            "used.\n See '-help compression' for more details.\n");
                    exit(1);
                }
				const int compression_name_max_length = 9; // length of longest supported compression name plus null terminator
				char compression_name[compression_name_max_length] = { '\0' };
                for(int i = 0; i < compression_name_max_length && argv[1][i]; ++i) {
                    compression_name[i] = toupper(argv[1][i]);
                }
                compression = Compression::compressionNamed(compression_name);
                if (!strcmp(compression.name, Compression::no_compression.name)) {
                    fprintf(stderr, "Unrecognized compression scheme '%s'. Turning off compression.\n", compression_name);
                }
				desired_format.is_compression_set = true;
                argv++;
                argc--;
            }
			else if (!strcmp(argv[0], "-param1") || !strcmp(argv[0], "-p1"))
			{
				if (argc < 3)
				{
					fprintf(stderr,
							"the -param1 option requires two additional "
							"arguments specifying the\nparameter name and value."
							"\nSee '-help param' for more details.\n");
					exit(1);
				}
				if (new_ctl_operation.filename == NULL)
				{
					THROW(Iex::ArgExc, "the -param1 argument must occur *after* a -ctl option.");
				}
				new_ctl_operation.local.push_back(get_ctl_parameter(&argv, &argc, start_argc, "local", 1));
			}
			else if (!strcmp(argv[0], "-param2") || !strcmp(argv[0], "-p2"))
			{
				if (argc < 4)
				{
					fprintf(stderr,
							"the -param2 option requires three additional "
							"arguments specifying the\nparameter name and value."
							"\nSee '-help param' for more details.\n");
					exit(1);

				}
				if (new_ctl_operation.filename == NULL)
				{
					THROW(Iex::ArgExc, "the -param2 argument must occur *after* a -ctl option.");
				}
				new_ctl_operation.local.push_back(get_ctl_parameter(&argv, &argc, start_argc, "local", 3));
			}
			else if (!strcmp(argv[0], "-param3") || !strcmp(argv[0], "-p3"))
			{
				if (argc < 5)
				{
					fprintf(stderr,
							"the -param3 option requires four additional "
							"arguments specifying the\nparameter name and value."
							"\nSee '-help param' for more details.\n");
					exit(1);
				}
				if (new_ctl_operation.filename == NULL)
				{
					THROW(Iex::ArgExc, "the -param3 argument must occur *after* a -ctl option.");
				}
				new_ctl_operation.local.push_back(get_ctl_parameter(&argv, &argc, start_argc, "local", 3));
			}
			else if (!strcmp(argv[0], "-global_param1") || !strcmp(argv[0], "-gp1"))
			{
				if (argc < 3)
				{
					fprintf(stderr, "the -global_param1 option requires two "
							"additional arguments specifying the\nparameter "
							"name and value.\nSee '-help param' for more "
							"details.\n");
					exit(1);
				}
				global_ctl_parameters.push_back(get_ctl_parameter(&argv, &argc, start_argc, "global", 1));
			}
			else if (!strcmp(argv[0], "-global_param2") || !strcmp(argv[0], "-gp2"))
			{
				if (argc < 4)
				{
					fprintf(stderr, "the -global_param2 option requires three "
							"additional arguments specifying the\nparameter "
							"name and value.\nSee '-help param' for more "
							"details.\n");
					exit(1);
				}
				global_ctl_parameters.push_back(get_ctl_parameter(&argv, &argc, start_argc, "global", 2));
			}
			else if (!strcmp(argv[0], "-global_param3") || !strcmp(argv[0], "-gp3"))
			{
				if (argc < 5)
				{
					fprintf(stderr, "the -global_param3 option requires four "
							"additional arguments specifying the\nparameter "
							"name and value.\nSee '-help param' for more "
							"details.\n");
					exit(1);

				}
				global_ctl_parameters.push_back(get_ctl_parameter(&argv, &argc, start_argc, "global", 2));
			}
			else if (opt_matches(argv[0], "-verbose", 2))
			{
				verbosity++;
			}
			else if (opt_matches(argv[0], "-quiet", 2))
			{
				verbosity--;
			}
			else if (opt_matches(argv[0], "-force", 5))
			{
				force_overwrite_output_file = TRUE;
			}
			else if (!strcmp(argv[0], "-no-batch-math"))
			{
				no_batch_math = 1;
			}
			else if (opt_matches(argv[0], "-noalpha", 2))
			{
				noalpha = TRUE;
			}
			else if (!strcmp(argv[0], "-threads"))
			{
				if (argc == 1)
				{
					fprintf(stderr,
							"The -threads option requires an additional "
							"integer argument: the number of CPU worker "
							"threads (0 = autodetect, 1 = single-threaded).\n");
					exit(1);
				}
				char *end = NULL;
				long n = strtol(argv[1], &end, 10);
				if (end != NULL && *end != 0)
				{
					fprintf(stderr,
							"Unable to parse '%s' as an integer for the "
							"'-threads' argument.\n", argv[1]);
					exit(1);
				}
				if (n < 0)
				{
					fprintf(stderr, "-threads must be >= 0 (got %ld).\n", n);
					exit(1);
				}
				num_threads = static_cast<int>(n);
				argv++;
				argc--;
			}
			else if (!strcmp(argv[0], "-jobs"))
			{
				if (argc == 1)
				{
					fprintf(stderr,
							"The -jobs option requires an additional "
							"integer argument: the number of input files "
							"to process in parallel (0 = autodetect, "
							"1 = serial).\n");
					exit(1);
				}
				char *end = NULL;
				long n = strtol(argv[1], &end, 10);
				if (end != NULL && *end != 0)
				{
					fprintf(stderr,
							"Unable to parse '%s' as an integer for the "
							"'-jobs' argument.\n", argv[1]);
					exit(1);
				}
				if (n < 0)
				{
					fprintf(stderr, "-jobs must be >= 0 (got %ld).\n", n);
					exit(1);
				}
				file_jobs = static_cast<int>(n);
				argv++;
				argc--;
			}
#ifdef CTL_GPU_BACKEND
			else if (!strcmp(argv[0], "-parity-check") || !strcmp(argv[0], "--parity-check"))
			{
				parity_check = TRUE;
			}
			else if (!strcmp(argv[0], "-parity-check-ulp") || !strcmp(argv[0], "--parity-check-ulp"))
			{
				// Max per-channel ULP deviation that still counts as PASS.
				// Requires a non-negative integer argument. Implicitly
				// enables --parity-check.
				if (argc < 2)
				{
					fprintf(stderr,
							"--parity-check-ulp requires an integer argument.\n");
					exit(1);
				}
				char *end = NULL;
				long v = strtol(argv[1], &end, 10);
				if (end == NULL || *end != 0 || v < 0)
				{
					fprintf(stderr,
							"--parity-check-ulp: '%s' is not a non-negative integer.\n",
							argv[1]);
					exit(1);
				}
				parity_max_ulp = (uint32_t)v;
				parity_check = TRUE;
				argv++;
				argc--;
			}
			else if (!strcmp(argv[0], "-pipeline") || !strcmp(argv[0], "--pipeline"))
			{
				// Accepted for backward compatibility; pipelining is on
				// by default now.
				pipeline_mode = TRUE;
			}
			else if (!strcmp(argv[0], "-no-pipeline") || !strcmp(argv[0], "--no-pipeline"))
			{
				pipeline_mode = FALSE;
			}
			else if (!strcmp(argv[0], "-benchmark") || !strcmp(argv[0], "--benchmark"))
			{
				// Optional iteration count: treated as one if absent or
				// if the next arg doesn't parse as a positive integer.
				int iters = 100;
				if (argc > 1)
				{
					char *end = NULL;
					long v = strtol(argv[1], &end, 10);
					if (end != NULL && *end == 0 && v > 0)
					{
						iters = (int)v;
						argv++;
						argc--;
					}
				}
				benchmark_iterations = iters;
			}
#endif
			else if (!strcmp(argv[0], "-pixel"))
			{
				// -pixel r,g,b  or  -pixel r,g,b,a
				// Parses 3 or 4 comma-separated floats into pixel_values.
				// Depth is inferred from the count.  Running with -pixel
				// skips all image I/O: output goes to stdout.  See
				// '-help pixel' for examples.
				if (argc < 2)
				{
					fprintf(stderr,
					        "The -pixel option requires an argument of the "
					        "form 'r,g,b' or 'r,g,b,a'. See '-help pixel'.\n");
					exit(1);
				}
				const char *src = argv[1];
				int n = 0;
				char *end = NULL;
				while (n < 4)
				{
					pixel_values[n] = strtof(src, &end);
					if (end == src)
					{
						fprintf(stderr,
						        "-pixel: cannot parse '%s' as a floating "
						        "point number near offset %zu of '%s'.\n",
						        src, (size_t)(src - argv[1]), argv[1]);
						exit(1);
					}
					n++;
					if (*end == '\0') break;
					if (*end != ',')
					{
						fprintf(stderr,
						        "-pixel: unexpected character '%c' in '%s'; "
						        "expected ',' or end of argument.\n",
						        *end, argv[1]);
						exit(1);
					}
					src = end + 1;
				}
				if (n < 3)
				{
					fprintf(stderr,
					        "-pixel requires at least 3 values (r,g,b); "
					        "got %d from '%s'.\n", n, argv[1]);
					exit(1);
				}
				if (n == 4 && *end != '\0')
				{
					fprintf(stderr,
					        "-pixel accepts at most 4 values (r,g,b,a); "
					        "extra characters after '%s'.\n", argv[1]);
					exit(1);
				}
				pixel_depth = n;
				pixel_mode = true;
				argv++;
				argc--;
			}
			else if (!strncmp(argv[0], "-", 1))
			{
				fprintf(stderr,
						"unrecognized option %s. see -help for a list "
						"of available options.\n", argv[0]);
				exit(1);
			}
			else
			{
				input_image_files.push_back(argv[0]);
			}
			argv++;
			argc--;

		}  // end while

		if (new_ctl_operation.filename != NULL)
		{
			ctl_operations.push_back(new_ctl_operation);
		}

#ifdef CTL_GPU_BACKEND
		//
		// --benchmark mode skips file-output entirely: it just times the
		// Metal transform on each input. We require >=1 input and ignore
		// any trailing path. No format validation, no output overwriting
		// checks -- those are irrelevant to benchmarking.
		//
		if (benchmark_iterations > 0)
		{
			if (input_image_files.size() < 1)
			{
				fprintf(stderr,
				        "--benchmark requires at least one input image.\n");
				exit(1);
			}
			if (ctl_operations.empty())
			{
				fprintf(stderr,
				        "--benchmark requires a -ctl script.\n");
				exit(1);
			}
			// If a trailing arg looks like an output path (would have been
			// popped as outputFile below), drop it -- benchmark doesn't
			// need one.
			if (input_image_files.size() > 1)
				input_image_files.pop_back();

			int bench_exit = 0;
			while (!input_image_files.empty())
			{
				const char *inputFile = input_image_files.front();
				format_t bench_format = desired_format;
				int rc = run_benchmark(benchmark_iterations, inputFile,
				                       input_scale, &bench_format,
				                       ctl_operations, global_ctl_parameters);
				if (rc != 0)
					bench_exit = rc;
				input_image_files.pop_front();
			}
			return bench_exit;
		}
#endif

		//
		// -pixel mode: one r,g,b[,a] pixel from the CLI, no files.
		// Apply the CTL chain and print the result to stdout.
		//
		if (pixel_mode)
		{
			if (!input_image_files.empty())
			{
				fprintf(stderr,
				        "-pixel cannot be combined with a positional input "
				        "file (got '%s'). See '-help pixel'.\n",
				        input_image_files.front());
				exit(1);
			}
			if (ctl_operations.empty())
			{
				fprintf(stderr,
				        "-pixel requires at least one -ctl script.\n");
				exit(1);
			}

			// Metal's compute pipeline is tuned for large sample counts
			// (threadgroup width = 32, kernel launches require alignment
			// padding).  A single-pixel dispatch sometimes returns zeros
			// on Apple Silicon.  Replicate the input pixel across a small
			// batch so the dispatch is always at least one full
			// threadgroup wide, then read lane 0 as the result.  The
			// CTL chain runs identically on every lane, so lane 0's
			// output is the answer for our single logical pixel.
			const uint32_t kBatch = 64;
			ctl::dpx::fb<float> pix;
			pix.init(kBatch, 1, pixel_depth);
			for (uint32_t p = 0; p < kBatch; p++)
				for (int i = 0; i < pixel_depth; i++)
					pix.ptr()[p * pixel_depth + i] = pixel_values[i];

#ifdef CTL_GPU_BACKEND
			MetalInterpreterCache pix_cache;
#else
			InterpreterCache pix_cache;
#endif
			transform_pixels(ctl_operations, global_ctl_parameters,
			                 &pix, &pix_cache);

			const float *out = pix.ptr();
			fprintf(stdout, "R=%.7f  G=%.7f  B=%.7f",
			        out[0], out[1], out[2]);
			if (pixel_depth == 4)
				fprintf(stdout, "  A=%.7f", out[3]);
			fprintf(stdout, "\n");
			return 0;
		}

		if (input_image_files.size() < 2)
		{
			usage(NULL);

			exit(1);
		}

		char *output_slash = NULL;
		const char *outputFile = input_image_files.back();
		input_image_files.pop_back();

		struct stat file_status;
		if (stat(outputFile, &file_status) >= 0)
		{
			if (S_ISDIR(file_status.st_mode))
			{
				memset(output_path, 0, sizeof(output_path));
				strncpy(output_path, outputFile, PATH_MAX -1);
				outputFile = output_path;
				output_slash = output_path + strlen(output_path);
				if (*output_slash != '/')
				{
					*(output_slash++) = '/';
					*output_slash = 0;
				}
			}
			else if (S_ISREG(file_status.st_mode))
			{
				if (input_image_files.size() > 1)
				{
					fprintf(stderr,
							"When providing more than one source "
							"image the destination must be a\ndirectory.\n");
					exit(1);
				}
				else
				{
					if (!force_overwrite_output_file)
					{
						fprintf(stderr,
								"The destination file %s already exists. Refusing to overwrite the existing file.\n"
								"To overwrite the existing file use the -force option.\n"
								, outputFile);
						exit(1);
					}
					else
					{
						// File exists, but we treat it as if it doesn't (see
						// if(output_slash==NULL) {...} down below...
						output_slash = NULL;
					}
				}
			}
			else
			{
				fprintf(stderr,
						"Specified destination is something other than "
						"a file or directory. That's\nprobably a bad idea.\n");
				exit(1);
			}
		}
		else
		{
			if (errno != ENOENT)
			{
				fprintf(stderr, "Unable to get information about %s (%s).\n", outputFile, strerror(errno));
				exit(1);
			}
			if (input_image_files.size() != 1)
			{
				fprintf(stderr,
						"When specifying more than one source file "
						"you must specify the destination as\na directory "
						"that already exists.\nUnable to stat '%s' (%s)\n",
						outputFile, strerror(errno));
				exit(1);
			}
		}

		if (output_slash == NULL)
		{
			// This is the case when our outputFile is a single file. We do a bunch
			// of sanity checking between the extension of the specified file
			// (if any) and the -format option (if any).
			char *dot = (char *) strrchr(outputFile, '.');
			if (dot == NULL && desired_format.ext == NULL)
			{
				fprintf(stderr,
						"You have not explicitly provided an output "
						"format, and the output file name\ndoes not not contain "
						"an extension. Please add an extension to the output "
						"file\nor use the -format option to specify the desired "
						"output format.\n");
				exit(1);
			}
			else if (dot != NULL)
			{
				if (desired_format.ext == NULL)
				{
					actual_format = find_format(dot + 1,
							                    " specified implicitly (by "
									            "the extension) for\noutput file "
									            "format. Please fix this or use\n"
									            "the -format option to specify "
									            "the desired output format.\n");
				}
				else
				{
					// HACK aces format file type check
                    const char *ext = desired_format.ext;
                    static const char exrext[] = "exr";
                    if (!strcmp(ext, "aces"))
                        ext = exrext;
                    if (strcmp(ext, dot + 1) && !force_overwrite_output_file)
					{
						fprintf(stderr,
								"You have specified a destination file "
								"type with the -format option, but the\noutput "
								"file extension does not match the format "
								"specified by the -format option.\nThis behavior "
								"can be overridden by specifing the "
								"-force option (which\nwill make the -format "
								"option take priority).\n");
						exit(1);
					}
					actual_format = desired_format;
				}
			}
		}

		if (verbosity > 1)
		{
			fprintf(stderr, "global ctl parameters:\n");

			CTLParameters temp_ctl_parameters;
			temp_ctl_parameters = global_ctl_parameters;

			while (temp_ctl_parameters.size() > 0)
			{
				ctl_parameter_t new_ctl_parameter = temp_ctl_parameters.front();
				temp_ctl_parameters.pop_front();
				fprintf(stderr, "%17s:", new_ctl_parameter.name);
				for (int i = 0; i < new_ctl_parameter.count; i++)
				{
					fprintf(stderr, " %f", new_ctl_parameter.value[i]);
				}
				fprintf(stderr, "\n");
			}
			fprintf(stderr, "\n");
		}

#ifdef CTL_GPU_BACKEND
		//
		// Per-file MetalInterpreter cache, hoisted out of the per-file
		// loop so MSL source compile (~800 ms cold on ACES v2) is paid
		// once per distinct CTL script rather than once per input image.
		// Each distinct .ctl file gets its own MetalInterpreter so that
		// two -ctl files both defining `main` can coexist (CTL's parser
		// rejects duplicate top-level symbols within one interpreter
		// scope; mirrors the CPU InterpreterCache design).
		//
		MetalInterpreterCache interpreter_cache;
#else
		// Shared across the multi-file loop so each distinct CTL script is
		// parsed + codegen'd once for the whole batch rather than once per
		// input file.
		InterpreterCache interpreter_cache;
#endif

#ifndef CTL_GPU_BACKEND
		// Pre-warm so worker threads can read byFilename without locking.
		// Also amortizes module load time before the first transform().
		// (GPU backend hoists its own MetalInterpreter above; MSL compile
		// is effectively the same amortization.)
		for (const ctl_operation_t &op : ctl_operations)
		{
			interpreter_cache.preWarm(op.filename);
		}
#endif

#ifdef CTL_GPU_BACKEND
		// The Metal backend shares a per-file MetalInterpreter cache and
		// one command queue per interpreter across the batch. Concurrent
		// transform() calls from multiple file workers race on that
		// state and SIGSEGV with no output. File-level parallelism
		// wouldn't help anyway -- one GPU is one GPU -- so clamp to 1 and
		// rely on the decode/compute/encode pipeline below for batch
		// throughput. Covers the parity-check-serial case too (races
		// on parity_exit_code).
		if (file_jobs != 1)
		{
			if (file_jobs > 1)
			{
				fprintf(stderr,
				    "ctlrender-metal: -jobs=%d requested but the Metal "
				    "backend runs a single shared interpreter and "
				    "command queue; clamping to -jobs=1 and relying on "
				    "the decode/compute/encode pipeline for batch "
				    "throughput.\n",
				    file_jobs);
			}
			file_jobs = 1;
		}
#endif

		// Resolve -jobs 0 (autodetect). Cap at the input count so a
		// single-file batch stays serial (letting per-tile threads take
		// all cores); cap at hardware_concurrency() so we never spawn
		// more file workers than there are CPUs.
		if (file_jobs == 0)
		{
			unsigned hw = std::thread::hardware_concurrency();
			if (hw == 0) hw = 1;
			unsigned nfiles = static_cast<unsigned>(input_image_files.size());
			unsigned j = (nfiles < hw) ? nfiles : hw;
			if (j == 0) j = 1;
			file_jobs = static_cast<int>(j);
		}

		// When running multiple files in parallel, split hardware_concurrency
		// between file-level and tile-level workers so the two layers don't
		// oversubscribe.  Only adjust when num_threads is in its default-auto
		// state; an explicit -threads value is respected.
		if (file_jobs > 1 && num_threads == 0)
		{
			unsigned hw = std::thread::hardware_concurrency();
			if (hw == 0) hw = 1;
			unsigned per = hw / static_cast<unsigned>(file_jobs);
			if (per == 0) per = 1;
			num_threads = static_cast<int>(per);
		}

		// OpenEXR's PIZ/ZIP/ZIPS/DWA codecs split scanline blocks into
		// independent compression tasks submitted to Imf's global thread
		// pool. The default pool size is 0 (single-threaded), so a 4K PIZ
		// write serializes 270 blocks onto one core -- on the cpu-perf
		// path that's the dominant stage on compressed outputs (measured
		// 4K 30 PIZ = 62 s vs 40 s for NONE). Size the pool to
		// hardware_concurrency() so the encode/decode work can fan out
		// across all available cores. This composes with -threads/-jobs
		// rather than oversubscribing: while a file worker is inside
		// writePixels/readPixels it's blocked on the Imf pool, so its
		// CPU slot is free for pool threads to use. The net is at most
		// hardware_concurrency() active threads at any instant, whether
		// the bottleneck is compute (jobs x threads) or I/O (pool).
		// Previously capped at 16 to match typical laptop topologies; the
		// cap was removed because it throttled encode on 28-/64-core
		// workstations where file workers still benefit from a full-
		// width pool during aligned encode-heavy phases.
		{
			unsigned hw = std::thread::hardware_concurrency();
			if (hw == 0) hw = 1;
			Imf::setGlobalThreadCount(static_cast<int>(hw));
		}

		const bool is_directory_output = (output_slash != NULL);
		const size_t output_prefix_len =
		    is_directory_output
		        ? static_cast<size_t>(output_slash - output_path)
		        : 0;

		// Snapshot the arg-parsing side-effects each file needs.  The main
		// `output_path`/`outputFile`/`actual_format` are mutated per-file in
		// the body below; workers must use local copies so they don't race.
		const format_t template_format = actual_format;

		auto process_one_file = [&](const char *inputFile)
		{
			char output_path_local[PATH_MAX];
			const char *outputFileLocal;
			format_t format_local = template_format;

			if (is_directory_output)
			{
				memcpy(output_path_local, output_path, output_prefix_len);
				output_path_local[output_prefix_len] = 0;
				char *slash_local = output_path_local + output_prefix_len;

				const char *input_slash = strrchr(inputFile, '/');
				input_slash = input_slash ? input_slash + 1 : inputFile;
				strcpy(slash_local, input_slash);

				char *dot = strrchr(output_path_local, '.');
				if (dot != NULL)
				{
					dot++;
					if (desired_format.ext != NULL)
					{
						// HACK aces format file type check
						const char *ext = desired_format.ext;
						static const char exrext[] = "exr";
						if (!strcmp(ext, "aces"))
							ext = exrext;
						strcpy(dot, ext);
						format_local = desired_format;
					}
					else
					{
						format_local = find_format(dot, " (determined from destination file extension).");
					}
				}
				outputFileLocal = output_path_local;
			}
			else
			{
				outputFileLocal = outputFile;
			}

			if (force_overwrite_output_file)
			{
				if (unlink(outputFileLocal) < 0)
				{
					if (errno != ENOENT)
					{
						fprintf(stderr, "Unable to remove existing file named "
								"'%s' (%s).\n", outputFileLocal, strerror(errno));
						exit(1);
					}
				}
			}
			if (access(outputFileLocal, F_OK) >= 0)
			{
				fprintf(stderr, "Can not overwrite the file '%s'.\n", outputFileLocal);
				exit(1);
			}
			format_local.squish = noalpha;
			if (desired_format.is_compression_set)
			{
				format_local.is_compression_set = true;
			}
#ifdef CTL_GPU_BACKEND
			if (parity_check)
			{
				int rc = run_parity_check(inputFile, outputFileLocal,
				                          input_scale, output_scale,
				                          &format_local, &compression,
				                          ctl_operations, global_ctl_parameters,
				                          parity_max_ulp);
				if (rc != 0)
					parity_exit_code = rc;
			}
			else
			{
				transform(inputFile, outputFileLocal, input_scale, output_scale, &format_local, &compression, ctl_operations, global_ctl_parameters, &interpreter_cache);
			}
#else
			transform(inputFile, outputFileLocal, input_scale, output_scale, &format_local, &compression, ctl_operations, global_ctl_parameters, &interpreter_cache);
#endif
		};

#ifdef CTL_GPU_BACKEND
		if (pipeline_mode && file_jobs == 1 && !parity_check
		    && input_image_files.size() >= 1)
		{
			//
			// Three-stage batch pipeline: overlap CPU decode(N+1) ||
			// GPU compute(N) || CPU encode(N-1). Serial per-file is
			// bound by decode+GPU+encode; with a shared interpreter,
			// GPU compute ~ 79% and decode+encode ~ 21% of wall-clock
			// on 4K aces_combined. Pipelining hides the CPU work
			// behind the GPU's kernel time (and vice-versa).
			//
			// Applies to N=1 too: a prewarm thread loads the CTL
			// modules into the shared MetalInterpreter (the SIMD-sidecar
			// parse+codegen is ~400 ms cold on ACES v2 -- the dominant
			// slice of single-file latency) concurrent with the
			// decoder thread reading the input EXR. parity_check
			// serializes naturally via main's file_jobs=1 override
			// above.
			//
			struct PipeItem
			{
				size_t              index;
				const char         *inputFile;
				std::string         outputFile;
				format_t            format;
				ctl::dpx::fb<float> buffer;
			};

			std::vector<const char *> file_vec(
			    input_image_files.begin(), input_image_files.end());
			input_image_files.clear();

			auto prep_item = [&](size_t idx, const char *inputFile,
			                     std::string &outOutputFile, format_t &outFormat)
			{
				char output_path_local[PATH_MAX];
				const char *outputFileLocal;
				format_t format_local = template_format;

				if (is_directory_output)
				{
					memcpy(output_path_local, output_path, output_prefix_len);
					output_path_local[output_prefix_len] = 0;
					char *slash_local = output_path_local + output_prefix_len;

					const char *input_slash = strrchr(inputFile, '/');
					input_slash = input_slash ? input_slash + 1 : inputFile;
					strcpy(slash_local, input_slash);

					char *dot = strrchr(output_path_local, '.');
					if (dot != NULL)
					{
						dot++;
						if (desired_format.ext != NULL)
						{
							const char *ext = desired_format.ext;
							static const char exrext[] = "exr";
							if (!strcmp(ext, "aces")) ext = exrext;
							strcpy(dot, ext);
							format_local = desired_format;
						}
						else
						{
							format_local = find_format(dot,
							    " (determined from destination file extension).");
						}
					}
					outputFileLocal = output_path_local;
				}
				else
				{
					outputFileLocal = outputFile;
				}

				if (force_overwrite_output_file)
				{
					if (unlink(outputFileLocal) < 0 && errno != ENOENT)
					{
						fprintf(stderr, "Unable to remove existing file "
						        "named '%s' (%s).\n",
						        outputFileLocal, strerror(errno));
						exit(1);
					}
				}
				if (access(outputFileLocal, F_OK) >= 0)
				{
					fprintf(stderr, "Can not overwrite the file '%s'.\n",
					        outputFileLocal);
					exit(1);
				}
				format_local.squish = noalpha;
				if (desired_format.is_compression_set)
					format_local.is_compression_set = true;

				outOutputFile = outputFileLocal;
				outFormat = format_local;
				(void)idx;
			};

			// Bounded single-slot queues -- holding one item in flight
			// per stage plus the one currently being worked on gives a
			// total window of 3, enough to saturate the longest stage
			// while bounding peak memory to 3 decoded 4K buffers
			// (~95 MB each float32 RGB).
			std::mutex  decQMu, encQMu;
			std::condition_variable decQCv, encQCv;
			std::queue<std::unique_ptr<PipeItem>> decQ, encQ;
			bool decDone = false, encDone = false;
			constexpr size_t kQCap = 1;

			std::exception_ptr first_err;
			std::mutex errMu;
			std::atomic<bool> aborted(false);
			auto record_err = [&](std::exception_ptr e)
			{
				std::lock_guard<std::mutex> g(errMu);
				if (!first_err) first_err = e;
				aborted.store(true, std::memory_order_relaxed);
				// Wake whoever is waiting so they can exit promptly.
				decQCv.notify_all();
				encQCv.notify_all();
			};

			auto decoder = [&]()
			{
				try
				{
					for (size_t i = 0; i < file_vec.size(); ++i)
					{
						if (aborted.load(std::memory_order_relaxed)) break;
						auto item = std::unique_ptr<PipeItem>(new PipeItem);
						item->index = i;
						item->inputFile = file_vec[i];
						prep_item(i, file_vec[i], item->outputFile,
						          item->format);

						transform_metal_decode(item->inputFile, input_scale,
						                       &item->format, &item->buffer);

						std::unique_lock<std::mutex> lk(decQMu);
						decQCv.wait(lk, [&]{
							return decQ.size() < kQCap
							    || aborted.load(std::memory_order_relaxed);
						});
						if (aborted.load(std::memory_order_relaxed)) break;
						decQ.push(std::move(item));
						lk.unlock();
						decQCv.notify_all();
					}
				}
				catch (...)
				{
					record_err(std::current_exception());
				}
				{
					std::lock_guard<std::mutex> lk(decQMu);
					decDone = true;
				}
				decQCv.notify_all();
			};

			auto encoder = [&]()
			{
				try
				{
					while (true)
					{
						std::unique_lock<std::mutex> lk(encQMu);
						encQCv.wait(lk, [&]{
							return !encQ.empty() || encDone
							    || aborted.load(std::memory_order_relaxed);
						});
						if (aborted.load(std::memory_order_relaxed)) break;
						if (encQ.empty()) {
							if (encDone) break;
							continue;
						}
						auto item = std::move(encQ.front());
						encQ.pop();
						lk.unlock();
						encQCv.notify_all();

						transform_metal_encode(item->outputFile.c_str(),
						                       output_scale, &item->format,
						                       &compression, &item->buffer);
					}
				}
				catch (...)
				{
					record_err(std::current_exception());
				}
			};

			// Prewarm: load every CTL module into the shared
			// interpreter concurrent with the decoder thread reading
			// the first input EXR. Joined before main enters the
			// compute loop so there's no race on the interpreter's
			// module table when the first newFunctionCall runs.
			auto prewarm = [&]()
			{
				try
				{
					for (const ctl_operation_t &op : ctl_operations)
					{
						if (aborted.load(std::memory_order_relaxed)) return;

						// Module name = basename of the CTL filename
						// without its extension; matches the convention
						// in run_ctl_transform so moduleIsLoaded hits.
						const char *fn = op.filename;
						const char *slashP = strrchr(fn, '/');
#ifdef WIN32
						const char *backP = strrchr(fn, '\\');
						if (backP && backP > slashP) slashP = backP;
#endif
						const char *base = slashP ? slashP + 1 : fn;
						std::string mod(base);
						size_t dot = mod.rfind('.');
						if (dot != std::string::npos) mod.resize(dot);

						{
							//
							// Resolve to an absolute path so the
							// persistent sidecar cache keys stably
							// across different working directories
							// (tests invoke ctlrender-metal from a
							// temp dir; users invoke it from wherever
							// they happen to be).
							//
							// Each distinct .ctl file owns its own
							// MetalInterpreter (see MetalInterpreterCache);
							// we pre-warm it here so the decode stage
							// can proceed without waiting on MSL compile.
							//
							// Lazily create the interpreter for this
							// file but do NOT call loadFile yet -- we
							// need to wrap loadFile with the sidecar
							// cache preload/flush pair.  cache.get()
							// would load eagerly; open-code the
							// create-and-load so we can interleave
							// the sidecar calls at the right point.
							//
							auto &slot = interpreter_cache.byFilename[
								std::string(op.filename)];
							if (!slot)
							{
								slot.reset(new Ctl::MetalInterpreter);
								char abs[PATH_MAX];
								const char *topAbs = realpath(fn, abs) ? abs : fn;
								const bool hit =
									slot->preloadSidecarCache(topAbs);
								slot->loadFile(fn, mod);
								if (!hit)
									slot->flushSidecarCache(topAbs);
							}
						}
					}
				}
				catch (...)
				{
					record_err(std::current_exception());
				}
			};

			std::thread prewarmThr(prewarm);
			std::thread decThr(decoder);
			std::thread encThr(encoder);

			// Block until prewarm is done so the compute loop's
			// newFunctionCall sees a fully-loaded interpreter. The
			// decoder has been running in parallel the whole time,
			// so the first decQ pop usually returns immediately.
			prewarmThr.join();

			// Main thread: compute stage -- pop from decQ, run GPU,
			// push to encQ. Must run on main thread so it holds the
			// shared MetalInterpreter (newFunctionCall is not
			// concurrency-safe against itself on the same interp).
			try
			{
				while (true)
				{
					std::unique_ptr<PipeItem> item;
					{
						std::unique_lock<std::mutex> lk(decQMu);
						decQCv.wait(lk, [&]{
							return !decQ.empty() || decDone
							    || aborted.load(std::memory_order_relaxed);
						});
						if (aborted.load(std::memory_order_relaxed)) break;
						if (decQ.empty()) {
							if (decDone) break;
							continue;
						}
						item = std::move(decQ.front());
						decQ.pop();
					}
					decQCv.notify_all();

					transform_metal_compute(interpreter_cache,
					                        ctl_operations,
					                        global_ctl_parameters,
					                        &item->format, &item->buffer);

					{
						std::unique_lock<std::mutex> lk(encQMu);
						encQCv.wait(lk, [&]{
							return encQ.size() < kQCap
							    || aborted.load(std::memory_order_relaxed);
						});
						if (aborted.load(std::memory_order_relaxed)) break;
						encQ.push(std::move(item));
					}
					encQCv.notify_all();
				}
			}
			catch (...)
			{
				record_err(std::current_exception());
			}
			{
				std::lock_guard<std::mutex> lk(encQMu);
				encDone = true;
			}
			encQCv.notify_all();

			decThr.join();
			encThr.join();

			if (first_err) std::rethrow_exception(first_err);
		}
		else
#endif
		if (file_jobs == 1)
		{
			while (input_image_files.size() > 0)
			{
				process_one_file(input_image_files.front());
				input_image_files.pop_front();
			}
		}
		else
		{
			std::vector<const char *> file_vec(
			    input_image_files.begin(), input_image_files.end());
			input_image_files.clear();

			std::atomic<size_t> next_index(0);
			std::mutex err_mutex;
			std::exception_ptr first_err;
			std::atomic<bool> aborted(false);

			auto worker = [&]()
			{
				try
				{
					while (!aborted.load(std::memory_order_relaxed))
					{
						size_t idx = next_index.fetch_add(
						    1, std::memory_order_relaxed);
						if (idx >= file_vec.size()) break;
						process_one_file(file_vec[idx]);
					}
				}
				catch (...)
				{
					std::lock_guard<std::mutex> g(err_mutex);
					if (!first_err) first_err = std::current_exception();
					aborted.store(true, std::memory_order_relaxed);
				}
			};

			int extras = file_jobs - 1;
			std::vector<std::thread> pool;
			pool.reserve(static_cast<size_t>(extras));
			for (int i = 0; i < extras; i++) pool.emplace_back(worker);
			worker();  // main thread is worker 0
			for (auto &t : pool) t.join();

			if (first_err) std::rethrow_exception(first_err);
		}

#ifdef CTL_GPU_BACKEND
		if (parity_exit_code != 0)
			return parity_exit_code;
#endif
		return 0;

	} catch (std::exception &e)
	{
		fprintf(stderr, "\nexception thrown (oops...): %s\n", e.what());
		return 1;
	}
}
