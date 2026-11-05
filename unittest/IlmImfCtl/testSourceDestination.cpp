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

#include <ImfCtlApplyTransforms.h>
#include <CtlSimdInterpreter.h>
#include <ImfHeader.h>
#include <ImfFrameBuffer.h>
#include <ImfArray.h>
#include <ImfFloatAttribute.h>
#include <ImfThreading.h>
#include <ImathRandom.h>
#include <iostream>
#include <exception>
#include <cassert>

using namespace Ctl;
using namespace Imf;
using namespace ImfCtl;
using namespace Imath;
using namespace std;

namespace {

void
runTest (Interpreter &interp, int numThreads)
{
    cout << "\tnumber of threads = " << numThreads << endl;

    setGlobalThreadCount (numThreads);

    Rand48 rand (numThreads);

    //
    // Create transformNames
    //

    StringList transformNames;
    transformNames.push_back ("function1");
    transformNames.push_back ("function2");

    //
    // Create inHeader, envHeader, outHeader
    //

    Header inHeader;
    inHeader.insert ("a1", FloatAttribute (rand.nextf()));
    inHeader.insert ("f2", FloatAttribute (rand.nextf()));

    Header envHeader;
    envHeader.insert ("b1", FloatAttribute (rand.nextf()));
    envHeader.insert ("g2", FloatAttribute (rand.nextf()));

    Header outHeader;
    outHeader.insert ("a2", FloatAttribute (-1));
    outHeader.insert ("a3", FloatAttribute (-1));
    outHeader.insert ("b3", FloatAttribute (-1));
    outHeader.insert ("c3", FloatAttribute (-1));
    outHeader.insert ("f3", FloatAttribute (-1));
    outHeader.insert ("g3", FloatAttribute (-1));
    outHeader.insert ("i3", FloatAttribute (-1));
    outHeader.insert ("n2", FloatAttribute (-1));

    //
    // Create inFb, outFb
    //

    Box2i tw (V2i (1110, -80), V2i (1200, 100));

    size_t twWidth  = (tw.max.x - tw.min.x + 1);
    size_t twHeight = (tw.max.y - tw.min.y + 1);
    size_t nPixels  = twWidth * twHeight;

    Array <half> d1 (nPixels);
    Array <half> h2 (nPixels);

    for (size_t i = 0; i < nPixels; ++i)
    {
	d1[i] = rand.nextf();
	h2[i] = rand.nextf();
    }

    FrameBuffer inFb;

    inFb.insert ("d1", Slice::Make (HALF, &d1[0], tw));
    inFb.insert ("h2", Slice::Make (HALF, &h2[0], tw));

    Array <half> d2 (nPixels);
    Array <half> d3 (nPixels);
    Array <half> e3 (nPixels);
    Array <half> h3 (nPixels);
    Array <half> j3 (nPixels);
    Array <half> l2 (nPixels);

    FrameBuffer outFb;

    outFb.insert ("d2", Slice::Make (HALF, &d2[0], tw));
    outFb.insert ("d3", Slice::Make (HALF, &d3[0], tw));
    outFb.insert ("e3", Slice::Make (HALF, &e3[0], tw));
    outFb.insert ("h3", Slice::Make (HALF, &h3[0], tw));
    outFb.insert ("j3", Slice::Make (HALF, &j3[0], tw));
    outFb.insert ("l2", Slice::Make (HALF, &l2[0], tw));

    //
    // Call functions
    //

    applyTransforms (interp,
		     transformNames,
		     tw,
		     envHeader,
		     inHeader,
		     inFb,
		     outHeader,
		     outFb);

    //
    // Check data in outHeader
    //

    assert (outHeader.typedAttribute<FloatAttribute>("a2").value() ==
	     inHeader.typedAttribute<FloatAttribute>("a1").value());

    assert (outHeader.typedAttribute<FloatAttribute>("a3").value() ==
	     inHeader.typedAttribute<FloatAttribute>("a1").value());

    assert (outHeader.typedAttribute<FloatAttribute>("b3").value() ==
	    envHeader.typedAttribute<FloatAttribute>("b1").value());

    assert (outHeader.typedAttribute<FloatAttribute>("c3").value() == 23.5);

    assert (outHeader.typedAttribute<FloatAttribute>("f3").value() ==
	     inHeader.typedAttribute<FloatAttribute>("f2").value());

    assert (outHeader.typedAttribute<FloatAttribute>("g3").value() ==
	    envHeader.typedAttribute<FloatAttribute>("g2").value());

    assert (outHeader.typedAttribute<FloatAttribute>("i3").value() == 1.0);

    assert (outHeader.typedAttribute<FloatAttribute>("n2").value() == 456.0);

    //
    // Check data in outFb
    //

    for (size_t i = 0; i < nPixels; ++i)
    {
	assert (d2[i] == d1[i]);
	assert (d3[i] == d1[i]);
	assert (e3[i] == 12);
	assert (h3[i] == h2[i]);
	assert (j3[i] == 2.0);
	assert (l2[i] == 234.0);
    }
}


} // namespace


void
testSourceDestination ()
{
    try
    {
	cout << "Testing function argument sources and destinations" << endl;

	SimdInterpreter interp;
	runTest (interp, 0);
	runTest (interp, 1);
	runTest (interp, 2);
	runTest (interp, 3);

	cout << "ok\n" << endl;
    }
    catch (const std::exception &e)
    {
	cerr << "ERROR -- caught exception: " << e.what() << endl;
	assert (false);
    }
}
