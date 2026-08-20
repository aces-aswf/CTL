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
//	- interpolated 1D and 3D table lookups
//
//-----------------------------------------------------------------------------

#include <CtlSimdStdLibLookupTable.h>
#include <CtlSimdStdLibTemplates.h>
#include <CtlSimdStdLibrary.h>
#include <CtlSimdStdTypes.h>
#include <CtlSimdCFunc.h>
#include <CtlLookupTable.h>
#include <half.h>
#include <cmath>
#include <cassert>

using namespace Imath;
using namespace std;

namespace Ctl {
namespace {

typedef float (*Lookup1DFunc) (const float[], int, float, float, float);


void
simdDoLookup1D
    (const SimdBoolMask &mask,
     SimdXContext &xcontext,
     Lookup1DFunc func)
{
    //
    // float func (float table[], float pMin, float pMax, float p)
    //

    const SimdReg &size  = xcontext.stack().regFpRelative (-1);
    const SimdReg &table = xcontext.stack().regFpRelative (-2);
    const SimdReg &pMin  = xcontext.stack().regFpRelative (-3);
    const SimdReg &pMax  = xcontext.stack().regFpRelative (-4);
    const SimdReg &p     = xcontext.stack().regFpRelative (-5);
    SimdReg &returnValue = xcontext.stack().regFpRelative (-6);

    assert (!size.isVarying());
    int s = *(int *)(size[0]);

    if (table.isVarying() ||
	pMin.isVarying() ||
	pMax.isVarying() ||
	p.isVarying())
    {
	returnValue.setVarying (true);

	if (!mask.isVarying() &&
	    !table.isVarying() &&
	    !pMin.isVarying() &&
	    !pMax.isVarying())
	{
	    //
	    // Fast path -- only p is varying, everything else is uniform.
	    //

	    float *table0 = (float *)(table[0]);
	    float pMin0 = *(float *)(pMin[0]);
	    float pMax0 = *(float *)(pMax[0]);

	    for (int i = xcontext.regSize(); --i >= 0;)
	    {
		*(float *)(returnValue[i]) = func (table0,
						   s,
						   pMin0,
						   pMax0,
						   *(float *)(p[i]));
	    }
	}
	else
	{
	    for (int i = xcontext.regSize(); --i >= 0;)
	    {
		if (mask[i])
		{
		    *(float *)(returnValue[i]) = func ((float *)(table[i]), 
						       s,
						       *(float *)(pMin[i]),
						       *(float *)(pMax[i]),
						       *(float *)(p[i]));
		}
	    }
	}
    }
    else
    {
	returnValue.setVarying (false);

	*(float *)(returnValue[0]) = func ((float *)(table[0]), 
					   s,
					   *(float *)(pMin[0]),
					   *(float *)(pMax[0]),
					   *(float *)(p[0]));
    }
}


void
simdLookup1D (const SimdBoolMask &mask, SimdXContext &xcontext)
{
    //
    // float lookup1D (float table[], float pMin, float pMax, float p)
    //

    simdDoLookup1D (mask, xcontext, lookup1D);
}


void
simdLookupCubic1D (const SimdBoolMask &mask, SimdXContext &xcontext)
{
    //
    // float lookupCubic1D (float table[], float pMin, float pMax, float p)
    //

    simdDoLookup1D (mask, xcontext, lookupCubic1D);
}


typedef V3f (*Lookup3DFunc) (const V3f[], const V3i &,
			     const V3f &, const V3f &, const V3f &);


void
simdDoLookup3D_f3
    (const SimdBoolMask &mask,
     SimdXContext &xcontext,
     Lookup3DFunc func)
{
    //
    // float[3] func (float table[][][][3],
    //		      float pMin[3], float pMax[3],
    //		      float p[3])
    //

    const SimdReg &size2  = xcontext.stack().regFpRelative (-1);
    const SimdReg &size1  = xcontext.stack().regFpRelative (-2);
    const SimdReg &size0  = xcontext.stack().regFpRelative (-3);
    const SimdReg &table  = xcontext.stack().regFpRelative (-4);
    const SimdReg &pMin   = xcontext.stack().regFpRelative (-5);
    const SimdReg &pMax   = xcontext.stack().regFpRelative (-6);
    const SimdReg &p      = xcontext.stack().regFpRelative (-7);
    SimdReg &returnValue  = xcontext.stack().regFpRelative (-8);

    assert (!size0.isVarying() && !size1.isVarying() && !size2.isVarying());

    V3i s (*(int *)(size0[0]),
	   *(int *)(size1[0]),
	   *(int *)(size2[0]));

    if (table.isVarying() ||
	pMin.isVarying() ||
	pMax.isVarying() ||
	p.isVarying())
    {
	returnValue.setVarying (true);

	for (int i = xcontext.regSize(); --i >= 0;)
	{
	    if (mask[i])
	    {
		*(V3f *)(returnValue[i]) = func ((V3f *)(table[i]),
						 s,
						 *(V3f *)(pMin[i]),
						 *(V3f *)(pMax[i]),
						 *(V3f *)(p[i]));
	    }
	}
    }
    else
    {
	returnValue.setVarying (false);

	*(V3f *)(returnValue[0]) = func ((V3f *)(table[0]),
					 s,
					 *(V3f *)(pMin[0]),
					 *(V3f *)(pMax[0]),
					 *(V3f *)(p[0]));
    }
}


void
simdLookup3D_f3 (const SimdBoolMask &mask, SimdXContext &xcontext)
{
    //
    // float[3] lookup3D_f3 (float table[][][][3],
    //			     float pMin[3], float pMax[3],
    //			     float p[3])
    //

    simdDoLookup3D_f3 (mask, xcontext, lookup3D);
}


void
simdLookup3DTetra_f3 (const SimdBoolMask &mask, SimdXContext &xcontext)
{
    //
    // float[3] lookup3DTetra_f3 (float table[][][][3],
    //			          float pMin[3], float pMax[3],
    //			          float p[3])
    //

    simdDoLookup3D_f3 (mask, xcontext, lookup3DTetra);
}


void
simdDoLookup3D_f
    (const SimdBoolMask &mask,
     SimdXContext &xcontext,
     Lookup3DFunc func)
{
    //
    // void func (float table[][][][3],
    //		  float pMin[3], float pMax[3],
    //		  float p0, float p1, float p2,
    //		  float q0, float q1, float q2)
    //

    const SimdReg &size2  = xcontext.stack().regFpRelative (-1);
    const SimdReg &size1  = xcontext.stack().regFpRelative (-2);
    const SimdReg &size0  = xcontext.stack().regFpRelative (-3);
    const SimdReg &table  = xcontext.stack().regFpRelative (-4);
    const SimdReg &pMin   = xcontext.stack().regFpRelative (-5);
    const SimdReg &pMax   = xcontext.stack().regFpRelative (-6);
    const SimdReg &p0     = xcontext.stack().regFpRelative (-7);
    const SimdReg &p1     = xcontext.stack().regFpRelative (-8);
    const SimdReg &p2     = xcontext.stack().regFpRelative (-9);
    SimdReg &q0           = xcontext.stack().regFpRelative (-10);
    SimdReg &q1           = xcontext.stack().regFpRelative (-11);
    SimdReg &q2           = xcontext.stack().regFpRelative (-12);

    assert (!size0.isVarying() && !size1.isVarying() && !size2.isVarying());

    V3i s (*(int *)(size0[0]),
	   *(int *)(size1[0]),
	   *(int *)(size2[0]));

    if (table.isVarying() ||
	pMin.isVarying() ||
	pMax.isVarying() ||
	p0.isVarying() ||
	p1.isVarying() ||
	p2.isVarying())
    {
	q0.setVarying (true);
	q1.setVarying (true);
	q2.setVarying (true);

	for (int i = xcontext.regSize(); --i >= 0;)
	{
	    if (mask[i])
	    {
		V3f p (*(float *)p0[i], *(float *)p1[i], *(float *)p2[i]);

		V3f q = func ((V3f *)(table[i]),
			      s,
			      *(V3f *)(pMin[i]),
			      *(V3f *)(pMax[i]),
			      p);

		*(float *)q0[i] = q[0];
		*(float *)q1[i] = q[1];
		*(float *)q2[i] = q[2];
	    }
	}
    }
    else
    {
	q0.setVarying (false);
	q1.setVarying (false);
	q2.setVarying (false);

	V3f p (*(float *)p0[0], *(float *)p1[0], *(float *)p2[0]);

	V3f q = func ((V3f *)(table[0]),
		      s,
		      *(V3f *)(pMin[0]),
		      *(V3f *)(pMax[0]),
		      p);

	*(float *)q0[0] = q[0];
	*(float *)q1[0] = q[1];
	*(float *)q2[0] = q[2];
    }
}


void
simdLookup3D_f (const SimdBoolMask &mask, SimdXContext &xcontext)
{
    //
    // void lookup3D_f (float table[][][][3],
    //		        float pMin[3], float pMax[3],
    //		        float p0, float p1, float p2,
    //		        float q0, float q1, float q2)
    //

    simdDoLookup3D_f (mask, xcontext, lookup3D);
}


void
simdLookup3DTetra_f (const SimdBoolMask &mask, SimdXContext &xcontext)
{
    //
    // void lookup3DTetra_f (float table[][][][3],
    //			     float pMin[3], float pMax[3],
    //			     float p0, float p1, float p2,
    //			     float q0, float q1, float q2)
    //

    simdDoLookup3D_f (mask, xcontext, lookup3DTetra);
}


void
simdDoLookup3D_h
    (const SimdBoolMask &mask,
     SimdXContext &xcontext,
     Lookup3DFunc func)
{
    //
    // void func (float table[][][][3],
    //		  float pMin[3], float pMax[3],
    //		  half p0, half p1, half p2,
    //		  half q0, half q1, half q2)
    //

    const SimdReg &size2  = xcontext.stack().regFpRelative (-1);
    const SimdReg &size1  = xcontext.stack().regFpRelative (-2);
    const SimdReg &size0  = xcontext.stack().regFpRelative (-3);
    const SimdReg &table  = xcontext.stack().regFpRelative (-4);
    const SimdReg &pMin   = xcontext.stack().regFpRelative (-5);
    const SimdReg &pMax   = xcontext.stack().regFpRelative (-6);
    const SimdReg &p0     = xcontext.stack().regFpRelative (-7);
    const SimdReg &p1     = xcontext.stack().regFpRelative (-8);
    const SimdReg &p2     = xcontext.stack().regFpRelative (-9);
    SimdReg &q0           = xcontext.stack().regFpRelative (-10);
    SimdReg &q1           = xcontext.stack().regFpRelative (-11);
    SimdReg &q2           = xcontext.stack().regFpRelative (-12);

    assert (!size0.isVarying() && !size1.isVarying() && !size2.isVarying());

    V3i s (*(int *)(size0[0]),
	   *(int *)(size1[0]),
	   *(int *)(size2[0]));

    if (table.isVarying() ||
	pMin.isVarying() ||
	pMax.isVarying() ||
	p0.isVarying() ||
	p1.isVarying() ||
	p2.isVarying())
    {
	q0.setVarying (true);
	q1.setVarying (true);
	q2.setVarying (true);

	for (int i = xcontext.regSize(); --i >= 0;)
	{
	    if (mask[i])
	    {
		V3f p (*(half *)p0[i], *(half *)p1[i], *(half *)p2[i]);

		V3f q = func ((V3f *)(table[i]),
			      s,
			      *(V3f *)(pMin[i]),
			      *(V3f *)(pMax[i]),
			      p);

		*(half *)q0[i] = q[0];
		*(half *)q1[i] = q[1];
		*(half *)q2[i] = q[2];
	    }
	}
    }
    else
    {
	q0.setVarying (false);
	q1.setVarying (false);
	q2.setVarying (false);

	V3f p (*(half *)p0[0], *(half *)p1[0], *(half *)p2[0]);

	V3f q = func ((V3f *)(table[0]),
		      s,
		      *(V3f *)(pMin[0]),
		      *(V3f *)(pMax[0]),
		      p);

	*(half *)q0[0] = q[0];
	*(half *)q1[0] = q[1];
	*(half *)q2[0] = q[2];
    }
}


void
simdLookup3D_h (const SimdBoolMask &mask, SimdXContext &xcontext)
{
    //
    // void lookup3D_h (float table[][][][3],
    //		        float pMin[3], float pMax[3],
    //		        half p0, half p1, half p2,
    //		        half q0, half q1, half q2)
    //

    simdDoLookup3D_h (mask, xcontext, lookup3D);
}


void
simdLookup3DTetra_h (const SimdBoolMask &mask, SimdXContext &xcontext)
{
    //
    // void lookup3DTetra_h (float table[][][][3],
    //			     float pMin[3], float pMax[3],
    //			     half p0, half p1, half p2,
    //			     half q0, half q1, half q2)
    //

    simdDoLookup3D_h (mask, xcontext, lookup3DTetra);
}


typedef float (*Interpolate1DFunc) (const float[][2], int, float);


void
simdDoInterpolate1D
    (const SimdBoolMask &mask,
     SimdXContext &xcontext,
     Interpolate1DFunc func)
{
    //
    // float func (float table[][2], float p)
    //

    const SimdReg &size  = xcontext.stack().regFpRelative (-1);
    const SimdReg &table = xcontext.stack().regFpRelative (-2);
    const SimdReg &p     = xcontext.stack().regFpRelative (-3);
    SimdReg &returnValue = xcontext.stack().regFpRelative (-4);

    assert (!size.isVarying());
    int s = *(int *)(size[0]);

    if (table.isVarying() ||
	p.isVarying())
    {
	returnValue.setVarying (true);

	if (!mask.isVarying() &&
	    !table.isVarying())
	{
	    //
	    // Fast path -- only p is varying, everything else is uniform.
	    //

	    float (*table0)[2] = (float (*)[2])(table[0]);

	    for (int i = xcontext.regSize(); --i >= 0;)
	    {
		*(float *)(returnValue[i]) = func (table0,
						   s,
						   *(float *)(p[i]));
	    }
	}
	else
	{
	    for (int i = xcontext.regSize(); --i >= 0;)
	    {
		if (mask[i])
		{
		    *(float *)(returnValue[i]) = func ((float (*)[2])(table[i]),
						       s,
						       *(float *)(p[i]));
		}
	    }
	}
    }
    else
    {
	returnValue.setVarying (false);

	*(float *)(returnValue[0]) = func ((float (*)[2])(table[0]), 
					   s,
					   *(float *)(p[0]));
    }
}


void
simdInterpolate1D (const SimdBoolMask &mask, SimdXContext &xcontext)
{
    //
    // float interpolate1D (float table[][2], float p)
    //

    simdDoInterpolate1D (mask, xcontext, interpolate1D);
}


void
simdInterpolateCubic1D (const SimdBoolMask &mask, SimdXContext &xcontext)
{
    //
    // float interpolateCubic1D (float table[][2], float p)
    //

    simdDoInterpolate1D (mask, xcontext, interpolateCubic1D);
}

} // namespace


void
declareSimdStdLibLookupTable (SymbolTable &symtab, SimdStdTypes &types)
{
    declareSimdCFunc (symtab, simdLookup1D,
		      types.funcType_f_f0_f_f_f(), "lookup1D");

    declareSimdCFunc (symtab, simdLookupCubic1D,
		      types.funcType_f_f0_f_f_f(), "lookupCubic1D");

    declareSimdCFunc (symtab, simdLookup3D_f3,
		      types.funcType_f3_f0003_f3_f3_f3(), "lookup3D_f3");

    declareSimdCFunc (symtab, simdLookup3D_f,
		      types.funcType_v_f0003_f3_f3_fff_offf(), "lookup3D_f");

    declareSimdCFunc (symtab, simdLookup3D_h,
		      types.funcType_v_f0003_f3_f3_hhh_ohhh(), "lookup3D_h");

    declareSimdCFunc (symtab, simdLookup3DTetra_f3,
		      types.funcType_f3_f0003_f3_f3_f3(), "lookup3DTetra_f3");

    declareSimdCFunc (symtab, simdLookup3DTetra_f,
		      types.funcType_v_f0003_f3_f3_fff_offf(), "lookup3DTetra_f");

    declareSimdCFunc (symtab, simdLookup3DTetra_h,
		      types.funcType_v_f0003_f3_f3_hhh_ohhh(), "lookup3DTetra_h");

    declareSimdCFunc (symtab, simdInterpolate1D,
		      types.funcType_f_f02_f(), "interpolate1D");

    declareSimdCFunc (symtab, simdInterpolateCubic1D,
		      types.funcType_f_f02_f(), "interpolateCubic1D");
}

} // namespace Ctl
