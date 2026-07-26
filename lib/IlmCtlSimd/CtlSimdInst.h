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


#ifndef INCLUDED_CTL_SIMD_INST_H
#define INCLUDED_CTL_SIMD_INST_H

//-----------------------------------------------------------------------------
//
//	Instructions for the SIMD color transformation engine.
//
//-----------------------------------------------------------------------------

#include <CtlSimdReg.h>
#include <CtlSimdAddr.h>
#include <CtlSyntaxTree.h>
#include <iostream>
#include <iomanip>
#include <typeinfo>
#include <string>

//
// C99's restrict has no standard C++ spelling.  GCC and Clang expose it as
// __restrict__, MSVC as __restrict; anything else gets a no-op, since the
// qualifier is strictly an optimization hint.
//

#ifndef CTL_RESTRICT
    #if defined(_MSC_VER)
        #define CTL_RESTRICT __restrict
    #elif defined(__GNUC__) || defined(__clang__)
        #define CTL_RESTRICT __restrict__
    #else
        #define CTL_RESTRICT
    #endif
#endif


namespace Ctl {

class SimdInst
{
  public:

    // Direct-dispatch thunk: avoids vtable lookup in the hot executePath loop.
    // Each concrete subclass stores a pointer to simdExecThunk<Self> at
    // construction time; executePath calls through it instead of the virtual
    // execute().
    typedef void (*ExecThunk)(const SimdInst *self,
			      SimdBoolMask &mask,
			      SimdXContext &xcontext);

    SimdInst (ExecThunk thunk, int lineNumber);
    virtual ~SimdInst ();

    void		setNextInPath (const SimdInst *nextInPath);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const = 0;

    void		executePath (SimdBoolMask &mask,
				     SimdXContext &xcontext) const;

    virtual void	print (int indent) const = 0;
    void		printPath (int indent) const;

    int                 lineNumber() const { return _lineNumber; }

  private:

    const ExecThunk     _execThunk;
    const int           _lineNumber;
    const SimdInst *	_nextInPath;
};


// Free thunk template: calls Derived::execute through a static_cast.  The
// compiler sees a direct (non-virtual) call here, so the vtable lookup is
// gone from the hot dispatch loop.
template <class Derived>
void simdExecThunk (const SimdInst *self,
		    SimdBoolMask &mask,
		    SimdXContext &xcontext)
{
    static_cast<const Derived *>(self)->Derived::execute (mask, xcontext);
}



//
// When used in binary operators, the branch inst paths leave a register on
// the stack.  In this case mergeResults should be set to true.  
// If the conditional is varying, both the truePath and falsePath 
// may be executed - mergeResults will merge the top two registers on the
// stack at the concolusion of the instruction.
class SimdBranchInst: public SimdInst
{
  public:

    SimdBranchInst (const SimdInst *truePath,
		    const SimdInst *falsePath,
		    bool mergeResults,
		    int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    const SimdInst *		_truePath;
    const SimdInst *		_falsePath;
    const bool                  _mergeResults;
};


class SimdLoopInst: public SimdInst
{
  public:

    SimdLoopInst (const SimdInst *conditionPath,
		  const SimdInst *loopPath,
		  int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    const SimdInst *		_conditionPath;
    const SimdInst *		_loopPath;
};


class SimdCallInst: public SimdInst
{
  public:

    SimdCallInst (const SimdInst *callPath,
		  int numParameters,
		  int lineNumber);

    void		setCallPath (const SimdInst *callPath);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    const SimdInst *	_callPath;
    int                 _numParameters;
};


class SimdCCallInst: public SimdInst
{
  public:

    SimdCCallInst (SimdCFunc func, 
		   int numParameters,
		   int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    SimdCFunc		_func;
    int                 _numParameters;
};


class SimdReturnInst: public SimdInst
{
  public:

    SimdReturnInst (int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;
};


template <class In, class Out, template <class I, class O> class Op>
class SimdUnaryOpInst: public SimdInst
{
  public:

    SimdUnaryOpInst (int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

};


template <class In1, class In2, class Out,
	  template <class I1, class I2, class O> class Op>
class SimdBinaryOpInst: public SimdInst
{
  public:

    SimdBinaryOpInst (int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

};


class SimdAssignInst: public SimdInst
{
  public:

    SimdAssignInst (size_t opTypeSize, int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    size_t  _opTypeSize;
};



class SimdInitializeInst: public SimdInst
{
  public:

    SimdInitializeInst (const SizeVector &sizes, 
			const SizeVector &offsets,
			int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    SizeVector _sizes;
    SizeVector _offsets;
};


class SimdAssignArrayInst: public SimdInst
{
  public:

    SimdAssignArrayInst (int size,  size_t opTypeSize, int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    int _size;
    size_t _opTypeSize;
};


class SimdIndexArrayInst: public SimdInst
{
  public:

