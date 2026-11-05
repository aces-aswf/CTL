#ifndef CTLTEST_RUNNER_H
#define CTLTEST_RUNNER_H

#include "CaseModel.h"
#include "Result.h"

#include <vector>

namespace ctltest {

class Reporter;

// Executes an entire suite: for each TestCase, instantiate an InterpRunner,
// load modules, run, apply the declared oracle, aggregate CaseResults, and
// stream them to the Reporter.
//
// Returns a non-zero count iff any case ended in Fail / Error / UnexpectedPass.
struct RunCounts {
    size_t passed = 0;
    size_t failed = 0;
    size_t errored = 0;
    size_t skipped = 0;
    size_t unexpectedPass = 0;

    size_t total() const { return passed + failed + errored + skipped + unexpectedPass; }
    size_t nonPassing() const { return failed + errored + unexpectedPass; }
};

RunCounts runSuite(const Suite& suite, Reporter& reporter);

} // namespace ctltest

#endif
