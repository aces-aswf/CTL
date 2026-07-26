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

#ifndef INCLUDED_CTL_SIMD_STD_LIB_TEMPLATES_H
#define INCLUDED_CTL_SIMD_STD_LIB_TEMPLATES_H

//-----------------------------------------------------------------------------
//
//	Templates an macros that are useful for the implementation of the
//	Standard Library of C++ functions that can be called from CTL.
//
//-----------------------------------------------------------------------------

#include <CtlSimdStdTypes.h>
#include <CtlSimdCFunc.h>
#include <CtlSimdInterpreter.h>
#include <CtlSimdReg.h>
#include <CtlSimdXContext.h>

namespace Ctl {

//
// Hot-path batch traits.  The primary templates run scalar per-lane
// loops; specializations (e.g. for Accelerate vForce) may replace the
// hot contiguous-varying inner loop with a batched library call.
// Uniform and masked lanes still go through Func::call.  Two traits
// because 1-arg Funcs have no Arg2T and instantiating a unified trait
// for them would fail template signature instantiation.
//

template <class Func>
struct SimdFuncBatch1
{
    static void run1 (const typename Func::Arg1T *in,
		      typename Func::ReturnT *out, int n)
    {
	while (n-- > 0) *(out++) = Func::call (*(in++));
    }
};

template <class Func>
struct SimdFuncBatch2
{
    static void run2_vv (const typename Func::Arg1T *a1,
			 const typename Func::Arg2T *a2,
			 typename Func::ReturnT *out, int n)
    {
	while (n-- > 0) *(out++) = Func::call (*(a1++), *(a2++));
    }

    static void run2_vu (const typename Func::Arg1T *a1,
			 const typename Func::Arg2T &a2,
			 typename Func::ReturnT *out, int n)
    {
	while (n-- > 0) *(out++) = Func::call (*(a1++), a2);
    }