    SimdIndexArrayInst (size_t arrayElementSize, 
			int lineNumber, 
			size_t arraySize);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    size_t _arrayElementSize;
    size_t _arraySize;
};

class SimdIndexVSArrayInst: public SimdInst
{
  public:

    SimdIndexVSArrayInst (size_t arrayElementSize,
			  const SimdDataAddrPtr &arrayElementSizePtr, 
			  size_t arraySize,
			  const SimdDataAddrPtr &arraySizePtr,
			  int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    const size_t _arrayElementSize;
    const SimdDataAddrPtr _arrayElementSizePtr;

    const size_t _arraySize;
    const SimdDataAddrPtr _arraySizePtr;
};


class SimdAccessMemberInst: public SimdInst
{
  public:

    SimdAccessMemberInst (size_t offset, int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    size_t _offset;
};


template <class T>
class SimdPushLiteralInst: public SimdInst
{
  public:

    SimdPushLiteralInst (T value, int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    T			_value;
};


class SimdPushStringLiteralInst: public SimdInst
{
  public:

    SimdPushStringLiteralInst (const std::string &value, int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    std::string		_value;
};


class SimdPushRefInst: public SimdInst
{
  public:

    SimdPushRefInst (const SimdDataAddrPtr &in, int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    SimdDataAddrPtr	_in;
};


class SimdPushPlaceholderInst: public SimdInst
{
  public:

    SimdPushPlaceholderInst (size_t eSize, int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    int _eSize;
    
};


class SimdPopInst: public SimdInst
{
  public:

    SimdPopInst (int numRegs, int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    int			_numRegs;
};


class SimdFileNameInst: public SimdInst
{
  public:

    SimdFileNameInst (const std::string &fileName, int lineNumber);

    virtual void	execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const;

    virtual void	print (int indent) const;

  private:

