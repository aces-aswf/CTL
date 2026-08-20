#include "JUnitReporter.h"

#include <iomanip>
#include <sstream>

namespace ctltest {

namespace {

std::string xmlEscape(const std::string& s, bool attr = false) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
          case '&':  out += "&amp;";  break;
          case '<':  out += "&lt;";   break;
          case '>':  out += "&gt;";   break;
          case '"':  out += attr ? std::string("&quot;") : std::string(1, c); break;
          case '\'': out += attr ? std::string("&apos;") : std::string(1, c); break;
          default:
              if (static_cast<unsigned char>(c) < 0x20 && c != '\n' && c != '\t') {
                  std::ostringstream os;
                  os << "&#" << static_cast<int>(static_cast<unsigned char>(c)) << ";";
                  out += os.str();
              } else {
                  out.push_back(c);
              }
        }
    }
    return out;
}

std::string secondsText(long long micros) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(6)
       << (static_cast<double>(micros) / 1.0e6);
    return os.str();
}

std::string classifyOutcome(CaseResult::Outcome o) {
    switch (o) {
      case CaseResult::Outcome::Pass:           return "pass";
      case CaseResult::Outcome::Fail:           return "fail";
      case CaseResult::Outcome::Error:          return "error";
      case CaseResult::Outcome::Skipped:        return "skip";
      case CaseResult::Outcome::UnexpectedPass: return "unexpected_pass";
    }
    return "error";
}

} // namespace

JUnitReporter::JUnitReporter(std::ostream& os): _os(os) {}

void JUnitReporter::onSuiteBegin(const std::string& suiteName, size_t /*totalCases*/) {
    SuiteEntry s;
    s.name = suiteName.empty() ? "(unnamed)" : suiteName;
    _suites.push_back(std::move(s));
}

void JUnitReporter::onCaseResult(const CaseResult& r) {
    if (_suites.empty()) {
        SuiteEntry s; s.name = "(unnamed)";
        _suites.push_back(std::move(s));
    }
    SuiteEntry& s = _suites.back();

    CaseEntry ce;
    ce.id = r.testcase ? r.testcase->id : std::string("<no id>");
    ce.description = r.testcase ? r.testcase->description : std::string();
    ce.outcomeKind = classifyOutcome(r.outcome);
    ce.errorMessage = r.errorMessage;
    ce.failures = r.verdict.failures;
    ce.durationMicros = r.duration.count();
    s.totalMicros += ce.durationMicros;
    s.cases.push_back(std::move(ce));
}

void JUnitReporter::onSuiteEnd(size_t /*passed*/, size_t /*failed*/, size_t /*errored*/,
                               size_t /*skipped*/, size_t /*unexpectedPass*/) {
    // Per-suite counts are recomputed from the buffered cases in finalize()
    // so they stay consistent when a filter hides cases in the middle.
}

void JUnitReporter::finalize() {
    if (_finalized) return;
    _finalized = true;

    // Per-suite counts: derive from buffered cases so counts reflect what
    // actually reached the reporter (filter-consistent).
    for (auto& s : _suites) {
        s.passed = s.failed = s.errored = s.skipped = s.unexpectedPass = 0;
        for (const auto& c : s.cases) {
            if      (c.outcomeKind == "pass")             ++s.passed;
            else if (c.outcomeKind == "fail")             ++s.failed;
            else if (c.outcomeKind == "error")            ++s.errored;
            else if (c.outcomeKind == "skip")             ++s.skipped;
            else if (c.outcomeKind == "unexpected_pass")  ++s.unexpectedPass;
        }
    }

    size_t totalTests = 0, totalFailures = 0, totalErrors = 0, totalSkipped = 0;
    long long totalMicros = 0;
    for (const auto& s : _suites) {
        totalTests    += s.cases.size();
        // Treat UnexpectedPass as failure for JUnit's failure count.
        totalFailures += s.failed + s.unexpectedPass;
        totalErrors   += s.errored;
        totalSkipped  += s.skipped;
        totalMicros   += s.totalMicros;
    }

    _os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    _os << "<testsuites"
        << " name=\"ctltest\""
        << " tests=\""    << totalTests    << "\""
        << " failures=\"" << totalFailures << "\""
        << " errors=\""   << totalErrors   << "\""
        << " skipped=\""  << totalSkipped  << "\""
        << " time=\""     << secondsText(totalMicros) << "\">\n";

    for (const auto& s : _suites) {
        _os << "  <testsuite"
            << " name=\""     << xmlEscape(s.name, true) << "\""
            << " tests=\""    << s.cases.size() << "\""
            << " failures=\"" << (s.failed + s.unexpectedPass) << "\""
            << " errors=\""   << s.errored << "\""
            << " skipped=\""  << s.skipped << "\""
            << " time=\""     << secondsText(s.totalMicros) << "\">\n";

        for (const auto& c : s.cases) {
            _os << "    <testcase"
                << " classname=\"" << xmlEscape(s.name, true) << "\""
                << " name=\""      << xmlEscape(c.id, true) << "\""
                << " time=\""      << secondsText(c.durationMicros) << "\"";
            const bool hasBody =
                   c.outcomeKind == "fail"
                || c.outcomeKind == "error"
                || c.outcomeKind == "skip"
                || c.outcomeKind == "unexpected_pass"
                || !c.description.empty();
            if (!hasBody) { _os << "/>\n"; continue; }
            _os << ">\n";

            if (c.outcomeKind == "fail" || c.outcomeKind == "unexpected_pass") {
                const std::string msg = (c.outcomeKind == "unexpected_pass")
                    ? std::string("known_failure: true but the case passed")
                    : (!c.failures.empty()
                        ? ("at " + c.failures.front().path + ": expected "
                           + c.failures.front().expectedText + ", got "
                           + c.failures.front().gotText)
                        : std::string("case failed with no diagnostics"));
                _os << "      <failure"
                    << " message=\"" << xmlEscape(msg, true) << "\""
                    << " type=\"" << c.outcomeKind << "\">\n";
                for (const auto& d : c.failures) {
                    _os << "        " << xmlEscape(d.path)
                        << ": expected " << xmlEscape(d.expectedText)
                        << ", got "      << xmlEscape(d.gotText);
                    if (d.abs_err != 0.0 || d.rel_err != 0.0 || d.ulp_err != 0) {
                        std::ostringstream es;
                        es << std::scientific << std::setprecision(6)
                           << " [abs=" << d.abs_err << " rel=" << d.rel_err
                           << " ulp=" << d.ulp_err << "]";
                        _os << xmlEscape(es.str());
                    }
                    if (!d.toleranceSource.empty()) {
                        _os << " (tol: " << xmlEscape(d.toleranceSource) << ")";
                    }
                    _os << "\n";
                }
                _os << "      </failure>\n";
            } else if (c.outcomeKind == "error") {
                _os << "      <error"
                    << " message=\"" << xmlEscape(c.errorMessage, true) << "\">"
                    << xmlEscape(c.errorMessage) << "</error>\n";
            } else if (c.outcomeKind == "skip") {
                _os << "      <skipped/>\n";
            }

            if (!c.description.empty()) {
                _os << "      <system-out>" << xmlEscape(c.description)
                    << "</system-out>\n";
            }
            _os << "    </testcase>\n";
        }
        _os << "  </testsuite>\n";
    }

    _os << "</testsuites>\n";
}

} // namespace ctltest
