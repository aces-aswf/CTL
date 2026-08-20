#include "ConsoleReporter.h"

#include <iomanip>

namespace ctltest {

namespace {

const char* kReset  = "\x1b[0m";
const char* kRed    = "\x1b[31m";
const char* kGreen  = "\x1b[32m";
const char* kYellow = "\x1b[33m";
const char* kDim    = "\x1b[2m";
const char* kBold   = "\x1b[1m";

const char* outcomeLabel(CaseResult::Outcome o) {
    switch (o) {
      case CaseResult::Outcome::Pass:            return "PASS";
      case CaseResult::Outcome::Fail:            return "FAIL";
      case CaseResult::Outcome::Error:           return "ERROR";
      case CaseResult::Outcome::Skipped:         return "SKIP";
      case CaseResult::Outcome::UnexpectedPass:  return "UNEXPECTED PASS";
    }
    return "?";
}

} // namespace

ConsoleReporter::ConsoleReporter(std::ostream& os, bool color)
    : _os(os), _color(color) {}

void ConsoleReporter::onSuiteBegin(const std::string& suiteName, size_t totalCases) {
    if (_color) _os << kBold;
    _os << "ctltest: " << (suiteName.empty() ? "(unnamed suite)" : suiteName)
        << " -- " << totalCases << " case" << (totalCases == 1 ? "" : "s");
    if (_color) _os << kReset;
    _os << "\n";
}

void ConsoleReporter::onCaseResult(const CaseResult& r) {
    const char* tag = outcomeLabel(r.outcome);
    const char* color = kReset;
    if (_color) {
        switch (r.outcome) {
          case CaseResult::Outcome::Pass:            color = kGreen; break;
          case CaseResult::Outcome::Fail:            color = kRed;   break;
          case CaseResult::Outcome::Error:           color = kRed;   break;
          case CaseResult::Outcome::Skipped:         color = kYellow;break;
          case CaseResult::Outcome::UnexpectedPass:  color = kRed;   break;
        }
    }
    _os << "  ";
    if (_color) _os << color;
    _os << "[" << tag << "]";
    if (_color) _os << kReset;
    _os << " " << (r.testcase ? r.testcase->id : std::string("<no id>"));

    if (r.testcase && !r.testcase->description.empty()) {
        _os << " -- " << r.testcase->description;
    }
    if (_color) _os << kDim;
    _os << "  (" << r.duration.count() << " us)";
    if (_color) _os << kReset;
    _os << "\n";

    if (r.outcome == CaseResult::Outcome::Error && !r.errorMessage.empty()) {
        _os << "      error: " << r.errorMessage << "\n";
    }
    if (r.outcome == CaseResult::Outcome::Fail
     || r.outcome == CaseResult::Outcome::UnexpectedPass) {
        for (const auto& d : r.verdict.failures) {
            _os << "      at " << (d.path.empty() ? "(root)" : d.path)
                << ": expected " << d.expectedText
                << ", got " << d.gotText;
            if (d.abs_err != 0.0 || d.rel_err != 0.0 || d.ulp_err != 0) {
                _os << std::scientific << std::setprecision(3)
                    << " [abs=" << d.abs_err
                    << " rel=" << d.rel_err
                    << " ulp=" << d.ulp_err << "]";
                _os.unsetf(std::ios::scientific);
            }
            if (!d.toleranceSource.empty()) {
                _os << " (tol: " << d.toleranceSource << ")";
            }
            _os << "\n";
        }
    }
}

void ConsoleReporter::onSuiteEnd(size_t passed, size_t failed, size_t errored,
                                 size_t skipped, size_t unexpectedPass) {
    const size_t total = passed + failed + errored + skipped + unexpectedPass;
    _os << "\n";
    if (_color) _os << kBold;
    _os << "summary: " << passed << "/" << total << " passed";
    if (_color) _os << kReset;
    if (failed)          _os << ", " << failed << " failed";
    if (errored)         _os << ", " << errored << " errored";
    if (skipped)         _os << ", " << skipped << " skipped";
    if (unexpectedPass)  _os << ", " << unexpectedPass << " UNEXPECTED PASS";
    _os << "\n";
}

} // namespace ctltest
