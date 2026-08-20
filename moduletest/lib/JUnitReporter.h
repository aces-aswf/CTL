#ifndef CTLTEST_JUNIT_REPORTER_H
#define CTLTEST_JUNIT_REPORTER_H

#include "Reporter.h"

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

namespace ctltest {

// JUnit XML reporter -- Ant/Surefire-compatible schema.
//
// Emits a single <testsuites> document summing all suites seen during the
// run. Each onSuiteBegin starts a new <testsuite>; onCaseResult fills in
// <testcase> entries; the outer document is flushed in finalize().
//
// Buffers the full document in memory because JUnit requires suite-level
// count attributes (tests, failures, errors, skipped) which aren't known
// until the suite ends.
class JUnitReporter: public Reporter {
public:
    explicit JUnitReporter(std::ostream& os);

    void onSuiteBegin(const std::string& suiteName, size_t totalCases) override;
    void onCaseResult(const CaseResult& r) override;
    void onSuiteEnd(size_t passed, size_t failed, size_t errored,
                    size_t skipped, size_t unexpectedPass) override;

    // Flush the buffered <testsuites> document to the stream. Call once
    // after every suite has run.
    void finalize();

private:
    struct CaseEntry {
        std::string id;
        std::string description;
        std::string outcomeKind;   // "pass" | "fail" | "error" | "skip" | "unexpected_pass"
        std::string errorMessage;
        std::vector<Diagnostic> failures;
        long long   durationMicros = 0;
    };

    struct SuiteEntry {
        std::string name;
        size_t      passed = 0;
        size_t      failed = 0;
        size_t      errored = 0;
        size_t      skipped = 0;
        size_t      unexpectedPass = 0;
        long long   totalMicros = 0;
        std::vector<CaseEntry> cases;
    };

    std::ostream& _os;
    std::vector<SuiteEntry> _suites;
    bool _finalized = false;
};

} // namespace ctltest

#endif
