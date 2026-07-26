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
//	Registers for the SIMD color transformation engine
//
//-----------------------------------------------------------------------------

#include <CtlSimdReg.h>
#include <CtlSimdArena.h>
#include <sstream>
#include <algorithm>
#include <cstdint>



namespace Ctl {

namespace detail {

BoolBufferPool &
boolBufferPool ()
{
    // One pool per thread: under Phase-B tile dispatch each worker's mask
    // traffic (trueMask/falseMask/loopMask/callMask, all stack-scoped)
    // reuses the same ~10-20 cache-hot 8 KB buffers without any locking.
    static thread_local BoolBufferPool pool;
    return pool;
}

} // namespace detail


namespace {

size_t zeroOffsetPlaceholder = 0;

void
throwIndexOutOfRange (int index, int size)
{
    THROW (IndexOutOfRangeExc,
	   "Array index out of range "
	   "(index = " << index << ", "
	   "array size = " << size << ").");
}


// Broadcast one `eSize`-byte element across `count` slots of `dst`.  Dispatches
// to a typed fill for the 4 sizes that cover ~every CTL scalar register
// (bool/char, short, float/int32, double/int64); callers on float dominate the
// hot path (SimdReg::setVarying under SimdAssignInst at 20.9% of wall-clock).
// For other sizes, falls back to memcpy-in-loop.  dst alignment: arena gives
// 16 B, `new char[]` gives max_align_t (>= 16 B on every target) so the typed
// casts are well-defined.
inline void
broadcastElement (char *dst, const char *src, size_t eSize, size_t count)
{
    switch (eSize)
    {
        case 1:
            std::memset (dst, *reinterpret_cast<const unsigned char*>(src),
                         count);
            break;
        case 2:
        {
            uint16_t v;
            std::memcpy (&v, src, sizeof v);
            std::fill_n (reinterpret_cast<uint16_t*>(dst), count, v);
            break;
        }
        case 4:
        {
            uint32_t v;
            std::memcpy (&v, src, sizeof v);
            std::fill_n (reinterpret_cast<uint32_t*>(dst), count, v);
            break;
        }
        case 8:
        {
            uint64_t v;
            std::memcpy (&v, src, sizeof v);
            std::fill_n (reinterpret_cast<uint64_t*>(dst), count, v);
            break;
        }
        default:
            for (size_t i = 0; i < count; ++i)
                std::memcpy (dst + i * eSize, src, eSize);
            break;
    }
}

} // namespace

size_t *SimdReg::zeroOffset = &zeroOffsetPlaceholder;


SimdReg::SimdReg (bool varying, size_t elementSize)
: _eSize(elementSize),
  _varying(varying),
  _oVarying(false),
  _dataOwned(true),
  _arenaOwned(false),
  _offsets(zeroOffset),
  _data (new char [ varying ? MAX_REG_SIZE * _eSize : _eSize]()),
  _ref(0),
  _arena(0)
{
}


SimdReg::SimdReg (bool varying, size_t elementSize, SimdArena &arena)
: SimdReg(varying, elementSize, arena, true)
{
}


SimdReg::SimdReg (bool varying, size_t elementSize, SimdArena &arena,
		  bool zeroInit)
: _eSize(elementSize),
  _varying(varying),
  _oVarying(false),
  _dataOwned(false),
  _arenaOwned(false),
  _offsets(zeroOffset),
  _data(0),
  _ref(0),
  _arena(&arena)
{
    const size_t nbytes = varying ? MAX_REG_SIZE * _eSize : _eSize;
    _data = arena.allocate(nbytes);

    if (!_data)
    {
	// Arena exhausted (malloc inside grow() failed). Fall back to
	// heap-owned storage so the op does not lose correctness.  Use
	// the zero-init new[] form whenever the caller asked for zero
	// (matches the scalar ctor); the uninit path skips zeroing
	// here too -- the caller has promised a full-write.
	_data = zeroInit ? new char [nbytes]() : new char [nbytes];
	_dataOwned = true;
	return;
    }

    if (zeroInit)
    {
	// Partial-write consumers (varying mask, references, merge-both-
	// paths ternary with neither-branch-taken lanes) rely on unwritten
	// lanes being zero so a later contiguous memcpy under a uniform
	// mask does not propagate uninitialized memory.
	std::memset (_data, 0, nbytes);
    }
}


SimdReg::SimdReg
   (SimdReg &r, 
    const SimdReg &indReg, 
    const SimdBoolMask &mask, 
    size_t arrayElementSize,
    size_t arraySize,
    size_t regSize,
    bool transferData /* = false */)

