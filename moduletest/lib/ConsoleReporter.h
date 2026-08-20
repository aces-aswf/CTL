#ifndef CTLTEST_CONSOLE_REPORTER_H
#define CTLTEST_CONSOLE_REPORTER_H

#include "Reporter.h"

#include <ostream>

namespace ctltest {

// Human-readable reporter. Writes to the given ostream (default std::cout).
// Uses ANSI color if the stream is a TTY unless disabled.
class ConsoleReporter: public Reporter {
public:
    explicit ConsoleReporter(std::ostream& os, bool color = true);

    void onSuiteBegin(const std::string& suiteName, size_t totalCases) override;
    void onCaseResult(const CaseResult& r) override;
    void onSuiteEnd(size_t passed, size_t failed, size_t errored,
                    size_t skipped, size_t unexpectedPass) override;

private:
    std::ostream& _os;
    bool _color;
};

} // namespace ctltest

#endif
