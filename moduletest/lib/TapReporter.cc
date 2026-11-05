#include "TapReporter.h"

#include <iomanip>
#include <sstream>

namespace ctltest {

namespace {

// YAML block strings must not contain unescaped control characters or
// colons that break the key:value shape. Double-quote + minimal escape.
std::string yamlQuote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
          case '"':  out += "\\\""; break;
          case '\\': out += "\\\\"; break;
          case '\n': out += "\\n";  break;
          case '\r': out += "\\r";  break;
          case '\t': out += "\\t";  break;
          default:
              if (static_cast<unsigned char>(c) < 0x20) {
                  std::ostringstream os;
                  os << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                     << (static_cast<unsigned>(c) & 0xff);
                  out += os.str();
              } else {
                  out.push_back(c);
              }
        }
    }
    out.push_back('"');
    return out;
}

const char* directiveFor(CaseResult::Outcome o) {
    switch (o) {
      case CaseResult::Outcome::Skipped:        return " # SKIP";
      case CaseResult::Outcome::UnexpectedPass: return " # TODO unexpected pass";
      case CaseResult::Outcome::Error:          return "";  // not ok, no directive
      default:                                  return "";
    }
}

bool isFailure(CaseResult::Outcome o) {
    return o == CaseResult::Outcome::Fail
        || o == CaseResult::Outcome::Error
        || o == CaseResult::Outcome::UnexpectedPass;
}

} // namespace

TapReporter::TapReporter(std::ostream& os): _os(os) {}

void TapReporter::ensureHeader() {
    if (_headerWritten) return;
    _os << "TAP version 14\n";
    _headerWritten = true;
}

void TapReporter::onSuiteBegin(const std::string& suiteName, size_t /*totalCases*/) {
    ensureHeader();
    _currentSuite = suiteName;
    // One "subtest" comment per suite boundary -- readable but not semantic.
    _os << "# suite: " << (suiteName.empty() ? "(unnamed)" : suiteName) << "\n";
}

void TapReporter::onCaseResult(const CaseResult& r) {
    ensureHeader();
    ++_caseNo;

    const std::string testName =
        (r.testcase ? r.testcase->id : std::string("<no id>"));
    const std::string qualified =
        _currentSuite.empty() ? testName : (_currentSuite + "::" + testName);

    const bool passed = !isFailure(r.outcome);
    _os << (passed ? "ok " : "not ok ") << _caseNo
        << " - " << qualified
        << directiveFor(r.outcome)
        << "\n";

    if (!isFailure(r.outcome)) return;

    // YAML block with diagnostics. Indented 2 spaces per TAP14.
    _os << "  ---\n";
    if (r.outcome == CaseResult::Outcome::Error) {
        _os << "  severity: error\n";
        if (!r.errorMessage.empty()) {
            _os << "  message: " << yamlQuote(r.errorMessage) << "\n";
        }
    } else if (r.outcome == CaseResult::Outcome::UnexpectedPass) {
        _os << "  severity: unexpected_pass\n";
        _os << "  message: \"known_failure: true but the case passed\"\n";
    } else {
        _os << "  severity: fail\n";
    }

    if (!r.verdict.summary.empty()) {
        _os << "  summary: " << yamlQuote(r.verdict.summary) << "\n";
    }
    _os << "  duration_us: " << r.duration.count() << "\n";

    if (!r.verdict.failures.empty()) {
        _os << "  diagnostics:\n";
        for (const auto& d : r.verdict.failures) {
            _os << "    - path: " << yamlQuote(d.path) << "\n";
            _os << "      expected: " << yamlQuote(d.expectedText) << "\n";
            _os << "      got: "      << yamlQuote(d.gotText) << "\n";
            if (d.abs_err != 0.0 || d.rel_err != 0.0 || d.ulp_err != 0) {
                _os << std::scientific << std::setprecision(6)
                    << "      abs_err: " << d.abs_err << "\n"
                    << "      rel_err: " << d.rel_err << "\n";
                _os.unsetf(std::ios::scientific);
                _os << "      ulp_err: " << d.ulp_err << "\n";
            }
            if (!d.toleranceSource.empty()) {
                _os << "      tolerance_source: " << yamlQuote(d.toleranceSource) << "\n";
            }
        }
    }
    _os << "  ...\n";
}

void TapReporter::onSuiteEnd(size_t /*passed*/, size_t /*failed*/, size_t /*errored*/,
                             size_t /*skipped*/, size_t /*unexpectedPass*/) {
    // The final "1..N" plan is written by finalizePlan() after all suites end.
}

void TapReporter::finalizePlan() {
    ensureHeader();
    _os << "1.." << _caseNo << "\n";
}

} // namespace ctltest