    static void run2_uv (const typename Func::Arg1T &a1,
			 const typename Func::Arg2T *a2,
			 typename Func::ReturnT *out, int n)
    {
	while (n-- > 0) *(out++) = Func::call (a1, *(a2++));
    }
};

//
// Templated functions with one, two or more arguments that can be called
// from CTL.  The functions will operate on uniform or varying data.  The
// actual operation performed depends on the Func template parameter.
//

template <class Func>
void
simdFunc1Arg (const SimdBoolMask &mask, SimdXContext &xcontext)
{
    const SimdReg &a1 = xcontext.stack().regFpRelative(-1);
    SimdReg &returnValue = xcontext.stack().regFpRelative (-2);

    if (a1.isVarying())
    {
	if (!mask.isVarying() &&
	    !a1.isReference() &&
	    !returnValue.isReference() &&
	    xcontext.interpreter().useBatchedMath())
	{
	    //
	    // Mask is uniform, a1 and the return value are contiguous
	    // in memory, and the interpreter has batched-math dispatch
	    // enabled.
	    //

	    // !returnValue.isReference() already on the guard above, so
	    // _oVarying is false and isVarying() == _varying.  Skip the
	    // no-op call on the hot path where returnValue has already
	    // been promoted varying by an earlier tile.
	    if (!returnValue.isVarying())
		returnValue.setVaryingDiscardData (true);

	    const typename Func::Arg1T *a1Ptr =
				(typename Func::Arg1T *)(a1[0]);

	    typename Func::ReturnT *returnPtr =
				(typename Func::ReturnT *)(returnValue[0]);

	    SimdFuncBatch1<Func>::run1 (a1Ptr, returnPtr, xcontext.regSize());
	}
	else
	{
	    //
	    // Mask is varying, or a1 or the return value
	    // may not be contiguous in memory.
	    //

	    returnValue.setVarying (true);

	    for (int i = xcontext.regSize(); --i >= 0;)
		if (mask[i])
		    *(typename Func::ReturnT *)(returnValue[i]) =
			Func::call (*(typename Func::Arg1T *)(a1[i]));
	}
    }
    else
    {
	returnValue.setVarying (false);

	*(typename Func::ReturnT *)(returnValue[0]) =
	    Func::call (*(typename Func::Arg1T *)(a1[0]));
    }
}


template <class Func>
void
simdFunc2Arg (const SimdBoolMask &mask, SimdXContext &xcontext)
{
    const SimdReg &a1 = xcontext.stack().regFpRelative (-1);
    const SimdReg &a2 = xcontext.stack().regFpRelative (-2);
    SimdReg &returnValue = xcontext.stack().regFpRelative (-3);

    if (a1.isVarying() || a2.isVarying())
    {
	if (!mask.isVarying() &&
	    !a1.isReference() &&
	    !a2.isReference() &&
	    !returnValue.isReference() &&
	    xcontext.interpreter().useBatchedMath())
	{
	    //
	    // Mask is uniform, a1, a2 and the return value are
	    // contiguous in memory, and the interpreter has batched-
	    // math dispatch enabled.  A1, a2 or both are varying.
	    //

	    // !returnValue.isReference() already on the guard above, so
	    // _oVarying is false and isVarying() == _varying.  Skip the
	    // no-op call on the hot path where returnValue has already
	    // been promoted varying by an earlier tile.
	    if (!returnValue.isVarying())
		returnValue.setVaryingDiscardData (true);

	    const typename Func::Arg1T *a1Ptr =
				(typename Func::Arg1T *)(a1[0]);

	    const typename Func::Arg2T *a2Ptr =
				(typename Func::Arg2T *)(a2[0]);

	    typename Func::ReturnT *returnPtr =
				(typename Func::ReturnT *)(returnValue[0]);

	    int n = xcontext.regSize();

	    if (a1.isVarying() && a2.isVarying())
	    {
		SimdFuncBatch2<Func>::run2_vv (a1Ptr, a2Ptr, returnPtr, n);
	    }
	    else if (a1.isVarying())
	    {
		SimdFuncBatch2<Func>::run2_vu (a1Ptr, *a2Ptr, returnPtr, n);
	    }
	    else
	    {
		SimdFuncBatch2<Func>::run2_uv (*a1Ptr, a2Ptr, returnPtr, n);
	    }
	}
	else
	{
	    //
	    // Mask is varying, or a1, a2 or the return value
	    // may not be contiguous in memory.
	    //

	    returnValue.setVarying (true);

	    for (int i = xcontext.regSize(); --i >= 0;)
		if (mask[i])
		    *(typename Func::ReturnT *)(returnValue[i]) =
			Func::call (*(typename Func::Arg1T *)(a1[i]), 
				    *(typename Func::Arg2T *)(a2[i]));
	}
    }
    else
    {
	returnValue.setVarying (false);

	*(typename Func::ReturnT *)(returnValue[0]) =
	    Func::call (*(typename Func::Arg1T *)(a1[0]), 
		        *(typename Func::Arg2T *)(a2[0]));
    }
}


//
// Function objects that can be passed to the templates above.
//

#define DEFINE_SIMD_FUNC_1_ARG(className, op, rT, a1T)			\
									\
    struct className							\
    {									\
	typedef rT ReturnT;						\
	typedef a1T Arg1T;						\
									\
	static ReturnT							\
	call (const Arg1T &a1)						\
	{								\
	    return op;							\
	}								\
    };


#define DEFINE_SIMD_FUNC_2_ARG(className, op, rT, a1T, a2T)		\
									\
    struct className							\
    {									\
	typedef rT ReturnT;						\
	typedef a1T Arg1T;						\
	typedef a2T Arg2T;						\
									\
	static ReturnT							\
	call (const Arg1T &a1,						\
	      const Arg2T &a2)						\
	{								\
	    return op;							\
	}								\
    };


} // namespace Ctl

#endif
