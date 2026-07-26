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
//	The syntax tree for the SIMD implementation of the
//	color transformation language.
//
//-----------------------------------------------------------------------------

#include <CtlSimdSyntaxTree.h>
#include <CtlSimdModule.h>
#include <CtlSimdLContext.h>
#include <CtlSimdInst.h>
#include <CtlSymbolTable.h>
#include <CtlSimdType.h>
#include <cassert>
#include <vector>
#include <CtlSimdOp.h>
#include <CtlSimdStdLibTemplates.h>
#include <cmath>


namespace Ctl {

// SimdCFunc-shaped implementations of seven operations that CTL
// programs commonly write out by hand: min, max, clip,
// radians_to_degrees, degrees_to_radians, copysign and wrap_to_360.
// Calling one of these through the interpreter costs far more than the
// one or two arithmetic operations it performs, so a definition that
// computes one of them is compiled into a direct call to the matching
// implementation below.
//
// Which definitions qualify is decided by reading the function's body,
// not its name -- see recognizeHelperBody further down.  CTL reserves
// none of these names, so a program may define, say, its own clip()
// that does something else; that definition is left alone.  Each body
// below therefore has to stay bit-identical to the CTL body it stands
// in for, and unittest/IlmCtl/testInlineHelpers pins that.
//
// Restricted to pure arithmetic so helpers that internally call
// pow/log/exp/sqrt keep their SLEEF / Accelerate batched-math path
// through the existing simdFunc{1,2}Arg templates.

DEFINE_SIMD_FUNC_2_ARG (InlineMinFloat,
                        (a1 < a2 ? a1 : a2),
                        float, float, float);
DEFINE_SIMD_FUNC_2_ARG (InlineMaxFloat,
                        (a1 > a2 ? a1 : a2),
                        float, float, float);
DEFINE_SIMD_FUNC_1_ARG (InlineClipFloat,
                        (a1 < 1.0f ? a1 : 1.0f),
                        float, float);
DEFINE_SIMD_FUNC_1_ARG (InlineRadToDegFloat,
                        (a1 * 180.0f / 3.14159265358979323846f),
                        float, float);
DEFINE_SIMD_FUNC_1_ARG (InlineDegToRadFloat,
                        (a1 / 180.0f * 3.14159265358979323846f),
                        float, float);

// Match CTL `sign(y)*fabs(x)` exactly; sign(0) is 0, so copysign(x,0)=0.
// (std::copysign would return +x for y=0 -- different semantics.)
DEFINE_SIMD_FUNC_2_ARG (InlineCopysignFloat,
                        ((a2 < 0.0f ? -1.0f : (a2 > 0.0f ? 1.0f : 0.0f))
                         * std::fabs (a1)),
                        float, float, float);

// CTL: float y = fmod(hue, 360.); if (y < 0.) y += 360.; return y;
DEFINE_SIMD_FUNC_1_ARG (InlineWrapTo360Float,
                        ([](float h) {
                            float y = std::fmod (h, 360.0f);
                            return y < 0.0f ? y + 360.0f : y;
                        })(a1),
                        float, float);

} // namespace Ctl

using namespace std;

#if 0
    #include <iostream>
    #define debug(x) (cout << x << endl)
    #define debug_only(x) x
#else
    #define debug(x)
    #define debug_only(x)
#endif

namespace Ctl {
namespace {

SimdInst *
generateCodeForPath (ExprNodePtr node, SimdLContext &slcontext)
{
    slcontext.newPath();
    node->generateCode (slcontext);
    return slcontext.currentPath().firstInst;
}



SimdInst *
generateCodeForPath (StatementNodePtr node, SimdLContext &slcontext, 
		     const SimdLContext::Path *insertPath = 0,
		     const vector<DataTypePtr> *locals = 0)
{
    if( !node )
    {
	return 0;
    }


    slcontext.newPath();

    if( locals )
    {
	// Insert instructions to hold space for local variables on stack
	for(vector<DataTypePtr>::const_iterator type = locals->begin();
	    type != locals->end(); 
	    type++)
	{
	    (*type)->newAutomaticVariable(node, slcontext);
	}
    }
    
    if(insertPath && insertPath->firstInst)
    {
	slcontext.appendPath(*insertPath);
    }

    while (node)
    {
	node->generateCode (slcontext);
	node = node->next;
    }

    return slcontext.currentPath().firstInst;
}

} //namespace


SimdModuleNode::SimdModuleNode
    (int lineNumber,
     const StatementNodePtr &constants,
     const FunctionNodePtr &functions)
:
    ModuleNode (lineNumber, constants, functions)
{
    // empty
}


void
SimdModuleNode::generateCode (LContext &lcontext)
{
    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);

    //
    // Generate code for initialization of static constants.
    //

    if (constants)
    {
	debug ("generating module initialization code");

	SimdInst *firstInitInst = generateCodeForPath (constants, slcontext);
	slcontext.simdModule()->setFirstInitInst (firstInitInst);

	debug_only (if (firstInitInst) firstInitInst->printPath (1));
    }

    //
    // Generate code for functions.
    //

    FunctionNodePtr function = functions;

    while (function)
    {
	function->generateCode (lcontext);
	function = function->next;
    }

    //
    // Fixup function call instructions whose callPath was
    // not known when the instructions were generated (see
    // SimdCallNode::generateCode().
    // 

    slcontext.fixCalls();
}


SimdFunctionNode::SimdFunctionNode
    (int lineNumber,
     const std::string &name,
     const SymbolInfoPtr &info,
     const StatementNodePtr &body,
     const std::vector<DataTypePtr> locals)
:
    FunctionNode (lineNumber, name, info, body)
{
    _locals = locals;


}


