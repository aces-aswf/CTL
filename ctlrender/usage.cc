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
#include <string.h>

void usage(const char *section) {
	if(section==NULL) {
		fprintf(stdout, "\n"
"ctlrender - transforms an image using one or more CTL scripts, potentially\n"
"            converting the file format in the process\n"
"\nusage:\n"
"    ctlrender [<options> ...] <source file...> <destination>\n"
"\n"
"\n"
"options:\n"
"\n"
"    <source file...>      One or more source files may be specified in a\n"
"                          space separated list. Note to non-cygwin using\n"
"                          Windows users: wild card ('*') expansions are not\n"
"                          supported.\n"
"\n"
"    <destination>         In the case that only one source file is specified\n"
"                          this may be either a filename or a directory. If\n"
"                          a file is specified, then the output format of the\n"
"                          file is determined from the input file type and\n"
"                          the extension of the output file type.\n"
"                          If more than one source file is specified then\n"
"                          this must specify an existing directory. To\n"
"                          perform a file type conversion, the '-format'\n"
"                          option must be used.\n"
"                          See below for details on the '-format' option.\n"
"\n"
"    -input_scale <value>  Specifies a scaling value for the input.\n"
"                          Details on this are provided with '-help scale'.\n"
"\n"
"    -output_scale <value> Specifies a scaling value for the output file.\n"
"                          Details on this are provided with '-help scale'.\n"
"\n"
"    -format <output_fmt>  Specifies the output file format. If ony one\n"
"                          source file is specified then the extension of\n"
"                          destination file is used to determine the file\n"
"                          format. Details on this are provided with\n"
"                          '-help format'\n"
"\n"
"    -compression <type>   Specifies OpenEXR compression type. Value will\n"
"                          be ignored when not saving an exr file\n"
"                          '-help compression'\n"
"\n"
"    -ctl <filename>       Specifies the name of a CTL file to be applied\n"
"                          to the input images. More than one CTL file may\n"
"                          be provided (each must be delineated by a '-ctl'\n"
"                          option), and they are applied in-order.\n"
"\n"
"    -pixel r,g,b[,a]      Run the CTL chain on a single pixel supplied on\n"
"                          the command line instead of reading an image\n"
"                          file, and print the result to stdout.  3 values\n"
"                          = RGB (depth 3), 4 values = RGBA (depth 4).\n"
"                          Details on this mode are provided with\n"
"                          '-help pixel'.\n"
"\n"
"    -param1 ...           Specifies the value of a CTL script parameter.\n"
"    -param2 ...           Details on this and similar options are provided\n"
"    -param3 ...           with '-help param'\n"
"\n"
"  parallelism (-threads and -jobs compose as two orthogonal axes; both\n"
"  default to autodetect and together saturate hardware_concurrency()):\n"
"\n"
"    -threads <n>          Per-tile parallelism WITHIN a single transform.\n"
"                          Splits one input image into tiles and runs the\n"
"                          SIMD interpreter on them concurrently. Best\n"
"                          lever when you have one (or a few) large\n"
"                          images to process. Scales up to a memory-\n"
"                          bandwidth ceiling (~6x on 4K images).\n"
"                          0 = autodetect (default), 1 = single-threaded\n"
"                          (bit-exact with pre-threading behaviour).\n"
"\n"
"    -jobs <n>             File-level parallelism ACROSS inputs. Runs\n"
"                          N whole transforms concurrently on different\n"
"                          input files. Best lever when you have many\n"
"                          input files in a batch; each worker has its\n"
"                          own working set so cache behaviour and\n"
"                          I/O-compute overlap are better than per-tile\n"
"                          threading. 0 = autodetect (default):\n"
"                          min(num-inputs, hardware_concurrency()).\n"
"                          1 = serial (bit-exact with pre-jobs\n"
"                          behaviour).\n"
"\n"
"                          How they compose: total active CPU workers\n"
"                          is roughly jobs * threads. When both are auto\n"
"                          the defaults pick:\n"
"                            - 1 input file:    jobs=1, threads=N\n"
"                            - M files, M>=N:   jobs=N, threads=1\n"
"                            - few files, M<N:  jobs=M, threads=N/M\n"
"                          where N = hardware_concurrency(). Explicit\n"
"                          values are respected; an explicit -threads\n"
"                          survives the jobs resolver's even split, so\n"
"                          e.g. '-jobs 4 -threads 4' gives 16 active\n"
"                          workers regardless of core count.\n"
"\n"
"                          Rule of thumb: prefer -jobs for batches of\n"
"                          files; prefer -threads for a single large\n"
"                          image; leave both at the default when unsure.\n"
"\n"
"    -verbose              Increases the level of output verbosity.\n"
"    -quiet                Decreases the level of output verbosity.\n"
"\n"
"    -force                Overrides the default program behavior in some\n"
"                          instances such as overwriting existing files and\n"
"                          requiring format specified and file extensions\n"
"                          match.\n"
"\n"
"    -no-batch-math        Skip the Accelerate/SLEEF batched transcendental\n"
"                          dispatch in the SIMD interpreter and run every\n"
"                          stdlib math call through the per-element scalar\n"
"                          libm path. Useful as a same-binary reference\n"
"                          baseline for diffing against the optimized path.\n"
"\n"
"    -help                 Prints this message. Additional help details are\n"
"                          available for some options by specifying an option\n"
"                          after '-help' (e.g. '-help format', '-help pixel').\n"
"\n");
	} else if(!strncmp(section, "format", 1)) {
		fprintf(stdout, "\n"
"format conversion:\n"
"\n"
"    ctlrender provides file format conversion either implicitly by the\n"
"    extension of the output file, or via the use of the '-format' option.\n"
"    Valid values for the '-format' option are:\n"
"\n"
"        dpx10   Produces a DPX file with a 10 bits per sample (32 bit \n"
"                packed) format\n"
"\n"
"        dpx16   Produces a DPX file with a 16 bits per sample format\n"
"\n"
"        dpx     Produces a DPX file with the same bit depth as the source\n"
"                image\n"
"\n"
"        tiff8   Produces a TIFF file in the 8 bits per sample format\n"
"\n"
"        tiff16  Produces a TIFF file in the 16 bits per sample format\n"
"\n"
"        tiff32  Produces a TIFF file in the 32 bits per sample format\n"
"\n"
"        tiff    Produces a TIFF file with the same bit depth as the source\n"
"                image\n"
"\n"
"        exr16   Produces an exr file in the half (16 bit float) per sample\n"
"                format\n"
"\n"
"        exr32   Produces an exr file in the float (32 bit float) per sample\n"
"                format\n"
"\n"
"        exr     Produces an exr file with the same bit depth as the source\n"
"\n"
"        aces    Produces an aces compliant exr file\n"
"\n"
"    When only one source file is specified with a destination file name,\n"
"    the extension is interpreted the same way as an argument to '-format',\n"
"    and will not be changed.\n"
"\n"
"    When the destination is a directory and the -format is provided, the\n"
"    file extension will be changed to the type specified in the -format\n"
"    option with the bit depth removed.\n"
"\n"
"    Note that no automatic depth scaling is performed, please see\n"
"    '-help scale' for more details on how scaling is performed.\n"
"\n");
    } else if(!strncmp(section, "compression", 2)) {
#if defined(HAVE_OPENEXR)
        fprintf(stdout, "\n"
"exr compression:\n"
"\n"
"    ctlrender provides the option of a compression scheme when saving an \n"
"    OpenEXR image. If '-compression' option is not given, PIZ will be used.\n"
"    Valid values for the '-compression' option are:\n"
"\n"
"        NONE    Do not compress.\n"
"\n"
"        PIZ     (lossless) Ideal for photographic images.\n"
"                Default compression scheme.\n"
"\n"
"        ZIPS    (lossless) ZIP one scanline at a time.\n"
"\n"
"        ZIP     (lossless) Ideal for texture maps.\n"
"\n"
"        RLE     (lossless) Ideal for images with large flat areas.\n"
"\n"
"        PXR24   (lossy) Ideal for images with a large range of values but\n"
"                full 32-bit accuracy is not necessary (e.g. depth buffer).\n"
"                HALF and UINT channels are preserved exactly.\n"
"\n"
"        B44     (lossy) Possibly advantageous to real-time playback systems.\n"
"\n"
"        B44A    (lossy) Like B44 but smaller for images containing large\n"
"                uniform areas.\n"
"\n"
"        DWAA    (lossy) Lossy compression of RGB data by quantizing discrete cosine transform (DCT) components,\n"
"                in blocks of 32 scanlines. More efficient for partial buffer access.\n"
"\n"
"        DWAB    (lossy) Lossy compression of RGB data by quantizing discrete cosine transform (DCT) components,\n"
"                in blocks of 256 scanlines. More efficient space wise and faster to decode full frames than DWAA access.\n"

#if (defined(OPENEXR_VERSION_MAJOR) && (OPENEXR_VERSION_MAJOR >= 3) && defined(OPENEXR_VERSION_MAJOR) && (OPENEXR_VERSION_MINOR >= 4)) || (defined(OPENEXR_VERSION_MAJOR) && (OPENEXR_VERSION_MAJOR >= 4))

"\n"
"        HTJ2K256 (lossless) High Throughput JPEG 2000 compression.\n" 
"                 Compression is performed on blocks of 256 scanlines.\n"
"\n"
"        HTJ2K32  (lossless) High Throughput JPEG 2000 compression.\n"
"                 Compression is performed on blocks of 32 scanlines.\n"
"\n");
#else
	);
#endif
#else
        fprintf(stdout, "\n"
"exr compression:\n"
"\n"
"    ctlrender provides the option of a compression scheme when saving an \n"
"    OpenEXR image. If '-compression' option is not given, PIZ will be used.\n"
"    Valid values for the '-compression' option are:\n"
"\n"
"        NONE    Do not compress.\n"
"\n"
"    OpenEXR support must be enabled for the '-compression' option to be\n"
"    meaningful. Please see build documentation for details.\n"
"\n");
#endif
	} else if(!strncmp(section, "ctl", 1)) {
		fprintf(stdout, "\n"
"ctl file interpretation:\n"
"    ctlrender treats all ctl files as if they take their input as 'R', 'G',\n"
"    'B', and 'A' (optional) channels, and produce output as 'R', 'G', and\n"
"    'B', and 'A' (if required) channels. In the event of a single channel\n"
"    input file only the 'G' channel will be used.\n"
"\n");
	} else if(!strncmp(section, "pixel", 2)) {
		fprintf(stdout, "\n"
"-pixel mode (one pixel from the command line, result to stdout):\n"
"\n"
"    Usage:\n"
"        ctlrender -ctl <script> [-ctl <script> ...] -pixel r,g,b[,a]\n"
"\n"
"    Apply the CTL transform chain to a single pixel whose channel values\n"
"    are given as comma-separated floats.  No input image is read; no\n"
"    output file is written.  The transformed pixel is printed to stdout\n"
"    in the form:\n"
"        R=<float>  G=<float>  B=<float>  [A=<float>]\n"
"    with 7 decimal digits of precision.\n"
"\n"
"    Supplying 3 values (r,g,b) runs the chain at depth 3 (no alpha\n"
"    channel), matching the semantics of a 3-channel EXR or DPX input.\n"
"    Supplying 4 values (r,g,b,a) runs at depth 4.  CTL scripts that\n"
"    expect an 'aIn' input (e.g. a premult op) require depth 4.\n"
"\n"
"    Multiple -ctl files chain in order, identical to the image path:\n"
"    each script's main() runs on the result of the previous script.\n"
"\n"
"    Interaction with other flags:\n"
"      -input_scale / -output_scale  apply to the pixel as they would\n"
"                                    to image data\n"
"      -param1 / -param2 / ...       apply as usual\n"
"      -format / -compression        ignored (no output file)\n"
"      -threads / -jobs              irrelevant for one pixel; ignored\n"
"\n"
"    A positional input file cannot be combined with -pixel.  -pixel\n"
"    requires at least one -ctl script.\n"
"\n"
"    Examples:\n"
"        ctlrender -ctl identity.ctl -pixel 0.5,0.25,0.1,1.0\n"
"        ctlrender -ctl gain.ctl -ctl gamma.ctl -pixel 0.1,0.2,0.3\n"
"\n"
"    ctlrender-metal supports -pixel with the same syntax; multi -ctl\n"
"    chains work on both binaries.\n"
"\n");
//"    The *LAST* function in the file is the function that will be called to\n"
//"    provide the transform. This is to maintain compatability with scripts\n"
//"    developed for Autodesk's TOXIC product.\n"
	} else if(!strncmp(section, "scale", 1)) {
		fprintf(stderr, "\n"
"input and output value scaling:\n"
"\n"
"    To deal with differences in input and output file bit depth, the ability\n"
"    to scale input and output values has been provided. While these options\n"
"    are primarily of use for integral file formats, they can be used with\n"
"    file formats that store data in floating point or psuedo-floating point\n"
"    formats. The default handling of the input and output scaling is variant\n"
"    on the format of the input (and output) file, but is intended to behave \n"
"    as one expects.\n"
"\n"
"    integral input files (integer tiff, integer dpx):\n"
"        If the '-input_scale' option is provided then the sample value from\n"
"        the file is *divided by* the specified scale.\n"
"        If the '-input_scale' option is not provided, then the input values\n"
"        are scaled to the range 0.0-1.0 (inclusive). For the purposes of\n"
"        this argument, DPX files are considered an integral file format,\n"
"        however ACES files are *not*. This is equivalent to specifying\n"
"        -input_scale <bits_per_sample_in_input_file>\n"
"\n"    
"    floating point input files (exr, floatint point TIFF, floating point\n"
"    dpx):\n"
"        If the '-input_scale' option is provided then the sample values\n"
"        are *multiplied by* the scale value.\n"
"        If the '-input_scale' option is not provided then the sample values\n"
"        from the file is used as-is (with a scale of 1.0).\n"
"\n"
"    integral output files (integer tiff, integer dpx):\n"
"        If the '-output_scale' option is provided then the sample value from\n"
"        the CTL transformation is *multiplied by* the scale factor.\n"
"        If the '-output_scale' option is not provided, then the values of\n"
"        0.0-1.0 from the CTL transformation are scaled to the bit depth of\n"
"        the output file. For the purposes of this argument, DPX files are\n"
"        considered an integral file format, however ACES files are *not*.\n"
"        This is equivalent to specifying\n"
"        -output_scale <bits_per_sample_in_output_file>\n"
"\n"
"    floating point output files (exr, floatint point TIFF, floating point\n"
"    dpx):\n"
"        If the '-output_scale' option is provided then the sample values\n"
"        are *divided by* the scale value.\n"
"        If the '-output_scale' option is not provided then the sample values\n"
"        from the file is used as-is (with a scale of 1.0).\n"
"\n"
"    In all cases the CTL output values (after output_scaling) are clipped\n"
"    to the maximum values supported by the output file format.\n"
"\n");
	} else if(!strncmp(section, "param", 1)) {
		fprintf(stdout, "\n"
"ctl parameters:\n"
"\n"
"    In CTL scripts it is possible to define parameters that are not set\n"
"    until runtime. These parameters take one, two, or three floating point\n"
"    values. There are three options that allow you to specify the name\n"
"    of the parameter and the associated values. The options are as follows:\n"
"\n"
"        -param1 <name> <float1>\n"
"        -param2 <name> <float1> <float2>\n"
"        -param3 <name> <float1> <float2> <float3>\n"
"\n"
"        -global_param1 <name> <float1>\n"
"        -global_param2 <name> <float1> <float2>\n"
"        -global_param3 <name> <float1> <float2> <float3>\n"
"\n");
	} else {
		fprintf(stdout, ""
"The '%s' section of the help does not exist. Try running ctlrender with\n"
"only the -help option.\n"
"", section);
	}
}
