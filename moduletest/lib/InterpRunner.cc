#include "InterpRunner.h"
#include "Marshal.h"
#include "TestKit.h"

#include <CtlSimdInterpreter.h>
#include <CtlFunctionCall.h>

#include <sstream>

namespace ctltest {

struct InterpRunner::Impl {
    // Always the TestKit-enabled subclass -- zero cost for non-ctl_native
    // tests, and keeps construction single-path.
    std::unique_ptr<Ctl::SimdInterpreter> interp{newTestInterpreter()};
    std::vector<std::string> savedPaths;
    bool pathsSwapped = false;
};

InterpRunner::InterpRunner(): _p(new Impl()) {}

InterpRunner::~InterpRunner() {
    if (_p && _p->pathsSwapped) {
        Ctl::Interpreter::setModulePaths(_p->savedPaths);
    }
}

void InterpRunner::setModulePaths(const std::vector<std::string>& paths) {
    if (!_p->pathsSwapped) {
        _p->savedPaths = Ctl::Interpreter::modulePaths();
        _p->pathsSwapped = true;
    }
    // Append user paths to whatever was already there so CTL stdlib still resolves.
    std::vector<std::string> merged = _p->savedPaths;
    merged.insert(merged.end(), paths.begin(), paths.end());
    Ctl::Interpreter::setModulePaths(merged);
}

void InterpRunner::loadModules(const std::vector<std::string>& names) {
    for (const std::string& n : names) {
        try {
            _p->interp->loadModule(n);
        } catch (const std::exception& e) {
            throw RunError("failed to load module '" + n + "': " + e.what());
        }
    }
}

std::map<std::string, Value>
InterpRunner::run(const std::string& functionName,
                  const std::map<std::string, Value>& inputs,
                  const std::string& returnsName)
{
    Ctl::FunctionCallPtr fn;
    try {
        fn = _p->interp->newFunctionCall(functionName);
    } catch (const std::exception& e) {
        throw RunError("newFunctionCall('" + functionName + "') failed: " + e.what());
    }
    if (!fn) throw RunError("newFunctionCall returned null for '" + functionName + "'");

    // Bind inputs (uniform, single sample).
    for (size_t i = 0; i < fn->numInputArgs(); ++i) {
        Ctl::FunctionArgPtr a = fn->inputArg(i);
        if (!a) continue;
        try {
            bindNamedInput(inputs, a);
        } catch (const MarshalError& e) {
            throw RunError(std::string("input bind failed: ") + e.what());
        }
    }

    fn->callFunction(1);

    std::map<std::string, Value> results;
    for (size_t i = 0; i < fn->numOutputArgs(); ++i) {
        Ctl::FunctionArgPtr a = fn->outputArg(i);
        if (!a) continue;
        try {
            results.emplace(a->name(), fromArg(a));
        } catch (const MarshalError& e) {
            throw RunError(std::string("output read failed for '") + a->name() + "': " + e.what());
        }
    }
    Ctl::FunctionArgPtr rv = fn->returnValue();
    if (rv) {
        // Skip void return (type is VoidType). TypeStorage::get would throw.
        Ctl::DataTypePtr dt = rv->type();
        if (dt && dt->cDataType() != Ctl::VoidTypeEnum) {
            try {
                results.emplace(returnsName, fromArg(rv));
            } catch (const MarshalError& e) {
                throw RunError(std::string("return read failed: ") + e.what());
            }
        }
    }
    return results;
}

InterpRunner::FunctionSignature
InterpRunner::signature(const std::string& functionName)
{
    Ctl::FunctionCallPtr fn;
    try {
        fn = _p->interp->newFunctionCall(functionName);
    } catch (const std::exception& e) {
        throw RunError("newFunctionCall('" + functionName + "') failed: " + e.what());
    }
    if (!fn) throw RunError("newFunctionCall returned null for '" + functionName + "'");

    auto classify = [](Ctl::DataTypePtr dt) {
        if (!dt) return TypeKind::Other;
        switch (dt->cDataType()) {
          case Ctl::BoolTypeEnum:
          case Ctl::IntTypeEnum:
          case Ctl::UIntTypeEnum:
          case Ctl::HalfTypeEnum:
          case Ctl::FloatTypeEnum:
          case Ctl::StringTypeEnum:
            return TypeKind::Scalar;
          case Ctl::ArrayTypeEnum:  return TypeKind::Array;
          case Ctl::StructTypeEnum: return TypeKind::Struct;
          default:                  return TypeKind::Other;
        }
    };

    FunctionSignature sig;
    for (size_t i = 0; i < fn->numInputArgs(); ++i) {
        if (Ctl::FunctionArgPtr a = fn->inputArg(i)) sig.inputNames.push_back(a->name());
    }
    for (size_t i = 0; i < fn->numOutputArgs(); ++i) {
        Ctl::FunctionArgPtr a = fn->outputArg(i);
        if (!a) continue;
        sig.outputNames.push_back(a->name());
        sig.outputKinds.push_back(classify(a->type()));
    }
    if (Ctl::FunctionArgPtr rv = fn->returnValue()) {
        Ctl::DataTypePtr dt = rv->type();
        if (dt && dt->cDataType() != Ctl::VoidTypeEnum) {
            sig.hasNonVoidReturn = true;
            sig.returnKind = classify(dt);
        }
    }
    return sig;
}

std::vector<TestAssertion>
InterpRunner::runCtlNative(const std::string& functionName)
{
    Ctl::FunctionCallPtr fn;
    try {
        fn = _p->interp->newFunctionCall(functionName);
    } catch (const std::exception& e) {
        throw RunError("newFunctionCall('" + functionName + "') failed: " + e.what());
    }
    if (!fn) throw RunError("newFunctionCall returned null for '" + functionName + "'");

    if (fn->numInputArgs() != 0 || fn->numOutputArgs() != 0) {
        throw RunError("ctl_native test '" + functionName +
                       "' must take no arguments and have no outputs");
    }
    // Return value must be void or absent.
    if (Ctl::FunctionArgPtr rv = fn->returnValue()) {
        Ctl::DataTypePtr dt = rv->type();
        if (dt && dt->cDataType() != Ctl::VoidTypeEnum) {
            throw RunError("ctl_native test '" + functionName +
                           "' must return void");
        }
    }

    clearAssertions();
    try {
        fn->callFunction(1);
    } catch (const std::exception& e) {
        // Convert an uncaught CTL runtime exception into a recorded failure.
        TestAssertion a;
        a.kind    = TestAssertion::Kind::Fail;
        a.passed  = false;
        a.message = std::string("unexpected CTL exception: ") + e.what();
        std::vector<TestAssertion> out = drainAssertions();
        out.push_back(std::move(a));
        return out;
    }
    return drainAssertions();
}

std::vector<std::map<std::string, Value>>
InterpRunner::runBatch(const std::string& functionName,
                       const std::vector<std::map<std::string, Value>>& rows,
                       const std::string& returnsName)
{
    std::vector<std::map<std::string, Value>> out;
    out.reserve(rows.size());
    if (rows.empty()) return out;

    Ctl::FunctionCallPtr fn;
    try {
        fn = _p->interp->newFunctionCall(functionName);
    } catch (const std::exception& e) {
        throw RunError("newFunctionCall('" + functionName + "') failed: " + e.what());
    }
    if (!fn) throw RunError("newFunctionCall returned null for '" + functionName + "'");

    // CTL's parser defaults unmarked parameters to uniform. Sweep semantics
    // (one sample per row) require per-lane buffers on every arg we touch, so
    // we promote each arg to varying here. Outputs: the public FunctionArg
    // docs call setVarying() on outputs "undefined", but the Simd backend
    // resizes its internal register correctly, and CtlSimdFunctionCall
    // reconciles the flag after the call.
    for (size_t i = 0; i < fn->numInputArgs(); ++i) {
        if (Ctl::FunctionArgPtr a = fn->inputArg(i)) a->setVarying(true);
    }
    for (size_t i = 0; i < fn->numOutputArgs(); ++i) {
        if (Ctl::FunctionArgPtr a = fn->outputArg(i)) a->setVarying(true);
    }
    if (Ctl::FunctionArgPtr rv0 = fn->returnValue()) {
        Ctl::DataTypePtr dt = rv0->type();
        if (dt && dt->cDataType() != Ctl::VoidTypeEnum) rv0->setVarying(true);
    }

    const size_t maxLanes = _p->interp->maxSamples();
    if (maxLanes == 0) throw RunError("interpreter reports maxSamples()==0");

    size_t offset = 0;
    while (offset < rows.size()) {
        size_t pass = rows.size() - offset;
        if (pass > maxLanes) pass = maxLanes;

        // Bind inputs, lane by lane.
        for (size_t i = 0; i < fn->numInputArgs(); ++i) {
            Ctl::FunctionArgPtr a = fn->inputArg(i);
            if (!a) continue;
            for (size_t lane = 0; lane < pass; ++lane) {
                try {
                    bindNamedInputLane(rows[offset + lane], a, lane);
                } catch (const MarshalError& e) {
                    std::ostringstream os;
                    os << "input bind failed (row " << (offset + lane) << "): " << e.what();
                    throw RunError(os.str());
                }
            }
        }

        fn->callFunction(pass);

        // Read outputs, lane by lane.
        for (size_t lane = 0; lane < pass; ++lane) {
            std::map<std::string, Value> results;
            for (size_t i = 0; i < fn->numOutputArgs(); ++i) {
                Ctl::FunctionArgPtr a = fn->outputArg(i);
                if (!a) continue;
                try {
                    results.emplace(a->name(), fromArgLane(a, lane));
                } catch (const MarshalError& e) {
                    std::ostringstream os;
                    os << "output read failed for '" << a->name()
                       << "' (row " << (offset + lane) << "): " << e.what();
                    throw RunError(os.str());
                }
            }
            Ctl::FunctionArgPtr rv = fn->returnValue();
            if (rv) {
                Ctl::DataTypePtr dt = rv->type();
                if (dt && dt->cDataType() != Ctl::VoidTypeEnum) {
                    try {
                        results.emplace(returnsName, fromArgLane(rv, lane));
                    } catch (const MarshalError& e) {
                        std::ostringstream os;
                        os << "return read failed (row " << (offset + lane) << "): " << e.what();
                        throw RunError(os.str());
                    }
                }
            }
            out.push_back(std::move(results));
        }

        offset += pass;
    }

    return out;
}

} // namespace ctltest
