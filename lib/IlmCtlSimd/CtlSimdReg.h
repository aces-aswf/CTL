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


#ifndef INCLUDED_CTL_SIMD_REG_H
#define INCLUDED_CTL_SIMD_REG_H

#include <CtlExc.h>
#include <CtlSimdArena.h>
#include <Iex.h>
#include <typeinfo>
#include <cstring>
#include <new>

//-----------------------------------------------------------------------------
//
//	Registers for the SIMD color transformation engine
//
//      Registers can contain a single element or MAX_REG_SIZE elements
//      if the register is varying.  SimdReg encapsulates the logic to
//      handle varying or non-varying-ness of the register.
//
//      A register is either a "value" register or a "reference" register.
//      A reference register points to values in another register.  SimdReg
//      also handles the logic to access elements of a register regardless
//      of whether it is a value or ref register.
//
//-----------------------------------------------------------------------------

namespace Ctl {

const int MAX_REG_SIZE = 8192;


namespace detail {

// Thread-local LIFO free-list of MAX_REG_SIZE bool buffers.  SimdBoolMask's
// varying storage comes from here instead of `new bool[]`: branch+loop+call
// sites construct/destruct masks in strict stack order, so the pool never
// grows past the CTL call-nesting depth (~10-20 on aces_combined).  Under
// Phase-B per-thread tile dispatch each worker has its own pool, so reuse
// stays cache-hot without any locking.  Beyond kCacheMax the pool falls
// through to heap alloc/free, bounding worst-case residency.
class BoolBufferPool
{
  public:
    static const int kCacheMax = 32;
    BoolBufferPool () : _count(0) {}
    ~BoolBufferPool ()
    {
	while (_count > 0) delete [] _buffers[--_count];
    }
    bool *acquire ()
    {
	if (_count > 0) return _buffers[--_count];
	return new bool[MAX_REG_SIZE];
    }
    void release (bool *p)
    {
	if (_count < kCacheMax) { _buffers[_count++] = p; return; }
	delete [] p;
    }
  private:
    bool *	_buffers[kCacheMax];
    int		_count;
};

BoolBufferPool &	boolBufferPool ();

} // namespace detail


class SimdBoolMask
{
  public:
    SimdBoolMask(const SimdBoolMask &copy, int copyLen);

    // Non-varying masks are a scalar bool; store that bool inline to avoid
    // a heap allocation per construction.  Varying masks pull an 8 KB
    // bool[MAX_REG_SIZE] buffer from a per-thread free-list (see
    // detail::BoolBufferPool) so repeated branch/call/loop traffic reuses
    // the same cache-hot region instead of round-tripping libc.
    explicit SimdBoolMask(bool varying)
	: _varying(varying),
	  _inlineData(false),
	  _data (varying ? detail::boolBufferPool().acquire() : &_inlineData) {}

    ~SimdBoolMask() { if (_varying) detail::boolBufferPool().release (_data); }

    void		setVarying (bool varying);
    bool                isVarying() const         {return _varying;}

    bool                &operator [] (int i) const
	                               {return _varying ? _data[i] : _data[0]; }
  private:
    bool		_varying;
    bool                _inlineData;   // storage for the non-varying case
    bool              * _data;

};


class SimdReg
{
  public:

    // Value constructor
    explicit SimdReg (bool varying, size_t elementSize);

    // Arena-backed value constructor -- for hot-path per-instruction regs.
    // _data is allocated from the supplied SimdArena and zero-filled
    // (matches the scalar-path ctor).  The arena outlives every reg
    // allocated from it (SimdArena::reset must not run while any
    // arena-backed reg is still reachable).
    explicit SimdReg (bool varying, size_t elementSize, SimdArena &arena);

    // Uninitialized variant -- the caller must full-write the buffer
    // before any read; any partial-write branch must zero unmasked
    // lanes itself.  Used by createInArena(..., zeroInit=false).
    SimdReg (bool varying, size_t elementSize, SimdArena &arena,
	     bool zeroInit);

    // Allocate both the SimdReg object itself and its data buffer from
    // `arena` -- saves a malloc+free pair per hot-path instruction. The
    // returned object must be destroyed by ~SimdReg() only; operator
    // delete must not run because the object storage is arena-owned.
    // Falls back to heap allocation if the arena is exhausted.
    // SimdStack::pop checks isArenaOwned() to decide which teardown path
    // to take.
    //
    // `zeroInit` (default true) matches the scalar ctor: every lane of
    // the varying buffer is zeroed before return, so partial-write
    // consumers (varying mask, non-contiguous inputs, merge-both-paths
    // with neither-branch-taken lanes) do not observe uninitialized
    // memory. Hot-path callers that provably overwrite every lane (the
    // uniform-mask contiguous Unary/Binary ALU branch) can pass false
    // to skip the memset; those callers MUST zero unmasked lanes
    // themselves on any fallback path that doesn't full-write.
    static SimdReg *createInArena (SimdArena &arena,
				   bool varying, size_t elementSize,
				   bool zeroInit = true);

