///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

// Scope: only the std::string and std::stringstream constructor
// variants -- the paths the interpreter itself takes (Iex::THROW wraps
// the stringstream form).  The printf-style variadic ctor +
// _explain() formatter crashes deterministically inside vsnprintf ->
// localeconv_l on Apple Silicon: a latent va_list ABI bug never hit
// because no caller in lib/IlmCtl* uses format args.  Untested here.

#include <CtlExc.h>
#include <testExc.h>

#include <iostream>
#include <sstream>
#include <string>
#include <testRequire.h>

using namespace Ctl;
using namespace std;


namespace {

void
testStringConstructor ()
{
    cout << "  std::string ctor preserves message" << endl;

    const string msg = "owned-string error message";
    try
    {
	throw CtlExc(msg);
    }
    catch (const CtlExc &e)
    {
	REQUIRE(string(e.what()) == msg);
    }
}


void
testStringstreamConstructor ()
{
    cout << "  std::stringstream ctor preserves message" << endl;

    stringstream ss;
    ss << "stream-built error " << 7 << " (" << 3.14 << ")";
    try
    {
	throw CtlExc(ss);
    }
    catch (const CtlExc &e)
    {
	const string what = e.what();
	REQUIRE(what.find("stream-built error 7") != string::npos);
	REQUIRE(what.find("3.14") != string::npos);
    }
}


void
testSubclassDispatch ()
{
    cout << "  CTL_DEFINE_EXC subclasses each catch as themselves AND as CtlExc" << endl;

    // Each macro-defined subclass of CtlExc must:
    //   1) be catchable as its own exact type (RTTI sanity)
    //   2) be catchable as CtlExc (inheritance preserved)
    //   3) preserve its message through what()

    // MaxInstExc as its own type.
    try { throw MaxInstExc(string("max inst hit at depth 12")); }
    catch (const MaxInstExc &e)
    {
	REQUIRE(string(e.what()) == "max inst hit at depth 12");
    }
    // MaxInstExc as CtlExc base -- inheritance preserved.
    try { throw MaxInstExc(string("for base catch")); }
    catch (const CtlExc &e)
    {
	REQUIRE(string(e.what()) == "for base catch");
    }

    // IndexOutOfRangeExc.
    try { throw IndexOutOfRangeExc(string("index 5 of 3")); }
    catch (const IndexOutOfRangeExc &e)
    {
	REQUIRE(string(e.what()) == "index 5 of 3");
    }

    // Three sibling stack exceptions -- RTTI must distinguish them
    // from each other even though they share the macro body.
    try { throw StackOverflowExc(string("overflow")); }
    catch (const StackOverflowExc &e) { REQUIRE(string(e.what()) == "overflow"); }
    catch (...) { REQUIRE(false && "StackOverflowExc not caught as itself"); }

    try { throw StackUnderflowExc(string("underflow")); }
    catch (const StackUnderflowExc &e) { REQUIRE(string(e.what()) == "underflow"); }
    catch (...) { REQUIRE(false && "StackUnderflowExc not caught as itself"); }

    try { throw StackLogicExc(string("logic error")); }
    catch (const StackLogicExc &e) { REQUIRE(string(e.what()) == "logic error"); }
    catch (...) { REQUIRE(false && "StackLogicExc not caught as itself"); }

    // Remaining subclasses share the same macro-generated body, so a
    // representative sample is enough to pin the codegen.
    try { throw LoadModuleExc(string("module load")); }
    catch (const LoadModuleExc &e) { (void)e; }
    catch (...) { REQUIRE(false && "LoadModuleExc not caught as itself"); }

    try { throw AbortExc(string("aborted")); }
    catch (const AbortExc &e) { (void)e; }
    catch (...) { REQUIRE(false && "AbortExc not caught as itself"); }

    try { throw InvalidSizeExc(string("bad size")); }
    catch (const InvalidSizeExc &e) { (void)e; }
    catch (...) { REQUIRE(false && "InvalidSizeExc not caught as itself"); }

    try { throw ArrayMismatchExc(string("mismatch")); }
    catch (const ArrayMismatchExc &e) { (void)e; }
    catch (...) { REQUIRE(false && "ArrayMismatchExc not caught as itself"); }

    try { throw StructAccessExc(string("struct")); }
    catch (const StructAccessExc &e) { (void)e; }
    catch (...) { REQUIRE(false && "StructAccessExc not caught as itself"); }

    try { throw RuntimeExc(string("compile-time issue")); }
    catch (const RuntimeExc &e) { (void)e; }
    catch (...) { REQUIRE(false && "RuntimeExc not caught as itself"); }

    try { throw DatatypeExc(string("type conv")); }
    catch (const DatatypeExc &e) { (void)e; }
    catch (...) { REQUIRE(false && "DatatypeExc not caught as itself"); }
}


void
testStdExceptionInteroperability ()
{
    cout << "  every Ctl exception is catchable as std::exception" << endl;

    // CtlExc derives from Iex::BaseExc, which derives from std::exception.  Anything that catches
    // std::exception in user code must see Ctl errors.
    try { throw IndexOutOfRangeExc(string("ioor")); }
    catch (const std::exception &e)
    {
	REQUIRE(string(e.what()) == "ioor");
    }
}


void
testStringstreamSubclass ()
{
    cout << "  subclass stringstream ctor (the form THROW() wraps)" << endl;

    // Iex's THROW macro builds a stringstream and passes it; that's the
    // pattern the actual interpreter uses everywhere it raises a Ctl
    // exception.  Pin it specifically.
    stringstream ss;
    ss << "computed: idx=" << 42 << " size=" << 16;
    try
    {
	throw IndexOutOfRangeExc(ss);
    }
    catch (const IndexOutOfRangeExc &e)
    {
	const string what = e.what();
	REQUIRE(what.find("idx=42") != string::npos);
	REQUIRE(what.find("size=16") != string::npos);
    }
}

} // anonymous namespace


void
testExc ()
{
    cout << endl;
    cout << "Testing CtlExc hierarchy (std::string + stringstream paths)" << endl;

    testStringConstructor();
    testStringstreamConstructor();
    testSubclassDispatch();
    testStdExceptionInteroperability();
    testStringstreamSubclass();

    cout << "ok" << endl;
}