       : _eSize(r._eSize),
	 _varying(r._varying),
	 _oVarying(indReg.isVarying() || r._oVarying),
	 _dataOwned(transferData && r._data ? r._dataOwned : true),
	 _arenaOwned(false),
	 _offsets(new size_t [_oVarying ? MAX_REG_SIZE : 1]),
	 _data(transferData && r._data ? r._data : 0),
         _ref(transferData && r._data ? this : (r._ref ? r._ref : &r)),
         _arena(r._arena)
{
    if( _oVarying )
    {
	if( r._oVarying)
	{
	    for( int i = 0; i < (int)regSize; i++ )
	    {
	    if( !mask[i] ) continue;
		int ind = *(int *)(indReg[i]);
		if( ind < 0 || ind >= (int)arraySize )
		    throwIndexOutOfRange (ind, arraySize);

		if( mask[i] )
		    _offsets[i] = r._offsets[i] 
			+ ind*arrayElementSize;
	    }
	}
	else  // !r._oVarying
	{
	    for( int i = 0; i < (int)regSize; i++ )
	    {
	    if( !mask[i] ) continue;
		int ind = *(int *)(indReg[i]);
		if( ind < 0 || ind >= (int)arraySize )
		    throwIndexOutOfRange (ind, arraySize);

		if( mask[i] )
		    _offsets[i] = r._offsets[0] 
			+ ind*arrayElementSize;
	    }
	}
    }
    else // ! _oVarying
    {
	int ind = *(int*)(indReg[0]);
	if( ind < 0 || ind >= (int)arraySize )
	    throwIndexOutOfRange (ind, arraySize);
	
	_offsets[0] = r._offsets[0] + ind*arrayElementSize;
    }

    //
    // If we are tranfering the ownership, complete the transfer
    //
    if( transferData && r._data )
    {
	r._data = 0;
    }

}


SimdReg::SimdReg
   (SimdReg &r, 
    const SimdBoolMask &mask, 
    size_t offset,
    size_t regSize,
    bool transferData /* = false */)

       : _eSize(r._eSize),
	 _varying(r._varying),
	 _oVarying(r._oVarying),
	 _dataOwned(transferData && r._data ? r._dataOwned : true),
	 _arenaOwned(false),
	 _offsets(new size_t [_oVarying ? MAX_REG_SIZE : 1]),
	 _data(transferData && r._data ? r._data : 0),
         _ref(transferData && r._data ? this : (r._ref ? r._ref : &r)),
         _arena(r._arena)
{
    if( _oVarying )
    {
	for( int i = 0; i < (int)regSize; i++ )
	{
	    if( mask[i] )
		_offsets[i] = r._offsets[i] + offset;
	}
    }
    else // ! _oVarying
    {
	_offsets[0] = r._offsets[0] + offset;
    }

    //
    // If we are tranfering the ownership, complete the transfer
    //
    if( transferData && r._data )
    {
	r._data = 0;
    }
}




SimdReg::~SimdReg ()
{
    // If this is a reference register, clean up _offsets
    if( _offsets != zeroOffset)
	delete [] _offsets;

    if (_dataOwned)
	delete [] _data;
}


SimdReg *
SimdReg::createInArena (SimdArena &arena,
			bool varying,
			size_t elementSize,
			bool zeroInit)
{
    char *mem = arena.allocate (sizeof (SimdReg));
    if (!mem)
	return new SimdReg (varying, elementSize, arena, zeroInit);

    SimdReg *reg = new (mem) SimdReg (varying, elementSize, arena, zeroInit);
    reg->_arenaOwned = true;
    return reg;
}


void
SimdReg::destroy (SimdReg *reg)
{
    if (!reg)
	return;
    if (reg->_arenaOwned)
	reg->~SimdReg ();
    else
	delete reg;
}


void
SimdReg::reference(SimdReg &r,
		   bool transferData /* = false */)
{
    _eSize = r._eSize;
    _varying = r._varying;

    if( !_ref )
    {
	_offsets = new size_t [_oVarying ? MAX_REG_SIZE : 1];
    }
    else if(_oVarying != r._oVarying)
    {
	delete [] _offsets;
	_offsets = new size_t [_oVarying ? MAX_REG_SIZE : 1];
    }
    _oVarying = r._oVarying;

    if (_dataOwned)
	delete [] _data;

    //
    // If we are tranfering the ownership, and the original is not a reference
    //
    if( transferData && r._data )
    {
	_ref =  this;
	_data =  r._data;
	_dataOwned = r._dataOwned;
	r._data = 0;
    }
    else
    {
	_ref = r._ref ? r._ref : &r;
	_data = 0;
	_dataOwned = false;
    }

    if( _oVarying )
	memcpy(_offsets, r._offsets, MAX_REG_SIZE*sizeof(*_offsets));
    else 
	_offsets[0] = r._offsets[0];

}


void
SimdReg::setVarying (bool varying)
{
    if(_ref)
    {
	_ref->setVarying (varying);
    }
    else if (varying != _varying)
    {
	const size_t nbytes = varying ? MAX_REG_SIZE * _eSize : _eSize;
	char *data = 0;
	bool owned = true;
	if (_arena)
	{
	    data = _arena->allocate (nbytes);
	    if (data) owned = false;
	}
	if (!data)
	    data = new char [nbytes];

	if (varying)
	{
	    broadcastElement (data, _data, _eSize, MAX_REG_SIZE);
	}
	else
	{
	    memcpy (data, _data, _eSize);
	}

	if (_dataOwned)
	    delete [] _data;
 	_data = data;
	_dataOwned = owned;
	_varying = varying;
    }
}


void
SimdReg::setVaryingDiscardData (bool varying)
{
    if(_ref)
    {
	_ref->setVaryingDiscardData (varying);
    }
    else if (varying != _varying)
    {
	const size_t nbytes = varying ? MAX_REG_SIZE * _eSize : _eSize;
	char *data = 0;
	bool owned = true;
	if (_arena)
	{
	    data = _arena->allocate (nbytes);
	    if (data) owned = false;
	}
	if (!data)
	    data = new char [nbytes];

	if (_dataOwned)
	    delete [] _data;
 	_data = data;
	_dataOwned = owned;
	_varying = varying;
    }
}

} // namespace Ctl