void
SimdFunctionNode::generateESizeCode(SimdLContext &slcontext, 
				    SimdArrayTypePtr arrayType)
{
    if( arrayType && arrayType->unknownElementSize())
    {
	// push unknownESize
	slcontext.addInst 
	    (new SimdPushRefInst (arrayType->unknownElementSize(), lineNumber));

	// we know sub-type is an array
	SimdArrayTypePtr subArrayType = arrayType->elementType();
	generateESizeCode(slcontext, subArrayType);

	// push sub elementSize : reference to computed or literal
	if( subArrayType->unknownElementSize() )
	    slcontext.addInst 
		(new SimdPushRefInst(subArrayType->unknownElementSize(), 
				     lineNumber));
	else
	    slcontext.addInst 
		(new SimdPushLiteralInst <int> (subArrayType->elementSize(), 
						lineNumber));

	// push sub size : reference to computed or literal
	if( subArrayType->unknownSize() )
	    slcontext.addInst 
		(new SimdPushRefInst (subArrayType->unknownSize(), lineNumber));
	else
	    slcontext.addInst 
		(new SimdPushLiteralInst <int> (subArrayType->size(), 
						lineNumber));
	// multiply
	slcontext.addInst
	    (new SimdBinaryOpInst <int, int, int, TimesOp> (lineNumber));

	// assign
	slcontext.addInst (new SimdAssignInst (sizeof(int), lineNumber));
    }
}


//
// Decide whether a function definition computes one of the operations
// that has a direct implementation here.  Defined further down, next to
// the implementations themselves and the body patterns it matches.
//

static int
recognizeHelperBody (const FunctionTypePtr &ft, const StatementNodePtr &body);


void
SimdFunctionNode::generateCode (LContext &lcontext)
{

    debug ("generating code for function " << name);

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);

    SimdFunctionTypePtr functionType = info->functionType();

    // step through arguments, if array pointer and unknown element size
    const ParamVector params = functionType->parameters();
    slcontext.newPath();
    slcontext.addInst (new SimdFileNameInst(lcontext.fileName(), lineNumber));
    for(int i = params.size()-1; i >= 0; i--)
    {
	generateESizeCode(slcontext, params[i].type.cast<SimdArrayType>());
    }
    SimdLContext::Path path = slcontext.currentPath();

    //
    // Record whether this definition computes one of the operations we
    // have a direct implementation for.  Done here, once, rather than
    // at each call site: the body is in hand, and call sites can then
    // read the answer off the symbol.
    //
    // Must happen before the body is compiled below, so that a helper
    // defined earlier in the file is already marked when a later one
    // that calls it is checked (clip needs min, copysign needs sign).
    //

    info->setCodeGenHint (recognizeHelperBody (functionType, body));

    SimdInst *firstBodyInst =
	generateCodeForPath (body, slcontext, &path, &_locals);

    info->setAddr (new SimdInstAddr (firstBodyInst));
    debug_only (if (firstBodyInst) firstBodyInst->printPath (1));
}


SimdVariableNode::SimdVariableNode
    (int lineNumber,
     const std::string &name,
     const SymbolInfoPtr &info,
     const ExprNodePtr &initialValue,
     bool assignInitialValue)
:
    VariableNode (lineNumber, name, info, initialValue, assignInitialValue)
{
    // empty
}


void
SimdVariableNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for a variable declaration.
    //
    // If the variable has no initial value, then there is nothing to do.
    //
    // If the variable does have an initial value, then there are two cases:
    //
    //     If the initial value is assigned to the variable,
    //	   then generate code to
    //         - push a reference to the variable onto the stack
    //         - evaluate the expression that computes the initial value
    //         - cast the value to the type of the variable
    //         - assign initial value to the variable.
    //
    //     Otherwise the variable is initialized by a side-effect of
    //     the initialization expression.  In this case we generate
    //     code to
    //         - evaluate the expression that computes the initial value
    //         - pop the expression's value off the stack (unless the
    //           expression's value is of type void).
    // 

    if (initialValue)
    {
	    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);
	
	    SimdDataAddrPtr dataPtr = info->addr().cast<SimdDataAddr>();
	    SimdValueNodePtr valuePtr = initialValue.cast<SimdValueNode>();

	    if (assignInitialValue)
	    {
	        //
	        // Initial value is assigned to the variable.
	        //

	        if( valuePtr && valuePtr->type && dataPtr  && dataPtr->reg())
	        {
				// The variable is static, and its value is a literal
				// or a collection of literals (for structs and arrays).
				// We can copy the initial value directly into the
				// variable instead of generating code to assign the
				// value.
		
		        // get sizes & offsets of elements
		        SizeVector sizes;
		        SizeVector offsets;
		        DataTypePtr dataType = valuePtr->type;
		        dataType->coreSizes(0, sizes, offsets);

		        ExprNodeVector &elements = valuePtr->elements;
		        int numElements = elements.size();
		        assert((int)sizes.size() == numElements 
		               && (int)offsets.size() == numElements);
		        assert(!dataPtr->reg()->isVarying());
		
		        char* dest = (*dataPtr->reg())[0];
		
		        int eIndex = 0;

                try {
                    valuePtr->castAndCopyRec(lcontext, dataType, eIndex,
                                             dest, sizes, offsets);
                } catch (Iex::TypeExc) {
                    // HACK: this should be avoided ahead of time
                    slcontext.addInst (new SimdPushRefInst (info->addr(), lineNumber));
                    initialValue->generateCode (lcontext);
                    info->type()->generateCastFrom (initialValue, lcontext);
                    info->type()->generateCode (this, lcontext);
                }
	        }
	        else
	        {
		        slcontext.addInst (new SimdPushRefInst (info->addr(), lineNumber));
		        initialValue->generateCode (lcontext);
		        info->type()->generateCastFrom (initialValue, lcontext);
		        info->type()->generateCode (this, lcontext);
	        }
	    }
	    else
	    {
	        //
	        // Variable is initialized via side-effect.
	        //

	        initialValue->generateCode (lcontext);

	        const SimdCallNode *call = 
		    dynamic_cast <const SimdCallNode*>(initialValue.pointer());
	        
	        RcPtr<SimdVoidType> pv(new SimdVoidType());

	        if (call == 0 || !call->returnsType (pv))
	        {
		        slcontext.addInst (new SimdPopInst (1, lineNumber));
	        }
	    }
    }
}




SimdAssignmentNode::SimdAssignmentNode
    (int lineNumber,
     const ExprNodePtr &lhs,
     const ExprNodePtr &rhs)
:
    AssignmentNode (lineNumber, lhs, rhs)
{
    // empty
}


