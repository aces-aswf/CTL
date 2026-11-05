#include "InlineOracle.h"

#include <algorithm>

namespace ctltest {

OracleVerdict InlineOracle::check(const TestCase& tc,
                                  const std::map<std::string, Value>& outputs) const
{
    OracleVerdict v;
    v.passed = true;

    auto isIgnored = [&](const std::string& name) {
        return std::find(tc.oracle.ignoreOutputs.begin(),
                         tc.oracle.ignoreOutputs.end(),
                         name) != tc.oracle.ignoreOutputs.end();
    };

    // Starting tolerance for every top-level output is the test-case tolerance.
    // compareTyped will further sharpen per sub-path if per_field entries match.
    const Tolerance& baseTol = tc.tolerance;
    const std::string baseSource = "test.tolerance";

    // Every expected key must match an actual output (or return).
    for (const auto& kv : tc.oracle.inlineExpected) {
        const std::string& name = kv.first;
        auto it = outputs.find(name);
        if (it == outputs.end()) {
            Diagnostic d;
            d.path = name;
            d.expectedText = kv.second.describe();
            d.gotText = "<no such output>";
            d.toleranceSource = "test.oracle.inline";
            v.failures.push_back(std::move(d));
            v.passed = false;
            continue;
        }
        compareTyped(name, kv.second, it->second, baseTol, baseSource, v.failures);
    }

    // Load-time rule (plan Sec.Ambiguity resolutions #4): every non-defaulted
    // output must be addressed by inlineExpected OR appear in ignore_outputs.
    // This is the runtime arm of that check.
    for (const auto& kv : outputs) {
        if (tc.oracle.inlineExpected.count(kv.first)) continue;
        if (isIgnored(kv.first)) continue;
        Diagnostic d;
        d.path = kv.first;
        d.expectedText = "<unmentioned; add to oracle.inline or ignore_outputs>";
        d.gotText = kv.second.describe();
        d.toleranceSource = "test.oracle.inline";
        v.failures.push_back(std::move(d));
    }

    if (!v.failures.empty()) v.passed = false;
    return v;
}

} // namespace ctltest