    // Correct teardown for a reg regardless of where its object storage
    // lives. Use this in exception-cleanup paths; the stack teardown in
    // SimdStack::pop does the same thing inline.
    static void destroy (SimdReg *reg);

    //
    // Reference constructor for array indexing.
    // If transferData is true and original is not a reference
    // this register takes over ownership of the original data,
    // rendering the original register useless
    //
    explicit SimdReg(SimdReg &original, const SimdReg &indices, 
	    const SimdBoolMask &mask, 
	    size_t arrayElementSize, 
	    size_t arraySize,
	    size_t regSize,
	    bool transferData = false);

    // Reference constructor for struct member access
    explicit SimdReg(SimdReg &r, 
	    const SimdBoolMask &mask, 
	    size_t offset,
	    size_t regSize,
	    bool transferData = false);

    ~SimdReg ();

    //
    // Similar to the reference constructors above, but creates a reference
    // out of an existing simdReg.
    //
    void reference(SimdReg &r, bool transferData = false);


    void		setVarying (bool varying);
    void		setVaryingDiscardData (bool varying);
    bool                isVarying () const { return _varying || _oVarying; }
    size_t              elementSize () const { return _eSize; }
    bool		isReference () const {return _ref != 0;}
    bool		isArenaOwned () const { return _arenaOwned; }


    const char*	operator [] (int i) const 
    {
	return _ref ? _ref->_data + (_ref->_varying ? offset(i) + i *_eSize 
				                    : offset(i) )
                    :               (_varying       ? _data + i *_eSize 
                                                   : _data);
    }
    char*	operator [] (int i)
    {
	return _ref ? _ref->_data + (_ref->_varying ? offset(i) + i *_eSize 
				                    : offset(i) )
                    :               (_varying       ? _data + i *_eSize 
                                                   : _data);
    }

  protected:

    size_t              offset(int i) const
    { return _oVarying ? _offsets[i] : _offsets[0]; }

    //  A register has four ownership states:
    // 
    //  1) A register created from scratch:  
    //        _ref = 0, 
    //        _data = the register's data
    //        _offsets = 0
    //        _oVarying = false
    //        _Varying = true | false
    // 
    //  2) A reference register created by array index or struct member
    //  access, referencing a static register or temporary lower on the
    //  stack
    //        _ref = the referenced register
    //        _data = 0
    //        _offsets = offsets into reference register's data
    //        _oVarying = true | false
    //        _Varying = true | false
    //
    //  3) A reference register created by array index or struct member
    //  access, referencing a temporary register about to be poped from
    //  the stack (and deleted).
    //        _ref = this
    //        _data = data from the original register
    //        _offsets = offsets into this register's data
    //        _oVarying = true | false        
    //        _Varying = true | false
    //
    //  4) A register whose data ownership has been transfered to a
    //  a register of type 3).  This register can not be accessed
    //  and can only be deleted.
    //        _ref = any:  not valid
    //        _data = 0
    //        _offsets = 0 or not valid anymore - to be deleted
    //        _oVarying = true | false
    //        _Varying = true | false


    size_t              _eSize;        // Size of element in varying array
    bool		_varying;
    bool                _oVarying;     // Ref Register Offsets varying?
    bool                _dataOwned;    // true: delete[] _data on dtor;
                                       // false: _data is arena-backed or
                                       // transferred away
    bool                _arenaOwned;   // true: object storage is arena-backed;
                                       // SimdStack::pop must call ~SimdReg
                                       // directly and skip operator delete
    size_t*             _offsets;      // indexed offsets into a _data block
    char*               _data;
    SimdReg*            _ref;          // If a reference, points to original
    SimdArena*          _arena;        // non-null if this reg was built with
                                       // an arena; setVarying reallocations
                                       // route through it instead of malloc

  private:
    static size_t *zeroOffset;  // for reference registers,_offsets = zeroOffset
};


inline void
SimdBoolMask::setVarying(bool varying)
{
    if (varying == _varying)
	return;

    if (varying)
    {
	// non-varying to varying: acquire a pooled 8 KB buffer,
	// broadcast the inline value across MAX_REG_SIZE lanes.
	bool *data = detail::boolBufferPool().acquire();
	memset(data, _data[0], MAX_REG_SIZE);
	_data = data;
    }
    else
    {
	// varying to non-varying: preserve first lane, return the buffer
	// to the pool, point at inline storage.
	_inlineData = _data[0];
	detail::boolBufferPool().release (_data);
	_data = &_inlineData;
    }
    _varying = varying;
}


inline
SimdBoolMask::SimdBoolMask(const SimdBoolMask &copy, int copyLen)
    : _varying(copy.isVarying()),
      _inlineData(copy.isVarying() ? false : copy._data[0]),
      _data (copy.isVarying() ? detail::boolBufferPool().acquire()
				: &_inlineData)
{
    if (_varying)
	memcpy(_data, copy._data, copyLen*sizeof(bool));
}

} // namespace Ctl

#endif