    std::string		_fileName;
};

//---------------
// Implementation
//---------------

template <class In, class Out, template <class I, class O> class Op>
SimdUnaryOpInst<In, Out, Op>::SimdUnaryOpInst (int lineNumber)
    : SimdInst(&simdExecThunk<SimdUnaryOpInst<In, Out, Op> >, lineNumber)
{
    // empty
}


template <class In, class Out, template <class I, class O> class Op>
void
SimdUnaryOpInst<In, Out, Op>::execute (SimdBoolMask &mask,
				       SimdXContext &xcontext) const
{
    const SimdReg &in = xcontext.stack().regSpRelative(-1);
    const bool outVarying = in.isVarying() || mask.isVarying();
    // Skip the arena ctor's memset: every branch below either full-writes
    // every lane or explicitly zeroes the unmasked lanes itself.
    SimdReg *out = SimdReg::createInArena (xcontext.arena(),
					   outVarying,
					   sizeof(Out),
					   /*zeroInit=*/false);

    try
    {
	if (outVarying)
	{
	    if (!mask.isVarying() && !in.isReference())
	    {
		//
		// The contents of in are contiguous in
		// memory and mask is uniform.  Full-write,
		// no zero needed.
		//
		// in and out come from distinct arena allocations
		// and cannot alias; CTL_RESTRICT lets clang hoist the
		// aliasing check and vectorize the loop on NEON/SSE.

		const In * CTL_RESTRICT inPtr  = (In *)in[0];
		Out * CTL_RESTRICT       outPtr = (Out *)(*out)[0];
		Out *                    outEnd = outPtr + xcontext.regSize();

		while (outPtr < outEnd)
		    Op<In,Out>::execute (*(inPtr++), *(outPtr++));
	    }
	    else
	    {
		//
		// Mask is not uniform or the contents of in
		// may not be contiguous in memory.  Zero
		// unmasked lanes so later uniform-mask reads
		// do not observe uninitialized memory.
		//

		for (int i = xcontext.regSize(); --i >= 0;)
		    if (mask[i])
			Op<In,Out>::execute (*(In*)(in[i]),
					     (*(Out*)(*out)[i]));
		    else
			*(Out *)((*out)[i]) = Out();
	    }
	}
	else
	{
	    Op<In,Out>::execute (*(In*)(in[0]), *(Out*)(*out)[0]);
	}
    }
    catch (...)
    {
	SimdReg::destroy (out);
	throw;
    }

    xcontext.stack().replaceTop (1, out, TAKE_OWNERSHIP);
}


template <class In, class Out, template <class I, class O> class Op>
void
SimdUnaryOpInst<In, Out, Op>::print (int indent) const
{
    std::cout << std::setw (indent) << "" <<
		 "unary op " <<
		 typeid(Op<In,Out>).name() << std::endl;
}


template <class In1, class In2, class Out,
	  template <class I1, class I2, class O> class Op>
SimdBinaryOpInst<In1, In2, Out, Op>::SimdBinaryOpInst (int lineNumber)
    : SimdInst(&simdExecThunk<SimdBinaryOpInst<In1, In2, Out, Op> >, lineNumber)
{
    // empty
}


template <class In1, class In2, class Out,
	  template <class I1, class I2, class O> class Op>
void
SimdBinaryOpInst<In1, In2, Out, Op>::execute (SimdBoolMask &mask,
					      SimdXContext &xcontext) const
{
    const SimdReg &in1 = xcontext.stack().regSpRelative(-2);
    const SimdReg &in2 = xcontext.stack().regSpRelative(-1);

    const bool outVarying =
	in1.isVarying() || in2.isVarying() || mask.isVarying();
    // Skip the arena ctor's memset: every branch below either full-writes
    // every lane or explicitly zeroes the unmasked lanes itself.
    SimdReg *out = SimdReg::createInArena (
	xcontext.arena(),
	outVarying,
	sizeof(Out),
	/*zeroInit=*/false);

    try
    {
	if (outVarying)
	{
	    if (!mask.isVarying() && !in1.isReference() && !in2.isReference())
	    {
		//
		// Mask is uniform and the contents of input registers
		// in1 and in2 are contiguous in memory.  At least one
		// of the input registers is varying.
		//
		// in1, in2 and out come from distinct arena allocations
		// and cannot alias; CTL_RESTRICT lets clang hoist the
		// aliasing check and vectorize the loop on NEON/SSE.

		const In1 * CTL_RESTRICT in1Ptr = (In1 *)in1[0];
		const In2 * CTL_RESTRICT in2Ptr = (In2 *)in2[0];
		Out * CTL_RESTRICT       outPtr = (Out *)(*out)[0];
		Out *                    outEnd = outPtr + xcontext.regSize();

		if (in1.isVarying() && in2.isVarying())
		{
		    while (outPtr < outEnd)
			Op<In1,In2,Out>::execute (*(in1Ptr++),
						  *(in2Ptr++),
						  *(outPtr++));
		}
		else if (in1.isVarying())
		{
		    while (outPtr < outEnd)
			Op<In1,In2,Out>::execute (*(in1Ptr++),
						  *(in2Ptr),
						  *(outPtr++));
		}
		else
		{
		    while (outPtr < outEnd)
			Op<In1,In2,Out>::execute (*(in1Ptr),
						  *(in2Ptr++),
						  *(outPtr++));
		}
	    }
	    else
	    {
		//
		// Mask is varying or the contents of the input
		// registers may not be contiguous in memory.
		// Zero unmasked lanes so later uniform-mask reads
		// do not observe uninitialized memory.
		//

		for (int i = xcontext.regSize(); --i >= 0;)
		    if (mask[i])
			Op<In1,In2,Out>::execute (*(In1*)(in1[i]),
						  *(In2*)(in2[i]),
						  *(Out*)((*out)[i]));
		    else
			*(Out *)((*out)[i]) = Out();
	    }
	}
	else
	{
	    Op<In1,In2,Out>::execute (*(In1*)(in1[0]),
				      *(In2*)(in2[0]),
				      *(Out*)((*out)[0]));
	}
    }
    catch (...)
    {
	SimdReg::destroy (out);
	throw;
    }

    xcontext.stack().replaceTop (2, out, TAKE_OWNERSHIP);
}


template <class In1, class In2, class Out,
	  template <class I1, class I2, class O> class Op>
void
SimdBinaryOpInst<In1, In2, Out, Op>::print (int indent) const
{
    std::cout << std::setw (indent) << "" <<
		 "binary op " <<
		 typeid(Op<In1,In2,Out>).name() << std::endl;
}



template <class T>
SimdPushLiteralInst<T>::SimdPushLiteralInst (T value, int lineNumber)
    : SimdInst(&simdExecThunk<SimdPushLiteralInst<T> >, lineNumber),
      _value (value)
{
    // empty
}


template <class T>
void
SimdPushLiteralInst<T>::execute (SimdBoolMask &mask,
				 SimdXContext &xcontext) const
{
    // Uniform single-element reg -- memcpy below full-writes the value.
    SimdReg *out = SimdReg::createInArena (xcontext.arena(),
					   /*varying=*/false,
					   sizeof(T),
					   /*zeroInit=*/false);
    xcontext.stack().push (out, TAKE_OWNERSHIP);
    memcpy((*out)[0],  &_value, sizeof(_value));
}


template <class T>
void
SimdPushLiteralInst<T>::print (int indent) const
{
    std::cout << std::setw (indent) << "" << "push literal " 
	      << _value << " " <<  typeid(T).name() << std::endl;
}


} // namespace Ctl

#endif
