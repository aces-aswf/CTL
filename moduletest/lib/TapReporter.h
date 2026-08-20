#ifndef CTLTEST_TAP_REPORTER_H
#define CTLTEST_TAP_REPORTER_H

#include "Reporter.h"

#include <cstddef>
#include <ostream>

namespace ctltest {

// TAP v14 reporter -- https://testanything.org/tap-version-14-specification.html
//
// Emits one TAP stream across all suites:
//   TAP version 14
//   1..N                    (N = sum of cases across all onSuiteBegin calls)
//   ok N - <suite>/<id>
//   not ok N - <suite>/<id>
//     ---
//     message: ...
//     diagnostics:
//       - path: return
//         expected: "0.3"
//         got: "0.30000000000000004"
//     ...
//
// Works with standard TAP harnesses (prove, tap-mocha-reporter, node-tap's
// raw reader, etc.).
class TapReporter: public Reporter {
public:
    explicit TapReporter(std::ostream& os);

    void onSuiteBegin(const std::string& suiteName, size_t totalCases) override;
    void onCaseResult(const CaseResult& r) override;
    void onSuiteEnd(size_t passed, size_t failed, size_t errored,
                    size_t skipped, size_t unexpectedPass) override;

    // Emit the trailing plan line "1..<N>" where N is the cumulative number
    // of cases seen so far. Call once after all suites have run. TAP14 allows
    // either a leading or trailing plan; we use trailing so multi-suite runs
    // in the CLI produce a single well-formed stream.
    void finalizePlan();

private:
    std::ostream& _os;
    std::string   _currentSuite;
    size_t        _caseNo = 0;
    bool          _headerWritten = false;

    void ensureHeader();
};

} // namespace ctltest

#endif