void
SimdAssignmentNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for an assigment statement:
    //
    // Evaluate the left-hand-side and right-hand-side expressions,
    // cast the right-hand-side value to the type of the left-hand-side,
    // assign the value to the left-hand-side.
    // 

    lhs->generateCode (lcontext);
    rhs->generateCode (lcontext);
    lhs->type->generateCastFrom (rhs, lcontext);
    lhs->type->generateCode (this, lcontext);
}


SimdExprStatementNode::SimdExprStatementNode
    (int lineNumber, const ExprNodePtr &expr)
:
    ExprStatementNode (lineNumber, expr)
{
    // empty
}


void
SimdExprStatementNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for an expression statement:
    //
    // Evaluate the expression.
    // Pop its value off the top of the stack (unless it is a void
    // function).
    //

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);

    expr->generateCode (lcontext);

    const SimdCallNode *call = 
	dynamic_cast <const SimdCallNode*>(expr.pointer()) ;
    
    RcPtr<SimdVoidType> pv(new SimdVoidType());
    if( call == 0 || !call->returnsType(pv))
    {
	slcontext.addInst (new SimdPopInst (1, lineNumber));
    }
}


SimdIfNode::SimdIfNode
    (int lineNumber,
     const ExprNodePtr &condition,
     const StatementNodePtr &truePath,
     const StatementNodePtr &falsePath)
:
    IfNode (lineNumber, condition, truePath, falsePath)
{
    // empty
}


void
SimdIfNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for an if statement:
    //

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);

    //
    // Append code for the expression that computes the condition to
    // the "main path", that is, to the path that contains the if statement. 
    // Cast value of the expression to type bool.
    //

    condition->generateCode (lcontext);
    SimdLContext::Path mainInstPath = slcontext.currentPath();

    BoolTypePtr boolType = lcontext.newBoolType ();
    boolType->generateCastFrom (condition, lcontext);

    //
    // Generate code for the true and false paths.
    //

    const SimdInst *firstTrueInst = generateCodeForPath (truePath, slcontext);
    const SimdInst *firstFalseInst = generateCodeForPath (falsePath, slcontext);

    //
    // Append a branch instruction to the main path.
    //

    slcontext.setCurrentPath (mainInstPath);
    slcontext.addInst (new SimdBranchInst (firstTrueInst, firstFalseInst, 
					   false,  lineNumber));
}


SimdReturnNode::SimdReturnNode
    (int lineNumber,
     const SymbolInfoPtr &info,
     const ExprNodePtr &returnedValue)
:
    ReturnNode (lineNumber, info, returnedValue)
{
    // empty
}


void
SimdReturnNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for a return statement.
    //

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);

    if (returnedValue)
    {
	//
	// Push a reference to the location for the
	// return value onto the stack.
	//

	slcontext.addInst (new SimdPushRefInst (info->addr(), lineNumber));

	//
	// Generate code for the expression that computes the return value.
	//

	returnedValue->generateCode (lcontext);

	//
	// Cast the return value to the return type of the function
	// that contains this return statement.
	//

	info->type()->generateCastFrom (returnedValue, lcontext);

	//
	// Copy the value into the location for the return value.
	//

	info->type()->generateCode (this, lcontext);
    }

    //
    // Return from the function.
    //

    slcontext.addInst (new SimdReturnInst (lineNumber));
}


SimdWhileNode::SimdWhileNode
    (int lineNumber,
     const ExprNodePtr &condition,
     const StatementNodePtr &loopBody)
:
    WhileNode (lineNumber, condition, loopBody)
{
    // empty
}


void
SimdWhileNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for a while statement.
    //

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);

    //
    // Generate a code path for the expression that computes the
    // loop condition.  Cast the value of the expression to type bool.
    // 

    SimdLContext::Path mainInstPath = slcontext.currentPath();

    const SimdInst *firstCondInst =
	generateCodeForPath (condition, slcontext);

    BoolTypePtr boolType = lcontext.newBoolType ();
    boolType->generateCastFrom (condition, lcontext);

    //
    // Generate a code path for the loop body.
    //

    const SimdInst *firstLoopBodyInst =
	generateCodeForPath (loopBody, slcontext);

    //
    // Append a loop instruction to the "main path", that is,
    // the path that contains the while statement.
    //

    slcontext.setCurrentPath (mainInstPath);
    slcontext.addInst (new SimdLoopInst (firstCondInst, firstLoopBodyInst, 
					 lineNumber));
}


SimdBinaryOpNode::SimdBinaryOpNode
    (int lineNumber,
     Token op,
     const ExprNodePtr &leftOperand,
     const ExprNodePtr &rightOperand)
:
    BinaryOpNode (lineNumber, op, leftOperand, rightOperand)
{
    // empty
}


