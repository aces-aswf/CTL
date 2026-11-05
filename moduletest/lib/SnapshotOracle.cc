#include "SnapshotOracle.h"

#include "Oracle.h"
#include "ValueIO.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace ctltest {

namespace {

bool envFlag(const char* name) {
    const char* v = std::getenv(name);
    if (!v) return false;
    // Accept 1 / true / on / yes (case-insensitive). Anything else == off.
    std::string s(v);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s == "1" || s == "true" || s == "on" || s == "yes" || s == "force";
}

Diagnostic makeDiag(const std::string& path,
                    const std::string& expected,
                    const std::string& got,
                    const std::string& source)
{
    Diagnostic d;
    d.path = path;
    d.expectedText = expected;
    d.gotText = got;
    d.toleranceSource = source;
    return d;
}

} // namespace

OracleVerdict SnapshotOracle::check(const TestCase& tc,
                                    const std::map<std::string, Value>& outputs) const
{
    OracleVerdict v;
    v.passed = true;

    if (tc.oracle.snapshotPath.empty()) {
        v.failures.push_back(makeDiag("<oracle.snapshot>", "path",
                                      "no snapshot path configured",
                                      "test.oracle"));
        v.passed = false;
        return v;
    }

    const bool envAllowsUpdate = envFlag("CTL_TEST_UPDATE_SNAPSHOTS");
    const bool envAllowsNew    = envFlag("CTL_TEST_ALLOW_NEW_SNAPSHOTS");
    const bool testOptsIn      = tc.oracle.snapshotWritable;
    const bool fileExists      = fs::exists(tc.oracle.snapshotPath);

    auto isIgnored = [&](const std::string& name) {
        return std::find(tc.oracle.ignoreOutputs.begin(),
                         tc.oracle.ignoreOutputs.end(),
                         name) != tc.oracle.ignoreOutputs.end();
    };

    // Gate-check for first-time record.
    if (!fileExists) {
        const bool canRecord = envAllowsUpdate && envAllowsNew && testOptsIn;
        if (!canRecord) {
            v.failures.push_back(makeDiag(
                "<oracle.snapshot>",
                "existing snapshot OR (CTL_TEST_UPDATE_SNAPSHOTS=1 AND "
                "CTL_TEST_ALLOW_NEW_SNAPSHOTS=1 AND snapshot.writable: true)",
                "snapshot not yet recorded (" + tc.oracle.snapshotPath + ")",
                "test.oracle.snapshot"));
            v.passed = false;
            return v;
        }
        // Gates pass: record a fresh snapshot of the current outputs,
        // excluding anything the test says to ignore.
        std::map<std::string, Value> toWrite;
        for (const auto& kv : outputs) {
            if (!isIgnored(kv.first)) toWrite.emplace(kv.first, kv.second);
        }
        try {
            saveValueMap(tc.oracle.snapshotPath, toWrite);
        } catch (const ValueIOError& e) {
            v.failures.push_back(makeDiag("<oracle.snapshot>",
                                          "write new snapshot",
                                          e.what(), "test.oracle.snapshot"));
            v.passed = false;
            return v;
        }
        v.summary = "recorded new snapshot at " + tc.oracle.snapshotPath;
        return v;
    }

    // File exists: load and compare.
    std::map<std::string, Value> expected;
    try {
        expected = loadValueMap(tc.oracle.snapshotPath);
    } catch (const ValueIOError& e) {
        v.failures.push_back(makeDiag("<oracle.snapshot>",
                                      "readable YAML map",
                                      e.what(), "test.oracle.snapshot"));
        v.passed = false;
        return v;
    }

    // Same shape as InlineOracle: every expected must match an output; every
    // non-ignored output must be addressed by the snapshot.
    const Tolerance& baseTol = tc.tolerance;
    const std::string baseSource = "test.tolerance";

    for (const auto& kv : expected) {
        const std::string& name = kv.first;
        auto it = outputs.find(name);
        if (it == outputs.end()) {
            v.failures.push_back(makeDiag(name, kv.second.describe(),
                                          "<no such output>",
                                          "test.oracle.snapshot"));
            continue;
        }
        compareTyped(name, kv.second, it->second, baseTol, baseSource, v.failures);
    }
    for (const auto& kv : outputs) {
        if (expected.count(kv.first)) continue;
        if (isIgnored(kv.first)) continue;
        v.failures.push_back(makeDiag(kv.first,
            "<unmentioned in snapshot; update with CTL_TEST_UPDATE_SNAPSHOTS=1 + snapshot.writable, or add to ignore_outputs>",
            kv.second.describe(), "test.oracle.snapshot"));
    }

    if (!v.failures.empty()) v.passed = false;

    // Overwrite if gates pass and there was a mismatch.
    if (!v.passed && envAllowsUpdate && testOptsIn) {
        std::map<std::string, Value> toWrite;
        for (const auto& kv : outputs) {
            if (!isIgnored(kv.first)) toWrite.emplace(kv.first, kv.second);
        }
        try {
            saveValueMap(tc.oracle.snapshotPath, toWrite);
            v.passed = true;
            v.failures.clear();
            v.summary = "overwrote snapshot at " + tc.oracle.snapshotPath;
        } catch (const ValueIOError& e) {
            v.failures.push_back(makeDiag("<oracle.snapshot>",
                                          "overwrite snapshot",
                                          e.what(), "test.oracle.snapshot"));
            // keep v.passed == false
        }
    }

    return v;
}

} // namespace ctltest
