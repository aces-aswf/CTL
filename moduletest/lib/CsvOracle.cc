#include "CsvOracle.h"

#include "CsvTable.h"

#include <algorithm>

namespace ctltest {

OracleVerdict CsvOracle::check(const TestCase& tc,
                               const std::map<std::string, Value>& outputs) const
{
    OracleVerdict v;
    v.passed = true;

    auto isIgnored = [&](const std::string& name) {
        return std::find(tc.oracle.ignoreOutputs.begin(),
                         tc.oracle.ignoreOutputs.end(),
                         name) != tc.oracle.ignoreOutputs.end();
    };

    CsvTable table;
    try {
        table = readCsv(tc.oracle.csvPath);
    } catch (const CsvError& e) {
        Diagnostic d;
        d.path = "<oracle.csv>";
        d.expectedText = tc.oracle.csvPath;
        d.gotText = std::string("failed to load: ") + e.what();
        d.toleranceSource = "test.oracle.csv";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }

    if (table.rows.empty()) {
        Diagnostic d;
        d.path = "<oracle.csv>";
        d.expectedText = "at least one data row";
        d.gotText = "empty file (header only)";
        d.toleranceSource = "test.oracle.csv";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }

    const std::vector<std::map<std::string, Value>> rows = tableAsRows(table);
    const auto& expected = rows.front();

    const Tolerance& baseTol = tc.tolerance;
    const std::string baseSource = "test.tolerance";

    for (const auto& kv : expected) {
        const std::string& name = kv.first;
        if (isIgnored(name)) continue;
        auto it = outputs.find(name);
        if (it == outputs.end()) {
            Diagnostic d;
            d.path = name;
            d.expectedText = kv.second.describe();
            d.gotText = "<no such output>";
            d.toleranceSource = "test.oracle.csv";
            v.failures.push_back(std::move(d));
            v.passed = false;
            continue;
        }
        compareTyped(name, kv.second, it->second, baseTol, baseSource, v.failures);
    }

    // Every non-ignored output must be covered by a column; mirror InlineOracle's
    // "no silent gaps" rule.
    for (const auto& kv : outputs) {
        if (expected.count(kv.first)) continue;
        if (isIgnored(kv.first)) continue;
        Diagnostic d;
        d.path = kv.first;
        d.expectedText = "<unmentioned; add a CSV column or ignore_outputs entry>";
        d.gotText = kv.second.describe();
        d.toleranceSource = "test.oracle.csv";
        v.failures.push_back(std::move(d));
    }

    if (!v.failures.empty()) v.passed = false;
    return v;
}

} // namespace ctltest
