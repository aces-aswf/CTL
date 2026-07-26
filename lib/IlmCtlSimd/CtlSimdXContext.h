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


#ifndef INCLUDED_CTL_SIMD_X_CONTEXT_H
#define INCLUDED_CTL_SIMD_X_CONTEXT_H

//-----------------------------------------------------------------------------
//
//	class SimdXContext (SIMD execution-time context)
//	
//	The SIMD (single instruction / multiple data) engine
//	that executes compiled color transformation programs.
//
//-----------------------------------------------------------------------------

#include <vector>
#include <CtlSimdModule.h>
#include <typeinfo>
#include <CtlSimdReg.h>
#include <CtlSimdArena.h>
#include <string>

namespace Ctl {

class SimdInst;
class SimdInterpreter;


enum RegOwnership
{
    TAKE_OWNERSHIP,
    REFERENCE_ONLY
};


class SimdStack
{
  public:

     SimdStack (int size);
    ~SimdStack ();

    void	push (SimdReg *reg, RegOwnership ownership);
    void	pop (int n, bool giveUpOwnership = false);

    // Destroy the top n owned regs (same as pop(n)) and write reg in
    // place of the new TOS, all with a single _sp adjustment.  Equivalent
    // to pop(n); push(reg, ownership) but avoids the extra _sp bump and
    // bounds check on the SimdUnaryOpInst / SimdBinaryOpInst hot path.
    void	replaceTop (int n, SimdReg *reg, RegOwnership ownership);

    int		sp () const		{return _sp;}
    int		fp () const		{return _fp;}
    void	setFp (int fp);

    SimdReg &	regSpRelative (int offset) const;
    SimdReg &	regFpRelative (int offset) const;

    RegOwnership ownerSpRelative (int offset) const;
    RegOwnership ownerFpRelative (int offset) const;

  private:

    struct RegPointer
    {
	SimdReg *	reg;
	bool		owner;
    };

    RegPointer *	_regPointers;
    int			_size;
    int			_sp;
    int			_fp;

};


class SimdXContext
{
  public:

    SimdXContext (SimdInterpreter &interpreter);
    virtual ~SimdXContext ();

    void		run (int regSize, const SimdInst *entryPoint);

    SimdBoolMask &	returnMask () const		{return *_returnMask;}
    SimdBoolMask *      swapReturnMasks(SimdBoolMask *newMask);

    SimdStack &		stack ()			{return _stack;}
    int			regSize () const		{return _regSize;}
    SimdArena &		arena ()			{return _arena;}

    int			lineNumber () const		{return _lineNumber;}
    void		setLineNumber (int ln)   	{_lineNumber = ln;}
    
    const std::string & fileName () const		{return _fileName;}
    void		setFileName (const std::string &fn) {_fileName = fn;}

    SimdModule*		module()			{return _module;}
    void		setModule(SimdModule* m)	{_module = m;}

    // Hot-path: bumps _instCount.  Every 8192 increments, falls through to
    // countInstructionSlow to check for max-instruction-count / abort-count.
    // Inlined so the dispatch loop avoids a cross-TU function call on every
    // instruction.
    void		countInstruction ()
			{
			    if ((++_instCount & 0x01fff) == 0)
				countInstructionSlow();
			}
    void		countInstructionSlow ();

    SimdInterpreter &interpreter(void) const { return _interpreter; };

  private:

    SimdInterpreter &	_interpreter;

    SimdStack		_stack;
    SimdArena		_arena;
    int			_regSize;
    SimdBoolMask *	_returnMask;

    int			_lineNumber;
    SimdModule *	_module;

    unsigned long	_abortCount;
    unsigned long	_maxInstCount;
    unsigned long	_instCount;
    std::string		_fileName;
};


//
// A helper class to construct and destruct stack frames.
// Used by class SimdCallInst, below.
//

class StackFrame
{
  public:

    StackFrame (SimdXContext &xcontext):
	_xcontext (xcontext),
	_stack(xcontext.stack()),
	_savedSp (_stack.sp()),
	_savedFp (_stack.fp()),
	_freshMask (false),
	_savedRMask (0)
    {
	_stack.setFp (_stack.sp());

	_freshMask[0] = 0;
	_savedRMask = _xcontext.swapReturnMasks(&_freshMask);
    }

    ~StackFrame ()
    {
	_stack.pop (_stack.sp() - _savedSp);
	_stack.setFp (_savedFp);

	_xcontext.swapReturnMasks(_savedRMask);
    }

  private:

    SimdXContext & _xcontext;
    SimdStack    & _stack;
    int		   _savedSp;
    int		   _savedFp;
    SimdBoolMask   _freshMask;   // embedded; no heap alloc on ctor/dtor
    SimdBoolMask * _savedRMask;  // borrowed: caller's returnMask
};



} // namespace Ctl

#endif
