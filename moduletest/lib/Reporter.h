#ifndef CTLTEST_REPORTER_H
#define CTLTEST_REPORTER_H

#include "Result.h"

#include <string>

namespace ctltest {

// Streaming test reporter. The runner calls these in order:
//   onSuiteBegin(suiteName, totalCases)
//     onCaseResult(...) * totalCases
//   onSuiteEnd(totalPassed, totalFailed, totalErrors, totalSkipped, totalUnexpectedPass)
//
// Concrete subclasses format each event for their sink.
class Reporter {
public:
    virtual ~Reporter() = default;

    virtual void onSuiteBegin(const std::string& suiteName, size_t totalCases) = 0;
    virtual void onCaseResult(const CaseResult& r) = 0;
    virtual void onSuiteEnd(size_t passed, size_t failed, size_t errored,
                            size_t skipped, size_t unexpectedPass) = 0;
};

} // namespace ctltest

#endif
