#ifndef CTLTEST_ORACLE_H
#define CTLTEST_ORACLE_H

#include "CaseModel.h"
#include "Result.h"

#include <map>
#include <string>

namespace ctltest {

// Abstract oracle: compare a test's computed outputs against an expected form.
class Oracle {
public:
    virtual ~Oracle() = default;
    virtual OracleVerdict check(const TestCase& tc,
                                const std::map<std::string, Value>& outputs) const = 0;
};

// Common tree-walk used by every oracle backend. Walks the `expected` Value
// tree in parallel with the `got` Value tree, looking up a per-field
// tolerance via dotted path, and emitting one Diagnostic per mismatched leaf.
//
//   basePath       -- caller-supplied root path (typically the output arg name)
//   baseTolerance  -- tolerance in effect at basePath (caller merged in
//                    suite.default + test.tolerance before descending)
//   baseSource     -- human-readable source of the starting tolerance
//                    (e.g. "test.tolerance", "suite.default")
//   out            -- diagnostics accumulated here; oracle decides whether to
//                    cap.
void compareTyped(const std::string& basePath,
                  const Value& expected,
                  const Value& got,
                  const Tolerance& baseTolerance,
                  const std::string& baseSource,
                  std::vector<Diagnostic>& out);

// Validate that every per_field key in `tol` resolves to a floating-point leaf
// inside `shape` (typically the expected-outputs map for unit/sweep). Non-FP
// leaves (bool/int/string) are compared exactly and cannot carry abs/rel/ulp;
// authoring such a tolerance is a load-time error per the plan. basePath is
// prepended to each per_field key when resolving (e.g. "return" for single
// return values). Diagnostics are appended to `out`.
void validateTolerancePaths(const std::string& basePath,
                            const Tolerance& tol,
                            const Value& shape,
                            std::vector<Diagnostic>& out);

} // namespace ctltest

#endif
