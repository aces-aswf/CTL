#ifndef CTLTEST_INTERP_RUNNER_H
#define CTLTEST_INTERP_RUNNER_H

#include "CaseModel.h"
#include "TestKit.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace Ctl {
    class SimdInterpreter;
}

namespace ctltest {

class RunError: public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Owns one Ctl::SimdInterpreter. Loads modules, invokes one CTL function with
// one sample's worth of inputs, and returns the named outputs + return value.
//
// v0.1 always dispatches at N=1 (uniform). Sweep/image modes in later phases
// will add maxSamples()-batched varying calls.
class InterpRunner {
public:
    InterpRunner();
    ~InterpRunner();

    // Installs modulePaths (process-global via Interpreter::setModulePaths)
    // for the lifetime of this runner. Previous paths are restored on dtor.
    void setModulePaths(const std::vector<std::string>& paths);

    // Loads each module by bare name. Searches module paths for "<name>.ctl".
    void loadModules(const std::vector<std::string>& names);

    // Invoke the function and return the result map:
    //   { "<argName>": Value, ..., "<returnsName>": Value }
    //
    // inputs are bound by name via bindNamedInput (falling back to
    // hasDefaultValue()). functionName may be "module::fn" or bare "fn".
    //
    // Throws RunError on load or bind failure; CTL runtime exceptions
    // (Iex::*) propagate out.
    std::map<std::string, Value> run(const std::string& functionName,
                                     const std::map<std::string, Value>& inputs,
                                     const std::string& returnsName);

    // Sweep-mode dispatch. One row per sample. Rows are batched into chunks of
    // at most maxSamples(). Varying args receive per-lane values from rows;
    // uniform args use the first row's value (and must remain identical across
    // rows -- the runner does not currently verify that).
    //
    // Returns one result map per row, in the same order as `rows`.
    std::vector<std::map<std::string, Value>>
    runBatch(const std::string& functionName,
             const std::vector<std::map<std::string, Value>>& rows,
             const std::string& returnsName);

    // Function-signature introspection. Names and type-kind info for pre-flight
    // validation (strict column shape, varying-struct-output rejection, etc).
    // Mirrors what Ctl::FunctionCall exposes through numInputArgs/numOutputArgs.
    enum class TypeKind { Scalar, Array, Struct, Other };
    struct FunctionSignature {
        std::vector<std::string> inputNames;
        std::vector<std::string> outputNames;
        std::vector<TypeKind>    outputKinds;
        bool hasNonVoidReturn = false;
        TypeKind returnKind = TypeKind::Other;
    };
    FunctionSignature signature(const std::string& functionName);

    // ctl_native dispatch. The function must be zero-arg and return void;
    // its body communicates with the framework only through testkit::*
    // SimdCFuncs that append to a per-thread assertion buffer. The returned
    // vector is the drain of that buffer (populated during the call, empty
    // if the test recorded nothing).
    std::vector<TestAssertion> runCtlNative(const std::string& functionName);

private:
    struct Impl;
    std::unique_ptr<Impl> _p;
};

} // namespace ctltest

#endif