void
SimdBinaryOpNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for a binary operator.
    //

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);

    if (op == TK_AND)
    {
	//
	// The right operand of an "and" is evaluated only if the left
	// operand is true.  We implement the "and" roughly like this:
	//
	//     if (leftOperand)
	//         rightOperand
	//     else
	//         false
	//

	//
	// In the "main path", that is, the path that contains the
	// "and", generate code to evaluate the left operand and
	// to cast the value to type bool.
	//

	BoolTypePtr boolType = lcontext.newBoolType ();

	leftOperand->generateCode (lcontext);
	boolType->generateCastFrom (leftOperand, lcontext);

	SimdLContext::Path mainInstPath = slcontext.currentPath();

	//
	// In a new "true path", generate code to evaluate the right
	// operand and to cast the value to type bool.
	//

	const SimdInst *firstTrueInst =
	    generateCodeForPath (rightOperand, slcontext);

	boolType->generateCastFrom (leftOperand, lcontext);

	//
	// In a new "false path", generate code that produces "false".
	//

	slcontext.newPath();
	slcontext.addInst (new SimdPushLiteralInst <bool> (false, lineNumber));

	const SimdInst *firstFalseInst =
	    slcontext.currentPath().firstInst;

	//
	// Append a branch to the main path.  This selects between
	// the right operand and "false", depending on the value
	// of the left operand.
	//

	slcontext.setCurrentPath (mainInstPath);
	slcontext.addInst (new SimdBranchInst (firstTrueInst, firstFalseInst, 
					       true, lineNumber));
    }
    else if (op == TK_OR)
    {
	//
	// The right operand of an "or" is evaluated only if the left
	// operand is false.  We implement the "or" roughly like this:
	//
	//     if (leftOperand)
	//         true
	//     else
	//         rightOperand
	//

	//
	// In the "main path", that is, the path that contains the
	// "and", generate code to evaluate the left operand and
	// to cast the value to type bool.
	//

	BoolTypePtr boolType = lcontext.newBoolType ();

	leftOperand->generateCode (lcontext);
	boolType->generateCastFrom (leftOperand, lcontext);

	SimdLContext::Path mainInstPath = slcontext.currentPath();

	//
	// In a new "true path", generate code that produces "true".
	//

	slcontext.newPath();
	slcontext.addInst (new SimdPushLiteralInst <bool> (true, lineNumber));

	const SimdInst *firstTrueInst =
	    slcontext.currentPath().firstInst;

	//
	// In a new "false path", generate code to evaluate the right
	// operand and to cast the value to type bool.
	//

	const SimdInst *firstFalseInst =
	    generateCodeForPath (rightOperand, slcontext);

	boolType->generateCastFrom (leftOperand, lcontext);

	//
	// Append a branch to the main path.  This selects between
	// "true" and the right operand, depending on the value
	// of the left operand.
	//

	slcontext.setCurrentPath (mainInstPath);
	slcontext.addInst (new SimdBranchInst (firstTrueInst, firstFalseInst, 
					       true, lineNumber));
    }
    else
    {
	//
	// Operations other than "and" and "or":
	//
	// Evaluate the left and right operands and cast their
	// values to a common type, indicated by operandType.
	// Then perform the binary operation.
	//

	leftOperand->generateCode (lcontext);
	operandType->generateCastFrom (leftOperand, lcontext);
	rightOperand->generateCode (lcontext);
	operandType->generateCastFrom (rightOperand, lcontext);
	operandType->generateCode (this, lcontext);
    }
}


SimdUnaryOpNode::SimdUnaryOpNode
    (int lineNumber,
     Token op,
     const ExprNodePtr &operand)
:
    UnaryOpNode (lineNumber, op, operand)
{
    // empty
}


void
SimdUnaryOpNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for a unary operator:
    //
    // Evaluate the operand, cast it to the type that
    // the operation will produce, and perform the operation.
    //

    operand->generateCode (lcontext);
    type->generateCastFrom (operand, lcontext);
    type->generateCode (this, lcontext);
}


SimdArrayIndexNode::SimdArrayIndexNode
    (int lineNumber,
     const ExprNodePtr &array,
     const ExprNodePtr &index)
:
    ArrayIndexNode (lineNumber, array, index)
{
    // empty
}


void
SimdArrayIndexNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for an array indexing operation:
    //
    // Push a reference to the array onto the stack,
    // evaluate the expression for the index, cast the
    // index to type int, and perform the indexing
    // operation.
    //

    array->generateCode (lcontext);
    index->generateCode (lcontext);

    IntTypePtr intType = lcontext.newIntType ();
    intType->generateCastFrom (index, lcontext);

    array->type->generateCode (this, lcontext);
}


SimdMemberNode::SimdMemberNode
    (int lineNumber,
     const ExprNodePtr &obj,
     const std::string &member)
:
	MemberNode (lineNumber, obj, member)
{
    // empty
}


void
SimdMemberNode::generateCode (LContext &lcontext)
{
    //
    //
    //


    obj->generateCode (lcontext);
    obj->type->generateCode(this, lcontext);

}


SimdSizeNode::SimdSizeNode
    (int lineNumber,
     const ExprNodePtr &obj)
:
	SizeNode (lineNumber, obj)
{
    // empty
}


void
SimdSizeNode::generateCode (LContext &lcontext)
{
    //
    //
    //

//    obj->generateCode (lcontext);
    obj->type->generateCode(this, lcontext);

}



SimdNameNode::SimdNameNode
    (int lineNumber,
     const std::string &name,
     const SymbolInfoPtr &info)
:
    NameNode (lineNumber, name, info)
{
    // empty
}


void
SimdNameNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for a reference to a variable.
    //

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);
    slcontext.addInst (new SimdPushRefInst (info->addr(), lineNumber));
}


SimdBoolLiteralNode::SimdBoolLiteralNode
    (int lineNumber,
     const LContext &lcontext,
     bool value)
:
    BoolLiteralNode (lineNumber, lcontext, value)
{
    // empty
}


void
SimdBoolLiteralNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for a literal value of type bool.
    //

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);
    slcontext.addInst (new SimdPushLiteralInst <bool> (value, lineNumber));
}

char*
SimdBoolLiteralNode::valuePtr()
{
    return (char*)(&value);
}



SimdIntLiteralNode::SimdIntLiteralNode
    (int lineNumber,
     const LContext &lcontext,
     int value)
:
    IntLiteralNode (lineNumber, lcontext, value)
{
    // empty
}


void
SimdIntLiteralNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for a literal value of type int.
    //

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);
    slcontext.addInst (new SimdPushLiteralInst <int> (value, lineNumber));
}

char*
SimdIntLiteralNode::valuePtr()
{
    return (char*)(&value);
}


SimdUIntLiteralNode::SimdUIntLiteralNode
    (int lineNumber,
     const LContext &lcontext,
     unsigned int value)
:
    UIntLiteralNode (lineNumber, lcontext, value)
{
    // empty
}


void
SimdUIntLiteralNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for a literal value of type int.
    //

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);
    slcontext.addInst (new SimdPushLiteralInst <unsigned> (value, lineNumber));
}

char*
SimdUIntLiteralNode::valuePtr()
{
    return (char*)(&value);
}


SimdHalfLiteralNode::SimdHalfLiteralNode
    (int lineNumber,
     const LContext &lcontext,
     half value)
:
    HalfLiteralNode (lineNumber, lcontext, value)
{
    // empty
}


void
SimdHalfLiteralNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for a literal value of type float.
    //

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);
    slcontext.addInst (new SimdPushLiteralInst <half> (value, lineNumber));
}

char*
SimdHalfLiteralNode::valuePtr()
{
    return (char*)(&value);
}

SimdFloatLiteralNode::SimdFloatLiteralNode
    (int lineNumber,
     const LContext &lcontext,
     float value)
