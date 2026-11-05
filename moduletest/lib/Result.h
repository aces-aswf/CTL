#ifndef CTLTEST_RESULT_H
#define CTLTEST_RESULT_H

#include "CaseModel.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace ctltest {

// Diagnostic -- one mismatch, located by dotted path into the output tree.
struct Diagnostic {
    std::string path;           // "return", "out.r", "out[2]"
    std::string expectedText;
    std::string gotText;
    double abs_err = 0.0;
    double rel_err = 0.0;
    int    ulp_err = 0;
    Tolerance applied;          // the tolerance that was in effect at this leaf
    std::string toleranceSource; // human text: "suite.default" / "test.tolerance" / "test.tolerance.per_field[out.b]"
};

// OracleVerdict -- the output of Oracle::check().
struct OracleVerdict {
    bool passed = false;
    std::vector<Diagnostic> failures;   // first K; bounded by reporter
    std::string summary;                // image mode: "147/1048576 pixels failed"
};

// CaseResult -- full record for one TestCase after execution.
struct CaseResult {
    enum class Outcome { Pass, Fail, Error, Skipped, UnexpectedPass };

    const TestCase* testcase = nullptr;
    Outcome outcome = Outcome::Error;

    OracleVerdict verdict;
    std::string   errorMessage;  // populated on Outcome::Error (load / marshal / CTL runtime)

    std::chrono::microseconds duration{0};
};

} // namespace ctltest

#endif
