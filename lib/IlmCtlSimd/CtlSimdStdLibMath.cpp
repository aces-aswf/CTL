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

//-----------------------------------------------------------------------------
//
//	The Standard Library of C++ functions that can be called from CTL
//	
//	- math functions (cos(), exp(), sqrt(), etc.)
//	- vectors and matrices
//
//-----------------------------------------------------------------------------

#include <CtlSimdStdLibMath.h>
#include <CtlSimdStdLibTemplates.h>
#include <CtlSimdStdLibrary.h>
#include <CtlSimdStdTypes.h>
#include <CtlSimdCFunc.h>
#include <CtlSimdHalfExpLog.h>
#include <ImathMatrix.h>
#include <cmath>

#if CTL_USE_ACCELERATE
#include <Accelerate/Accelerate.h>
#include <algorithm>
#endif

#if CTL_USE_SLEEF && !CTL_USE_ACCELERATE
#include <sleef.h>
#include <cstring>
#endif

using namespace Imath;
using namespace std;

namespace Ctl {
namespace {

DEFINE_SIMD_FUNC_1_ARG (Acos, acos (a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (Asin, asin (a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (Atan, atan (a1), float, float);
DEFINE_SIMD_FUNC_2_ARG (Atan2, atan2 (a1, a2), float, float, float);
DEFINE_SIMD_FUNC_1_ARG (Cos, cos (a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (Sin, sin (a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (Tan, tan (a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (Cosh, cosh (a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (Sinh, sinh (a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (Tanh, tanh (a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (Exp, exp (a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (ExpH, exp_h (a1), half, float);
DEFINE_SIMD_FUNC_1_ARG (Log, log (a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (LogH, log_h (a1), float, half);
DEFINE_SIMD_FUNC_1_ARG (Log10, log10 (a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (Log10H, log10_h (a1), float, half);
DEFINE_SIMD_FUNC_2_ARG (Pow, pow (a1, a2), float, float, float);
DEFINE_SIMD_FUNC_2_ARG (PowH, pow_h (a1, a2), half, half, float);
DEFINE_SIMD_FUNC_1_ARG (Pow10, pow (10.0f, a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (Pow10H, pow10_h (a1), half, float);
DEFINE_SIMD_FUNC_1_ARG (Sqrt, sqrt (a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (Fabs, fabs (a1), float, float);
DEFINE_SIMD_FUNC_1_ARG (Floor, floor (a1), float, float);
DEFINE_SIMD_FUNC_2_ARG (Fmod, fmod (a1, a2), float, float, float);
DEFINE_SIMD_FUNC_2_ARG (Hypot, hypot (a1, a2), float, float, float);
DEFINE_SIMD_FUNC_2_ARG (Mult_f33_f33, a1 * a2, M33f, M33f, M33f);
DEFINE_SIMD_FUNC_2_ARG (Mult_f44_f44, a1 * a2, M44f, M44f, M44f);
DEFINE_SIMD_FUNC_2_ARG (Mult_f_f33, a1 * a2, M33f, float, M33f);
DEFINE_SIMD_FUNC_2_ARG (Mult_f_f44, a1 * a2, M44f, float, M44f);
DEFINE_SIMD_FUNC_2_ARG (Add_f33_f33, a1 + a2, M33f, M33f, M33f);
DEFINE_SIMD_FUNC_2_ARG (Add_f44_f44, a1 + a2, M44f, M44f, M44f);
DEFINE_SIMD_FUNC_1_ARG (Invert_f33, a1.inverse(), M33f, M33f);
DEFINE_SIMD_FUNC_1_ARG (Invert_f44, a1.inverse(), M44f, M44f);
DEFINE_SIMD_FUNC_1_ARG (Transpose_f33, a1.transposed(), M33f, M33f);
DEFINE_SIMD_FUNC_1_ARG (Transpose_f44, a1.transposed(), M44f, M44f);
DEFINE_SIMD_FUNC_2_ARG (Mult_f3_f33, a1 * a2, V3f, V3f, M33f);
DEFINE_SIMD_FUNC_2_ARG (Mult_f3_f44, a1 * a2, V3f, V3f, M44f);
DEFINE_SIMD_FUNC_2_ARG (Mult_f_f3, a1 * a2, V3f, float, V3f);
DEFINE_SIMD_FUNC_2_ARG (Add_f3_f3, a1 + a2, V3f, V3f, V3f);
DEFINE_SIMD_FUNC_2_ARG (Sub_f3_f3, a1 - a2, V3f, V3f, V3f);
DEFINE_SIMD_FUNC_2_ARG (Cross_f3_f3, a1.cross(a2), V3f, V3f, V3f);
DEFINE_SIMD_FUNC_2_ARG (Dot_f3_f3, a1.dot(a2), float, V3f, V3f);
DEFINE_SIMD_FUNC_1_ARG (Length_f3, a1.length(), float, V3f);

} // namespace

#if CTL_USE_ACCELERATE

//
// Per-function Accelerate vForce specializations.  Each routes the
// 4096-lane contiguous-varying fast path through the Apple-tuned
// batched entry point.  Non-contiguous / uniform / masked lanes still
// go through Func::call (scalar libm).  Apple's vForce documents <=1 ULP
// vs platform libm for these functions; opting in trades that drift for
// substantial throughput.
//
// Accelerate signatures:
//   void vvexpf (float *y, const float *x, const int *n);          // y = exp(x)
//   void vvpowf (float *y, const float *a, const float *b,         // y = b**a
//                const int *n);
//   void vvatan2f (float *y, const float *a, const float *b,       // y = atan2(a,b)
//                  const int *n);
//

#define CTL_ACC_BATCH_1(Class, vvFn)					\
    template <>								\
    struct SimdFuncBatch1<Class>					\
    {									\
	static void run1 (const float *in, float *out, int n)		\
	{ vvFn (out, in, &n); }						\
    };

CTL_ACC_BATCH_1 (Exp,   vvexpf)
CTL_ACC_BATCH_1 (Log,   vvlogf)
CTL_ACC_BATCH_1 (Log10, vvlog10f)
CTL_ACC_BATCH_1 (Sin,   vvsinf)
CTL_ACC_BATCH_1 (Cos,   vvcosf)
CTL_ACC_BATCH_1 (Tan,   vvtanf)
CTL_ACC_BATCH_1 (Asin,  vvasinf)
CTL_ACC_BATCH_1 (Acos,  vvacosf)
CTL_ACC_BATCH_1 (Atan,  vvatanf)
CTL_ACC_BATCH_1 (Sinh,  vvsinhf)
CTL_ACC_BATCH_1 (Cosh,  vvcoshf)
CTL_ACC_BATCH_1 (Tanh,  vvtanhf)
CTL_ACC_BATCH_1 (Sqrt,  vvsqrtf)

#undef CTL_ACC_BATCH_1

namespace {

// Per-thread scratch for splatting a uniform scalar into a contiguous
// array, so vvpowf / vvatan2f can consume the uniform side of a vu/uv
// call through the same batched API as vv.  Sized to MAX_REG_SIZE so
// one splat covers any regSize().  thread_local avoids shared-buffer
// contention across concurrent tile workers.
thread_local float g_ctl_acc_uniform_scratch[8192];

inline float *
ctl_acc_broadcast (float v, int n)
{
    std::fill_n (g_ctl_acc_uniform_scratch, n, v);
    return g_ctl_acc_uniform_scratch;
}

} // namespace

// Pow: CTL's pow(a1, a2) = a1**a2 (a1 is base).
// Accelerate's vvpowf(y, a, b, n) computes y = b**a (a is exponent).
// Map: vvpowf(out, /*exp*/ a2, /*base*/ a1, &n).  vu/uv cases broadcast
// the uniform side into the per-thread scratch above and route through
// the same batched call.
template <>
struct SimdFuncBatch2<Pow>
{
    static void run2_vv (const float *a1, const float *a2, float *out, int n)
	{ vvpowf (out, a2, a1, &n); }
    static void run2_vu (const float *a1, const float &a2,
			 float *out, int n)
    {
	float *expBcast = ctl_acc_broadcast (a2, n);
	vvpowf (out, expBcast, a1, &n);
    }
    static void run2_uv (const float &a1, const float *a2,
			 float *out, int n)
    {
	float *baseBcast = ctl_acc_broadcast (a1, n);
	vvpowf (out, a2, baseBcast, &n);
    }
};

// Atan2: CTL's atan2(a1, a2) = atan2(y=a1, x=a2).
// Accelerate's vvatan2f(y, a, b, n) computes y[i] = atan2(a[i], b[i]).
// Map: vvatan2f(out, /*y*/ a1, /*x*/ a2, &n).
template <>
struct SimdFuncBatch2<Atan2>
{
    static void run2_vv (const float *a1, const float *a2, float *out, int n)
	{ vvatan2f (out, a1, a2, &n); }
    static void run2_vu (const float *a1, const float &a2,
			 float *out, int n)
    {
	float *xBcast = ctl_acc_broadcast (a2, n);
	vvatan2f (out, a1, xBcast, &n);
    }
    static void run2_uv (const float &a1, const float *a2,
			 float *out, int n)
    {
	float *yBcast = ctl_acc_broadcast (a1, n);
	vvatan2f (out, yBcast, a2, &n);
    }
};

#endif // CTL_USE_ACCELERATE


#if CTL_USE_SLEEF && !CTL_USE_ACCELERATE

//
// Per-function sleef specializations.  Routes the 4096-lane
// contiguous-varying fast path through sleef's 4-wide SIMD entry
// points.  Non-contiguous / uniform / masked lanes stay on scalar
// libm via Func::call.  sleef's _u10 contract is <=1 ULP vs platform
// libm; _u05 on sqrt matches the same tier Accelerate's vvsqrtf sits
// at.  Tail elements (n % 4) use the Func's scalar libm path so their
// precision matches a CTL_USE_SLEEF=OFF build exactly.
//
// Arch selection is compile-time; no runtime CPUID branch in the
// hot loop.  On x86_64 the baseline is SSE2 (guaranteed on every
// x86_64 CPU); wider (AVX2/AVX-512) is a future expansion of the
// macro below.  On arm64 the baseline is NEON advsimd.
//

#if defined(__aarch64__) || defined(_M_ARM64)
  #define CTL_SLEEF_HAVE_V4 1
  #define CTL_SLEEF_SFX advsimd
  typedef float32x4_t CtlSleefV4f;
#elif defined(__x86_64__) || defined(_M_X64)
  #define CTL_SLEEF_HAVE_V4 1
  #define CTL_SLEEF_SFX sse2
  typedef __m128 CtlSleefV4f;
#else
  #define CTL_SLEEF_HAVE_V4 0
#endif

#if CTL_SLEEF_HAVE_V4

namespace {

inline CtlSleefV4f ctl_sleef_load4 (const float *p)
{
    CtlSleefV4f v;
    std::memcpy (&v, p, sizeof (v));
    return v;
}

inline void ctl_sleef_store4 (float *p, CtlSleefV4f v)
{
    std::memcpy (p, &v, sizeof (v));
}

// In-register splat of a scalar across a 4-lane SIMD register; used
// by the vu/uv paths of two-arg sleef ops to route the uniform side
// through the same 4-wide entry point as vv.
inline CtlSleefV4f ctl_sleef_splat (float s)
{
    float buf[4] = {s, s, s, s};
    return ctl_sleef_load4 (buf);
}

} // namespace

// Two-level paste: expands CTL_SLEEF_SFX before concatenation so the
// resulting token is e.g. Sleef_expf4_u10advsimd (arm64) or
// Sleef_expf4_u10sse2 (x86_64).
#define CTL_SLEEF_CAT_(a, b) a ## b
#define CTL_SLEEF_CAT(a, b) CTL_SLEEF_CAT_(a, b)
#define CTL_SLEEF_FN(base, ulp) \
    CTL_SLEEF_CAT(Sleef_ ## base ## f4_ ## ulp, CTL_SLEEF_SFX)

#define CTL_SLEEF_BATCH_1(Class, base, ulp)				\
    template <>								\
    struct SimdFuncBatch1<Class>					\
    {									\
	static void run1 (const float *in, float *out, int n)		\
	{								\
	    int i = 0;							\
	    for (; i + 4 <= n; i += 4)					\
		ctl_sleef_store4 (out + i,				\
		    CTL_SLEEF_FN (base, ulp) (			\
			ctl_sleef_load4 (in + i)));			\
	    for (; i < n; ++i)						\
		out[i] = Class::call (in[i]);				\
	}								\
    };

CTL_SLEEF_BATCH_1 (Exp,   exp,   u10)
CTL_SLEEF_BATCH_1 (Log,   log,   u10)
CTL_SLEEF_BATCH_1 (Log10, log10, u10)
CTL_SLEEF_BATCH_1 (Sin,   sin,   u10)
CTL_SLEEF_BATCH_1 (Cos,   cos,   u10)
CTL_SLEEF_BATCH_1 (Tan,   tan,   u10)
CTL_SLEEF_BATCH_1 (Asin,  asin,  u10)
CTL_SLEEF_BATCH_1 (Acos,  acos,  u10)
CTL_SLEEF_BATCH_1 (Atan,  atan,  u10)
CTL_SLEEF_BATCH_1 (Sinh,  sinh,  u10)
CTL_SLEEF_BATCH_1 (Cosh,  cosh,  u10)
CTL_SLEEF_BATCH_1 (Tanh,  tanh,  u10)
// sleef publishes sqrt at _u05 (<=0.5 ULP); no _u10 tier.  Matches
// the Accelerate path's vvsqrtf precision.
CTL_SLEEF_BATCH_1 (Sqrt,  sqrt,  u05)

#undef CTL_SLEEF_BATCH_1

// Pow: CTL pow(a1, a2) = a1**a2.
// sleef Sleef_powf4_u10<sfx>(x, y) returns x**y, same order.
// vu/uv cases splat the uniform side into a v4f and route through
// the same 4-wide entry point as vv.
template <>
struct SimdFuncBatch2<Pow>
{
    static void run2_vv (const float *a1, const float *a2, float *out, int n)
    {
	int i = 0;
	for (; i + 4 <= n; i += 4)
	    ctl_sleef_store4 (out + i,
		CTL_SLEEF_FN (pow, u10) (
		    ctl_sleef_load4 (a1 + i),
		    ctl_sleef_load4 (a2 + i)));
	for (; i < n; ++i)
	    out[i] = Pow::call (a1[i], a2[i]);
    }
    static void run2_vu (const float *a1, const float &a2,
			 float *out, int n)
    {
	const CtlSleefV4f s = ctl_sleef_splat (a2);
	int i = 0;
	for (; i + 4 <= n; i += 4)
	    ctl_sleef_store4 (out + i,
		CTL_SLEEF_FN (pow, u10) (ctl_sleef_load4 (a1 + i), s));
	for (; i < n; ++i)
	    out[i] = Pow::call (a1[i], a2);
    }
    static void run2_uv (const float &a1, const float *a2,
			 float *out, int n)
    {
	const CtlSleefV4f s = ctl_sleef_splat (a1);
	int i = 0;
	for (; i + 4 <= n; i += 4)
	    ctl_sleef_store4 (out + i,
		CTL_SLEEF_FN (pow, u10) (s, ctl_sleef_load4 (a2 + i)));
	for (; i < n; ++i)
	    out[i] = Pow::call (a1, a2[i]);
    }
};

// Atan2: CTL atan2(a1, a2) = atan2(y=a1, x=a2).
// sleef Sleef_atan2f4_u10<sfx>(y, x) returns atan2(y, x), same order.
template <>
struct SimdFuncBatch2<Atan2>
{
    static void run2_vv (const float *a1, const float *a2, float *out, int n)
    {
	int i = 0;
	for (; i + 4 <= n; i += 4)
	    ctl_sleef_store4 (out + i,
		CTL_SLEEF_FN (atan2, u10) (
		    ctl_sleef_load4 (a1 + i),
		    ctl_sleef_load4 (a2 + i)));
	for (; i < n; ++i)
	    out[i] = Atan2::call (a1[i], a2[i]);
    }
    static void run2_vu (const float *a1, const float &a2,
			 float *out, int n)
    {
	const CtlSleefV4f s = ctl_sleef_splat (a2);
	int i = 0;
	for (; i + 4 <= n; i += 4)
	    ctl_sleef_store4 (out + i,
		CTL_SLEEF_FN (atan2, u10) (ctl_sleef_load4 (a1 + i), s));
	for (; i < n; ++i)
	    out[i] = Atan2::call (a1[i], a2);
    }
    static void run2_uv (const float &a1, const float *a2,
			 float *out, int n)
    {
	const CtlSleefV4f s = ctl_sleef_splat (a1);
	int i = 0;
	for (; i + 4 <= n; i += 4)
	    ctl_sleef_store4 (out + i,
		CTL_SLEEF_FN (atan2, u10) (s, ctl_sleef_load4 (a2 + i)));
	for (; i < n; ++i)
	    out[i] = Atan2::call (a1, a2[i]);
    }
};

#undef CTL_SLEEF_FN
#undef CTL_SLEEF_CAT
#undef CTL_SLEEF_CAT_

#endif // CTL_SLEEF_HAVE_V4

#endif // CTL_USE_SLEEF && !CTL_USE_ACCELERATE


void
declareSimdStdLibMath (SymbolTable &symtab, SimdStdTypes &types)
{
    declareSimdCFunc (symtab, simdFunc1Arg <Acos>,
		      types.funcType_f_f(), "acos");

    declareSimdCFunc (symtab, simdFunc1Arg <Asin>,
		      types.funcType_f_f(), "asin");

    declareSimdCFunc (symtab, simdFunc1Arg <Atan>,
		      types.funcType_f_f(), "atan");

    declareSimdCFunc (symtab, simdFunc2Arg <Atan2>,
		      types.funcType_f_f_f(), "atan2");

    declareSimdCFunc (symtab, simdFunc1Arg <Cos>,
		      types.funcType_f_f(), "cos");

    declareSimdCFunc (symtab, simdFunc1Arg <Sin>,
		      types.funcType_f_f(), "sin");

    declareSimdCFunc (symtab, simdFunc1Arg <Tan>,
		      types.funcType_f_f(), "tan");

    declareSimdCFunc (symtab, simdFunc1Arg <Cosh>,
		      types.funcType_f_f(), "cosh");

    declareSimdCFunc (symtab, simdFunc1Arg <Sinh>,
		      types.funcType_f_f(), "sinh");

    declareSimdCFunc (symtab, simdFunc1Arg <Tanh>,
		      types.funcType_f_f(), "tanh");

    declareSimdCFunc (symtab, simdFunc1Arg <Exp>,
		      types.funcType_f_f(), "exp");

    declareSimdCFunc (symtab, simdFunc1Arg <ExpH>,
		      types.funcType_h_f(), "exp_h");

    declareSimdCFunc (symtab, simdFunc1Arg <Log>,
		      types.funcType_f_f(), "log");

    declareSimdCFunc (symtab, simdFunc1Arg <LogH>,
		      types.funcType_f_h(), "log_h");

    declareSimdCFunc (symtab, simdFunc1Arg <Log10>,
		      types.funcType_f_f(), "log10");

    declareSimdCFunc (symtab, simdFunc1Arg <Log10H>,
		      types.funcType_f_h(), "log10_h");

    declareSimdCFunc (symtab, simdFunc2Arg <Pow>,
		      types.funcType_f_f_f(), "pow");

    declareSimdCFunc (symtab, simdFunc2Arg <PowH>,
		      types.funcType_h_h_f(), "pow_h");

    declareSimdCFunc (symtab, simdFunc1Arg <Pow10>,
		      types.funcType_f_f(), "pow10");

    declareSimdCFunc (symtab, simdFunc1Arg <Pow10H>,
		      types.funcType_h_f(), "pow10_h");

    declareSimdCFunc (symtab, simdFunc1Arg <Sqrt>,
		      types.funcType_f_f(), "sqrt");

    declareSimdCFunc (symtab, simdFunc1Arg <Fabs>,
		      types.funcType_f_f(), "fabs");

    declareSimdCFunc (symtab, simdFunc1Arg <Floor>,
		      types.funcType_f_f(), "floor");

    declareSimdCFunc (symtab, simdFunc2Arg <Fmod>,
		      types.funcType_f_f_f(), "fmod");

    declareSimdCFunc (symtab, simdFunc2Arg <Hypot>,
		      types.funcType_f_f_f(), "hypot");

    declareSimdCFunc (symtab, simdFunc2Arg <Mult_f33_f33>,
		      types.funcType_f33_f33_f33(), "mult_f33_f33");

    declareSimdCFunc (symtab, simdFunc2Arg <Mult_f44_f44>,
		      types.funcType_f44_f44_f44(), "mult_f44_f44");
		  
    declareSimdCFunc (symtab, simdFunc2Arg <Mult_f_f33>,
		      types.funcType_f33_f_f33(), "mult_f_f33");
		  
    declareSimdCFunc (symtab, simdFunc2Arg <Mult_f_f44>,
		      types.funcType_f44_f_f44(), "mult_f_f44");

    declareSimdCFunc (symtab, simdFunc2Arg <Add_f33_f33>,
		      types.funcType_f33_f33_f33(), "add_f33_f33");

    declareSimdCFunc (symtab, simdFunc2Arg <Add_f44_f44>,
		      types.funcType_f44_f44_f44(), "add_f44_f44");

    declareSimdCFunc (symtab, simdFunc1Arg <Invert_f33>,
		      types.funcType_f33_f33(), "invert_f33");

    declareSimdCFunc (symtab, simdFunc1Arg <Invert_f44>,
		      types.funcType_f44_f44(), "invert_f44");

    declareSimdCFunc (symtab, simdFunc1Arg <Transpose_f33>,
		      types.funcType_f33_f33(), "transpose_f33");

    declareSimdCFunc (symtab, simdFunc1Arg <Transpose_f44>,
		      types.funcType_f44_f44(), "transpose_f44");

    declareSimdCFunc (symtab, simdFunc2Arg <Mult_f3_f33>,
		      types.funcType_f3_f3_f33(), "mult_f3_f33");

    declareSimdCFunc (symtab, simdFunc2Arg <Mult_f3_f44>,
		      types.funcType_f3_f3_f44(), "mult_f3_f44");

    declareSimdCFunc (symtab, simdFunc2Arg <Mult_f_f3>,
		      types.funcType_f3_f_f3(), "mult_f_f3");

    declareSimdCFunc (symtab, simdFunc2Arg <Add_f3_f3>,
		      types.funcType_f3_f3_f3(), "add_f3_f3");

    declareSimdCFunc (symtab, simdFunc2Arg <Sub_f3_f3>,
		      types.funcType_f3_f3_f3(), "sub_f3_f3");

    declareSimdCFunc (symtab, simdFunc2Arg <Cross_f3_f3>,
		      types.funcType_f3_f3_f3(), "cross_f3_f3");

    declareSimdCFunc (symtab, simdFunc2Arg <Dot_f3_f3>,
		      types.funcType_f_f3_f3(), "dot_f3_f3");

    declareSimdCFunc (symtab, simdFunc1Arg <Length_f3>,
		      types.funcType_f_f3(), "length_f3");
}

} // namespace Ctl