:
    FloatLiteralNode (lineNumber, lcontext, value)
{
    // empty
}


void
SimdFloatLiteralNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for a literal value of type float.
    //

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);
    slcontext.addInst (new SimdPushLiteralInst <float> (value, lineNumber));
}

char*
SimdFloatLiteralNode::valuePtr()
{
    return (char*)(&value);
}

SimdStringLiteralNode::SimdStringLiteralNode
    (int lineNumber,
     const LContext &lcontext,
     const string &value)
:
    StringLiteralNode (lineNumber, lcontext, value)
{
    // empty
}


void
SimdStringLiteralNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for a literal value of type float.
    //

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);
    slcontext.addInst (new SimdPushStringLiteralInst (value, lineNumber));
}

char*
SimdStringLiteralNode::valuePtr()
{
    return 0;
}

SimdCallNode::SimdCallNode
    (int lineNumber,
     const NameNodePtr &function,
     const ExprNodeVector &arguments)
:
    CallNode (lineNumber, function, arguments)
{
    // empty
}

bool
SimdCallNode::returnsType(const TypePtr &t) const
{
    SymbolInfoPtr info = function->info;

    if (!info)
	return false;

    FunctionTypePtr functionType = function->info->functionType();
    DataTypePtr returnType = functionType->returnType();

    return returnType->isSameTypeAs (t);
}



//-----------------------------------------------------------------------------
//
//	Recognizing the helpers listed above in a CTL function definition.
//
//	A name is not enough.  CTL reserves none of these names, so a
//	program is free to define its own clip() that does something else
//	entirely, and substituting our body for that one would silently
//	change the result.  What follows therefore matches the function's
//	*body*: a definition is recognized only when it computes exactly
//	the operation the C++ body computes.  Anything else -- a different
//	comparison, a swapped return, an extra statement -- is left alone
//	and compiles into an ordinary CTL function call.
//
//	The check runs once per function definition, in
//	SimdFunctionNode::generateCode, and the result is recorded on the
//	function's symbol.  Call sites read the recorded value, so a call
//	costs no more to compile than it did before.
//
//	Helpers that call other CTL functions (clip calls min, copysign
//	calls sign) are recognized only when the function they call was
//	itself recognized, so redefining min also stops clip from being
//	substituted.
//
//-----------------------------------------------------------------------------

