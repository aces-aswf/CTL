#include "TestKit.h"

#include <CtlReadWriteAccess.h>
#include <CtlSimdCFunc.h>
#include <CtlSimdInterpreter.h>
#include <CtlSimdLContext.h>
#include <CtlSimdModule.h>
#include <CtlSimdReg.h>
#include <CtlSimdStdLibrary.h>
#include <CtlSimdStdTypes.h>
#include <CtlSimdXContext.h>
#include <CtlSymbolTable.h>
#include <CtlType.h>

#include <cmath>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace ctltest {

namespace {

// Per-thread assertion buffer. Populated by SimdCFunc callbacks, drained by
// the Runner between CTL calls. Isolation is per-thread because the Simd
// interpreter holds no re-entrancy hook we could bolt onto.
thread_local std::vector<TestAssertion> g_assertions;

//----------------------------------------------------------------------------
// SimdCFunc implementations. Args live in the SimdReg stack above the frame
// pointer; the last-pushed arg is at regFpRelative(-1). These C funcs always
// read from lane 0 (uniform call site) because test_* CTL functions take no
// arguments and can't be meaningfully varying.
//----------------------------------------------------------------------------

void testkitExpectTrueFunc(const Ctl::SimdBoolMask& /*mask*/, Ctl::SimdXContext& xc)
{
    const Ctl::SimdReg& data = xc.stack().regFpRelative(-1);
    TestAssertion a;
    a.kind   = TestAssertion::Kind::ExpectTrue;
    a.passed = *(const bool*)(data[0]);
    g_assertions.push_back(std::move(a));
}

void testkitExpectNearFFunc(const Ctl::SimdBoolMask& /*mask*/, Ctl::SimdXContext& xc)
{
    // Calling convention: first param is at the top of the stack (regFp-1),
    // subsequent params are pushed below. Matches simdFunc2Arg in
    // CtlSimdStdLibTemplates.h.
    const Ctl::SimdReg& actual   = xc.stack().regFpRelative(-1);
    const Ctl::SimdReg& expected = xc.stack().regFpRelative(-2);
    const Ctl::SimdReg& absTol   = xc.stack().regFpRelative(-3);

    const float fa = *(const float*)(actual[0]);
    const float fe = *(const float*)(expected[0]);
    const float ft = *(const float*)(absTol[0]);

    TestAssertion a;
    a.kind     = TestAssertion::Kind::ExpectNearF;
    a.actual   = fa;
    a.expected = fe;
    a.abs_tol  = ft;
    a.abs_err  = std::fabs(fa - fe);
    a.passed   = a.abs_err <= ft;
    g_assertions.push_back(std::move(a));
}

//----------------------------------------------------------------------------
// Interpreter subclass -- only exists so we can reach SimdInterpreter's
// protected symtab() to register our own SimdCFuncs. Nothing else about the
// interpreter is customized.
//----------------------------------------------------------------------------

class TestInterpreter : public Ctl::SimdInterpreter {
public:
    TestInterpreter() : Ctl::SimdInterpreter()
    {
        // Build a throwaway LContext so we can fabricate the FunctionType
        // objects declareSimdCFunc wants. Mirrors SimdInterpreter's own
        // ctor-time use of SimdLContext for declareSimdStdLibrary.
        Ctl::SimdModule module(*this, "__testkit_init", "__testkit_init");
        std::stringstream buf;
        Ctl::SimdLContext lcontext(buf, &module, symtab());
        Ctl::SimdStdTypes types(lcontext);

        // void _testkit_expect_true(bool) -- uses the prebuilt v_b type.
        Ctl::declareSimdCFunc(symtab(),
                              &testkitExpectTrueFunc,
                              types.funcType_v_b(),
                              "_testkit_expect_true");

        // void _testkit_expect_near_f(float, float, float) -- hand-built,
        // there's no matching prefab in SimdStdTypes.
        Ctl::ParamVector p;
        p.push_back(Ctl::Param("actual",   types.type_f(), 0, Ctl::RWA_READ, false));
        p.push_back(Ctl::Param("expected", types.type_f(), 0, Ctl::RWA_READ, false));
        p.push_back(Ctl::Param("abs_tol",  types.type_f(), 0, Ctl::RWA_READ, false));
        Ctl::FunctionTypePtr expectType =
            lcontext.newFunctionType(types.type_v(), false, p);
        Ctl::declareSimdCFunc(symtab(),
                              &testkitExpectNearFFunc,
                              expectType,
                              "_testkit_expect_near_f");
    }
};

const char* kTestKitSource = R"CTL(
ctlversion 1;

namespace testkit
{

// Record an assertion that a boolean condition holds.
void
expect_true(bool cond)
{
    _testkit_expect_true(cond);
}

// Record an assertion that |actual - expected| <= abs_tol.
void
expect_near_f(float actual, float expected, float abs_tol)
{
    _testkit_expect_near_f(actual, expected, abs_tol);
}

} // namespace testkit
)CTL";

} // namespace

Ctl::SimdInterpreter* newTestInterpreter()
{
    TestInterpreter* interp = new TestInterpreter();
    // The testkit CTL module is tiny; load it eagerly so tests can just
    // `import "testkit"` without depending on a CTL_MODULE_PATH entry.
    interp->loadModule("testkit", "<ctltest-embedded-testkit>", kTestKitSource);
    return interp;
}

std::vector<TestAssertion> drainAssertions()
{
    std::vector<TestAssertion> out;
    out.swap(g_assertions);
    return out;
}

void clearAssertions()
{
    g_assertions.clear();
}

} // namespace ctltest
