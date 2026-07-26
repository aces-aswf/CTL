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
//	class SimdXContext (SIMD execution-time context)
//
//-----------------------------------------------------------------------------

#include <CtlSimdXContext.h>
#include <CtlSimdReg.h>
#include <CtlSimdInst.h>
#include <CtlSimdAddr.h>
#include <CtlSimdInterpreter.h>
#include <CtlExc.h>
#include <cassert>

using namespace Iex;

namespace Ctl {


SimdStack::SimdStack (int size):
    _regPointers (new RegPointer[size]),
    _size (size),
    _sp (0),
    _fp (0)
{
    // empty
}


SimdStack::~SimdStack ()
{
    pop (_sp);
    delete [] _regPointers;
}


void
SimdStack::setFp (int fp)
{
    if (fp < 0 || fp > _sp)
	throw ArgExc ("Frame pointer out of range.");

    _fp = fp;
}


void
SimdStack::push (SimdReg *reg, RegOwnership ownership)
{
    if (_sp > _size)
    {
	if (ownership == TAKE_OWNERSHIP)
	    SimdReg::destroy (reg);

	throw StackOverflowExc ("Stack overflow.");
    }

    _regPointers[_sp].reg = reg;
    _regPointers[_sp].owner = (ownership == TAKE_OWNERSHIP);
    ++_sp;
}


void
SimdStack::pop (int n, bool giveUpOwnership /* = false */)
{
    if (n > _sp)
	throw StackUnderflowExc ("Stack underflow.");

    if (n < 0)
	throw StackLogicExc("Stack pop passed negative value.");

    while (n > 0)
    {
	--n;
	--_sp;

	if (_regPointers[_sp].owner && !giveUpOwnership)
	{
	    SimdReg *reg = _regPointers[_sp].reg;
	    if (reg->isArenaOwned())
		reg->~SimdReg();
	    else
		delete reg;
	}
    }
}


void
SimdStack::replaceTop (int n, SimdReg *reg, RegOwnership ownership)
{
    if (n > _sp)
    {
	if (ownership == TAKE_OWNERSHIP)
	    SimdReg::destroy (reg);
	throw StackUnderflowExc ("Stack underflow.");
    }

    if (n < 1)
    {
	if (ownership == TAKE_OWNERSHIP)
	    SimdReg::destroy (reg);
	throw StackLogicExc ("replaceTop requires n >= 1.");
    }

    // Destroy the top n owned regs, leaving _sp at the slot the new reg
    // will occupy.  Unrolled by hand (most callers pass n=1 or n=2).
    while (n > 0)
    {
	--n;
	--_sp;

	if (_regPointers[_sp].owner)
	{
	    SimdReg *r = _regPointers[_sp].reg;
	    if (r->isArenaOwned())
		r->~SimdReg();
	    else
		delete r;
	}
    }

    _regPointers[_sp].reg   = reg;
    _regPointers[_sp].owner = (ownership == TAKE_OWNERSHIP);
    ++_sp;
}


SimdReg &
SimdStack::regSpRelative (int offset) const
{
    int i = _sp + offset;

    if (i < 0 || i >= _sp)
	throw ArgExc ("Invalid stack-pointer relative address.");

    return *_regPointers[i].reg;
}


SimdReg &
SimdStack::regFpRelative (int offset) const
{
    int i = _fp + offset;

    if (i < 0 || i >= _sp)
	throw ArgExc ("Invalid frame-pointer relative address.");

    return *_regPointers[i].reg;
}


RegOwnership
SimdStack::ownerSpRelative (int offset) const
{
    int i = _sp + offset;

    if (i < 0 || i >= _sp)
	throw ArgExc ("Invalid frame-pointer relative address.");

    return _regPointers[i].owner ? TAKE_OWNERSHIP : REFERENCE_ONLY;
}


RegOwnership
SimdStack::ownerFpRelative (int offset) const
{
    int i = _fp + offset;

    if (i < 0 || i >= _sp)
	throw ArgExc ("Invalid frame-pointer relative address.");

    return _regPointers[i].owner ? TAKE_OWNERSHIP : REFERENCE_ONLY;
}

SimdXContext::SimdXContext (SimdInterpreter &interpreter):
    _interpreter (interpreter),
    _stack (1000),
    _arena (),
    _regSize (0),
    _returnMask (new SimdBoolMask(false)),
    _lineNumber (0),
    _module(0),
    _abortCount (0),
    _maxInstCount (0),
    _instCount (0),
    _fileName ("unknown")
{
    (*_returnMask)[0] = false;
}

SimdBoolMask *
SimdXContext::swapReturnMasks(SimdBoolMask *newMask)
{
    SimdBoolMask *tmp = _returnMask;
    _returnMask = newMask;
    return tmp;

}

SimdXContext::~SimdXContext ()
{
    delete _returnMask;
}


void	
SimdXContext::run (int regSize, const SimdInst *entryPoint)
{
    if( entryPoint == 0)  // if function is empy
	return;

    assert (regSize <= MAX_REG_SIZE && entryPoint != 0);

    _regSize = regSize;

    SimdBoolMask mask (false);
    mask[0] = true;

    _abortCount = _interpreter.abortCount();
    _maxInstCount = _interpreter.maxInstCount();
    _instCount = 0;

    // Per-callFunction arena lifetime: all arena-backed SimdRegs that
    // executePath allocates are popped (and thus destructed) before
    // executePath returns, either normally or via exception unwind.
    // Reset the bump tip here so the next run() reuses the same chunks.
    try
    {
	entryPoint->executePath (mask, *this);
    }
    catch (...)
    {
	_arena.reset();
	throw;
    }
    _arena.reset();
}


void
SimdXContext::countInstructionSlow ()
{
    if (_maxInstCount && _instCount > _maxInstCount)
      THROW(Ctl::MaxInstExc, "\nException Ctl::MaxInstExc thrown\n" <<
        "Maximum CTL instruction count _maxInstCount=" << _maxInstCount <<
        " exceeded, _instCount=" << _instCount << "\n" <<
        "Try increasing SimdInterpreter::Data->maxInstCount\n");

    if (_abortCount != _interpreter.abortCount())
	throw Ctl::AbortExc ("CTL program aborted.");
}


} // namespace Ctl