namespace {

//
// Kinds recorded on a symbol.  Values are stable: the switch in
// SimdCallNode::generateCode and the test suite both depend on them.
// SIGN is verified but never substituted; it exists only so that
// copysign can require a genuine sign().
//

enum InlineKind
{
    INLINE_NONE		= -1,
    INLINE_MIN		= 0,
    INLINE_MAX		= 1,
    INLINE_CLIP		= 2,
    INLINE_RAD_TO_DEG	= 6,
    INLINE_DEG_TO_RAD	= 7,
    INLINE_COPYSIGN	= 9,
    INLINE_WRAP_TO_360	= 10,
    INLINE_SIGN		= 100
};


//
// Every body recognized below has a fixed, known length, so the
// patterns index into the statement list and check its length rather
// than walking it.
//

int
statementCount (const StatementNodePtr &s)
{
    int n = 0;
    for (StatementNode *p = s.pointer(); p; p = p->next.pointer())
	n++;
    return n;
}


StatementNode *
statementAt (const StatementNodePtr &s, int index)
{
    StatementNode *p = s.pointer();
    for (int i = 0; p && i < index; i++)
	p = p->next.pointer();
    return p;
}


//
// Is e a reference to a local or parameter (that is, to something at a
// frame-relative address) rather than to a global?  A global constant
// such as M_PI lives at an absolute address, so this distinguishes the
// two without needing the frame offset itself.
//

bool
isFrameRelative (const SymbolInfoPtr &info)
{
    if (!info)
	return false;

    SimdDataAddrPtr addr = info->addr().cast<SimdDataAddr>();

    return addr && addr->reg() == 0;
}


//
// Is e a reference to parameter number index of ft?
//
// Compared against the function's own parameter name, so a definition
// that spells its parameters differently is still recognized.  The
// frame-relative test rules out a same-named global.
//

bool
isParamRef (const ExprNodePtr &e, const FunctionTypePtr &ft, size_t index)
{
    NameNodePtr n = e.cast<NameNode>();

    if (!n || !ft || index >= ft->parameters().size())
	return false;

    return n->name == ft->parameters()[index].name && isFrameRelative (n->info);
}


bool
isLocalRef (const ExprNodePtr &e, const VariableNode *v)
{
    NameNodePtr n = e.cast<NameNode>();

    return n && v && n->info && n->info.pointer() == v->info.pointer();
}


//
// CTL promotes an integer literal to float where a float is wanted, so
// both spellings of, say, 360 have to be accepted.
//

bool
isNumericLiteral (const ExprNodePtr &e, double value)
{
    if (FloatLiteralNodePtr f = e.cast<FloatLiteralNode>())
	return f->value == value;

    if (HalfLiteralNodePtr h = e.cast<HalfLiteralNode>())
	return (double) h->value == value;

    if (IntLiteralNodePtr i = e.cast<IntLiteralNode>())
	return (double) i->value == value;

    if (UIntLiteralNodePtr u = e.cast<UIntLiteralNode>())
	return (double) u->value == value;

    return false;
}


//
// M_PI is defined by the standard library as a constant in a register
// rather than as a literal, so it reaches code generation as a name.
//

bool
isGlobalConstRef (const ExprNodePtr &e, const char *name)
{
    NameNodePtr n = e.cast<NameNode>();

    return n && n->name == name && n->info && !isFrameRelative (n->info);
}


BinaryOpNode *
asBinaryOp (const ExprNodePtr &e, Token op)
{
    BinaryOpNodePtr b = e.cast<BinaryOpNode>();

    return (b && b->op == op) ? b.pointer() : 0;
}


//
// How one helper requires another: the callee must already have been
// recognized as computing the operation named by hint.
//

CallNode *
asRecognizedCall (const ExprNodePtr &e, int hint, size_t argCount)
{
    CallNodePtr c = e.cast<CallNode>();

    if (!c || c->arguments.size() != argCount)
	return 0;

    if (!c->function || !c->function->info)
	return 0;

    return (c->function->info->codeGenHint() == hint) ? c.pointer() : 0;
}


//
// Is e a call to one of the standard library functions we are willing
// to see inside a recognized body?  These are C functions in the symbol
// table, so there is no CTL body to inspect; we identify them by name
// and by the fact that they are not user-defined.
//

CallNode *
asStdLibCall (const ExprNodePtr &e, const char *name, size_t argCount)
{
    CallNodePtr c = e.cast<CallNode>();

    if (!c || c->arguments.size() != argCount)
	return 0;

    if (!c->function || c->function->name != name || !c->function->info)
	return 0;

    return c->function->info->addr().cast<SimdCFuncAddr>() ? c.pointer() : 0;
}


//
// return <expr>;  as the function's whole body.
//

ExprNodePtr
soleReturnValue (const StatementNodePtr &body)
{
    if (statementCount (body) != 1)
	return 0;

    ReturnNodePtr r = StatementNodePtr (statementAt (body, 0)).cast<ReturnNode>();

    return r ? r->returnedValue : ExprNodePtr (0);
}


//
//	float min (float a, float b)
//	{
//	    if (a < b)
//		return a;
//	    else
//		return b;
//	}
//
// max is the same shape with the comparison reversed, which is why op
// is a parameter here.
//

bool
isMinMaxBody (const StatementNodePtr &body, const FunctionTypePtr &ft, Token op)
{
    if (statementCount (body) != 1)
	return false;

    IfNodePtr n = StatementNodePtr (statementAt (body, 0)).cast<IfNode>();

    if (!n)
	return false;

    BinaryOpNode *cond = asBinaryOp (n->condition, op);

    if (!cond ||
	!isParamRef (cond->leftOperand, ft, 0) ||
	!isParamRef (cond->rightOperand, ft, 1))
	return false;

    if (statementCount (n->truePath) != 1 || statementCount (n->falsePath) != 1)
	return false;

    ReturnNodePtr t = StatementNodePtr (statementAt (n->truePath, 0)).cast<ReturnNode>();
    ReturnNodePtr f = StatementNodePtr (statementAt (n->falsePath, 0)).cast<ReturnNode>();

    return t && f &&
	   isParamRef (t->returnedValue, ft, 0) &&
	   isParamRef (f->returnedValue, ft, 1);
}


//
//	float clip (float v)
//	{
//	    return min (v, 1.0);
//	}
//
// Requires the min() being called to be a recognized min().
//

bool
isClipBody (const StatementNodePtr &body, const FunctionTypePtr &ft)
{
    CallNode *c = asRecognizedCall (soleReturnValue (body), INLINE_MIN, 2);

    return c &&
	   isParamRef (c->arguments[0], ft, 0) &&
	   isNumericLiteral (c->arguments[1], 1.0);
}


//
//	float radians_to_degrees (float radians)	// scale 180, then / M_PI
//	{
//	    return radians * 180.0 / M_PI;
//	}
//
//	float degrees_to_radians (float degrees)	// / 180, then scale M_PI
//	{
//	    return degrees / 180.0 * M_PI;
//	}
//
// outer and inner are the two operators in source order; the two
// conversions differ only in which is which, so the operand checks are
// shared.
//

bool
isAngleConversionBody (const StatementNodePtr &body,
		       const FunctionTypePtr &ft,
		       Token outer,
		       Token inner)
{
    BinaryOpNode *o = asBinaryOp (soleReturnValue (body), outer);

    if (!o || !isGlobalConstRef (o->rightOperand, "M_PI"))
	return false;

    BinaryOpNode *i = asBinaryOp (o->leftOperand, inner);

    return i &&
	   isParamRef (i->leftOperand, ft, 0) &&
	   isNumericLiteral (i->rightOperand, 180.0);
}


//
//	int sign (float x)
//	{
//	    int y;
//	    if (x < 0)		y = -1;
//	    else if (x > 0)	y = 1;
//	    else		y = 0;
//	    return y;
//	}
//
// Never substituted on its own; recognized only so that copysign can
// insist on a genuine sign().  A returns-int helper does not fit the
// float call shape the inline templates use.
//

//
// y = <value>;  where value may be negative.  A negated literal is
// folded by the parser into a single literal, so there is no unary
// minus node to look through here.
//

bool
isSignedAssignment (const StatementNodePtr &s, const VariableNode *y, double value)
{
    if (statementCount (s) != 1)
	return false;

    AssignmentNodePtr a = StatementNodePtr (statementAt (s, 0)).cast<AssignmentNode>();

    return a && isLocalRef (a->lhs, y) && isNumericLiteral (a->rhs, value);
}


bool
isSignBody (const StatementNodePtr &body, const FunctionTypePtr &ft)
{
    if (statementCount (body) != 3)
	return false;

    VariableNodePtr y =
	StatementNodePtr (statementAt (body, 0)).cast<VariableNode>();

    if (!y || y->initialValue)
	return false;

    IfNodePtr neg = StatementNodePtr (statementAt (body, 1)).cast<IfNode>();

    if (!neg)
	return false;

    BinaryOpNode *negCond = asBinaryOp (neg->condition, TK_LESS);

    if (!negCond ||
	!isParamRef (negCond->leftOperand, ft, 0) ||
	!isNumericLiteral (negCond->rightOperand, 0.0))
	return false;

    if (!isSignedAssignment (neg->truePath, y.pointer(), -1.0))
	return false;

    //
    // else if (x > 0) y = 1; else y = 0;
    //

    if (statementCount (neg->falsePath) != 1)
	return false;

    IfNodePtr pos = StatementNodePtr (statementAt (neg->falsePath, 0)).cast<IfNode>();

    if (!pos)
	return false;

    BinaryOpNode *posCond = asBinaryOp (pos->condition, TK_GREATER);

    if (!posCond ||
	!isParamRef (posCond->leftOperand, ft, 0) ||
	!isNumericLiteral (posCond->rightOperand, 0.0))
	return false;

    if (!isSignedAssignment (pos->truePath, y.pointer(), 1.0) ||
	!isSignedAssignment (pos->falsePath, y.pointer(), 0.0))
	return false;

    ReturnNodePtr r = StatementNodePtr (statementAt (body, 2)).cast<ReturnNode>();

    return r && isLocalRef (r->returnedValue, y.pointer());
}


//
//	float copysign (float x, float y)
//	{
//	    return sign (y) * fabs (x);
//	}
//
// Requires a recognized sign().  Note the argument order: sign takes
// the second parameter, fabs the first.
//

bool
isCopysignBody (const StatementNodePtr &body, const FunctionTypePtr &ft)
{
    BinaryOpNode *m = asBinaryOp (soleReturnValue (body), TK_TIMES);

    if (!m)
	return false;

    CallNode *s = asRecognizedCall (m->leftOperand, INLINE_SIGN, 1);
    CallNode *f = asStdLibCall (m->rightOperand, "fabs", 1);

    return s && f &&
	   isParamRef (s->arguments[0], ft, 1) &&
	   isParamRef (f->arguments[0], ft, 0);
}


//
//	float wrap_to_360 (float hue)
//	{
//	    float y = fmod (hue, 360.);
//	    if (y < 0.)
//		y = y + 360.;
//	    return y;
//	}
//

bool
isWrapTo360Body (const StatementNodePtr &body, const FunctionTypePtr &ft)
{
    if (statementCount (body) != 3)
	return false;

    VariableNodePtr y =
	StatementNodePtr (statementAt (body, 0)).cast<VariableNode>();

    if (!y)
	return false;

    CallNode *mod = asStdLibCall (y->initialValue, "fmod", 2);

    if (!mod ||
	!isParamRef (mod->arguments[0], ft, 0) ||
	!isNumericLiteral (mod->arguments[1], 360.0))
	return false;

    IfNodePtr n = StatementNodePtr (statementAt (body, 1)).cast<IfNode>();

    if (!n || n->falsePath)
	return false;

    BinaryOpNode *cond = asBinaryOp (n->condition, TK_LESS);

    if (!cond ||
	!isLocalRef (cond->leftOperand, y.pointer()) ||
	!isNumericLiteral (cond->rightOperand, 0.0))
	return false;

    if (statementCount (n->truePath) != 1)
	return false;

    AssignmentNodePtr a =
	StatementNodePtr (statementAt (n->truePath, 0)).cast<AssignmentNode>();

    if (!a || !isLocalRef (a->lhs, y.pointer()))
	return false;

    BinaryOpNode *sum = asBinaryOp (a->rhs, TK_PLUS);

    if (!sum ||
	!isLocalRef (sum->leftOperand, y.pointer()) ||
	!isNumericLiteral (sum->rightOperand, 360.0))
	return false;

    ReturnNodePtr r = StatementNodePtr (statementAt (body, 2)).cast<ReturnNode>();

    return r && isLocalRef (r->returnedValue, y.pointer());
}

} // namespace


//
// Decide what, if anything, a function definition computes.  Called
// once per definition; the answer is recorded on the function's symbol.
//
// The signature is checked first because it is cheap and rules out
// almost everything, then the body decides.
//

static int
recognizeHelperBody (const FunctionTypePtr &ft, const StatementNodePtr &body)
{
    if (!ft || !body)
	return INLINE_NONE;

    const ParamVector &params = ft->parameters();

    for (size_t i = 0; i < params.size(); i++)
	if (!params[i].type.cast<FloatType>() || params[i].isWritable())
	    return INLINE_NONE;

    if (ft->returnType().cast<IntType>() && params.size() == 1)
	return isSignBody (body, ft) ? INLINE_SIGN : INLINE_NONE;

    if (!ft->returnType().cast<FloatType>())
	return INLINE_NONE;

    if (params.size() == 2)
    {
	if (isMinMaxBody (body, ft, TK_LESS))	 return INLINE_MIN;
	if (isMinMaxBody (body, ft, TK_GREATER)) return INLINE_MAX;
	if (isCopysignBody (body, ft))		 return INLINE_COPYSIGN;
    }
    else if (params.size() == 1)
    {
	if (isClipBody (body, ft))		 return INLINE_CLIP;
	if (isWrapTo360Body (body, ft))		 return INLINE_WRAP_TO_360;

	if (isAngleConversionBody (body, ft, TK_DIV, TK_TIMES))
	    return INLINE_RAD_TO_DEG;

	if (isAngleConversionBody (body, ft, TK_TIMES, TK_DIV))
	    return INLINE_DEG_TO_RAD;
    }

    return INLINE_NONE;
}


void
SimdCallNode::generateCode (LContext &lcontext)
{
    //
    // Generate code for a function call.
    //

    SimdLContext &slcontext = static_cast <SimdLContext &> (lcontext);

    SymbolInfoPtr info = function->info;

    if (!info)
	return;

    FunctionTypePtr functionType = info->functionType();

    //
    // If the function being called was recognized, when it was defined,
    // as computing one of the operations we have a direct implementation
    // for, emit that implementation instead of a call into its body.
    //
    // The decision was made from the body itself and recorded on the
    // symbol, so a function that merely shares a name with one of these
    // operations is not affected: its hint is INLINE_NONE and it
    // compiles into an ordinary call, below.
    //
    // The arg-push / return-slot layout is identical to the SimdCallInst
    // path the CTL function call would produce, so stack and scratch
    // bookkeeping are unchanged.
    //

    SimdCFunc cf = 0;

    switch (info->codeGenHint())
    {
      case INLINE_MIN:	      cf = simdFunc2Arg<InlineMinFloat>;       break;
      case INLINE_MAX:	      cf = simdFunc2Arg<InlineMaxFloat>;       break;
      case INLINE_CLIP:	      cf = simdFunc1Arg<InlineClipFloat>;      break;
      case INLINE_RAD_TO_DEG: cf = simdFunc1Arg<InlineRadToDegFloat>;  break;
      case INLINE_DEG_TO_RAD: cf = simdFunc1Arg<InlineDegToRadFloat>;  break;
      case INLINE_COPYSIGN:   cf = simdFunc2Arg<InlineCopysignFloat>;  break;
      case INLINE_WRAP_TO_360:cf = simdFunc1Arg<InlineWrapTo360Float>; break;
      default:		      cf = 0;			             break;
    }

    if (cf)
    {
	functionType->returnType()->generateCode (this, lcontext);

	const ParamVector &params = functionType->parameters();
	for (int i = (int)params.size() - 1; i >= 0; --i)
	{
	    ExprNodePtr a = (i < (int)arguments.size())
		            ? arguments[i] : params[i].defaultValue;
	    a->generateCode (lcontext);
	    params[i].type->generateCastFrom (a, lcontext);
	}

	slcontext.addInst (new SimdCCallInst (cf,
					      (int)params.size(),
					      lineNumber));
	slcontext.addInst (new SimdFileNameInst (lcontext.fileName(),
						 lineNumber));
	return;
    }

    //
    // Reserve space on the stack for the function's return value
    //

    functionType->returnType()->generateCode (this, lcontext);

    //
    // Push the arguments for the call onto the stack.
    //

    const ParamVector &parameters = functionType->parameters();

    int numParameters = parameters.size();
    for (int i = parameters.size() - 1; i >= 0; --i)
    {
	ExprNodePtr nextArg;

	if (i >= (int)arguments.size())
	{
	    //
	    // Argument is not explicity specified; use the default
	    // value for the corresponding function parameter.
	    //
	    nextArg = parameters[i].defaultValue;
	}
	else
	{
	    //
	    // Argument is explicitly specified.
	    //
	    nextArg = arguments[i];
	}

	nextArg->generateCode (lcontext);
	parameters[i].type->generateCastFrom(nextArg, lcontext);

	// If the size is unknown, push the unknown sizes onto the array
	//
	SimdArrayTypePtr arrayParam = parameters[i].type.cast<SimdArrayType>();
	if(arrayParam)
	{
	    SimdArrayTypePtr arrayArg = nextArg->type.cast<SimdArrayType>();
	    assert(arrayArg);

            SizeVector unknown;
	    arrayParam->sizes(unknown);
	    for(int i = 0; i < (int)unknown.size(); i++)
	    {
                assert(arrayArg);
		if(unknown[i] == 0)
		{
                    if(arrayArg->size() == 0)
                    {
                        slcontext.addInst(new SimdPushRefInst 
                                          (arrayArg->unknownSize(),
                                             lineNumber));
                    }
                    else
                    {
                        slcontext.addInst 
                            (new SimdPushLiteralInst<int>(int(arrayArg->size()), 
                                                          lineNumber));
                    }
		    numParameters++;
		}
                arrayArg = arrayArg->elementType().cast<SimdArrayType>();
	    }
            assert(!arrayArg);
	}
    }

    //
    // Output a SimdCallInst if the called function is written in CTL,
    // or a SimdCCallInst if the called function is written in C++.
    //

    if (SimdCFuncAddrPtr addr = info->addr().cast<SimdCFuncAddr>())
    {
	//
	// Called function is C++ code.
	//

	slcontext.addInst (new SimdCCallInst (addr->func(), 
					      numParameters,
					      lineNumber));
    }
    else if (SimdInstAddrPtr addr = info->addr().cast<SimdInstAddr>())
    {
	//
	// Called function is CTL code, address is known.
	//

	slcontext.addInst (new SimdCallInst (addr->inst(), 
					     numParameters, 
					     lineNumber));
    }
    else
    {
	//
	// Called function is CTL code, address is unknown.
	// Output a function call instruction with a null callPath,
	// and record its location.  We will fix the instruction's
	// callPath later, when we know the address of the called
	// function (see SimdModuleNode::generateCode()).
	//

	SimdCallInst *inst = new SimdCallInst(0, numParameters, lineNumber);
	slcontext.addInst (inst);
	slcontext.mustFixCall (inst, info);
    }

    slcontext.addInst (new SimdFileNameInst(lcontext.fileName(), lineNumber));

}


SimdValueNode::SimdValueNode
    (int lineNumber,
     const ExprNodeVector &elements)
:
    ValueNode (lineNumber, elements)
{
    // empty
}


void
SimdValueNode::generateCode (LContext &lcontext)
{
    int eIndex = 0;
    generateCodeRec(lcontext, type, eIndex);
}


void
SimdValueNode::generateCodeRec (LContext &lcontext, 
				const DataTypePtr &dataType, 
				int& eIndex)
{
    // For each element in the array, 
    //    Evaluate the expression,
    //    cast the value to the type of the array
    // Then assign multiple to the array
    //
    if( StructTypePtr structType = dataType.cast<StructType>())
    {
	for(MemberVectorConstIterator it = structType->members().begin();
	    it != structType->members().end();
	    it++)
	{
	    generateCodeRec(lcontext, it->type, eIndex);
	}
    }
    else if( ArrayTypePtr arrayType = dataType.cast<ArrayType>())
    {
	for (int i = 0; i < arrayType->size(); ++i)
	{
	    generateCodeRec(lcontext, arrayType->elementType(), eIndex);
	}
    }
    else
    {
	assert(eIndex < (int)elements.size());
	elements[eIndex]->generateCode(lcontext);
	dataType->generateCastFrom(elements[eIndex], lcontext);
	eIndex++;
    }
}


void
SimdValueNode::castAndCopyRec(LContext &lcontext,
			      const DataTypePtr &dataType,
			      int &eIndex,
			      char *dest,
			      const SizeVector &sizes,
			      const SizeVector &offsets)
{
    // For each element in the array, 
    //    cast to the correct type
    //    and copy into dest
    // Then assign multiple to the array
    //
    if( StructTypePtr structType = dataType.cast<StructType>())
    {
	    for(MemberVectorConstIterator it = structType->members().begin();
	        it != structType->members().end();
	        it++)
	    {
	        castAndCopyRec(lcontext, it->type, eIndex, dest, sizes, offsets);
	    }
    }
    else if( ArrayTypePtr arrayType = dataType.cast<ArrayType>())
    {
	    for (int i = 0; i < arrayType->size(); ++i)
	    {
	        castAndCopyRec(lcontext, arrayType->elementType(), eIndex, 
			       dest, sizes, offsets);
	    }
    }
    else
    {
	    assert(eIndex < (int)elements.size());
	    LiteralNodePtr literal = elements[eIndex];
	    literal = dataType->castValue(lcontext, literal);
	    memcpy(dest+offsets[eIndex], literal->valuePtr(), sizes[eIndex]);
	    eIndex++;
    }
}


} // namespace Ctl
