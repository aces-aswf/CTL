#include "Runner.h"

#include "CsvOracle.h"
#include "CsvTable.h"
#include "ImageCompare.h"
#include "ImageIO.h"
#include "InlineOracle.h"
#include "InterpRunner.h"
#include "Oracle.h"
#include "Reporter.h"
#include "SnapshotOracle.h"

#include <CtlExc.h>
#include <IexBaseExc.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <memory>
#include <set>
#include <sstream>

namespace ctltest {

namespace {

std::unique_ptr<Oracle> makeOracle(const TestCase& tc) {
    switch (tc.oracle.kind) {
      case OracleSpec::Kind::Inline:
          return std::unique_ptr<Oracle>(new InlineOracle());
      case OracleSpec::Kind::Csv:
          return std::unique_ptr<Oracle>(new CsvOracle());
      case OracleSpec::Kind::Exr:
          // Image mode bypasses the single-output Oracle interface -- it has
          // its own pipeline via runImage(). Caller should never hit this.
          return std::unique_ptr<Oracle>();
      case OracleSpec::Kind::Snapshot:
          return std::unique_ptr<Oracle>(new SnapshotOracle());
    }
    return std::unique_ptr<Oracle>(new InlineOracle());
}

// Reject output shapes the varying-lane path can't represent yet. Image and
// Sweep modes always dispatch varying; a StructType output arg hits the
// known-unsupported path in SimdReg. Fail load-early instead of letting the
// interpreter crash mid-batch.
bool rejectVaryingStructOutputs(const InterpRunner::FunctionSignature& sig,
                                OracleVerdict& v,
                                const char* modeLabel)
{
    for (size_t i = 0; i < sig.outputKinds.size(); ++i) {
        if (sig.outputKinds[i] == InterpRunner::TypeKind::Struct) {
            Diagnostic d;
            d.path = sig.outputNames[i];
            d.expectedText = "scalar / array output in " + std::string(modeLabel) + " mode";
            d.gotText      = "struct output (varying struct outputs are not yet supported)";
            d.toleranceSource = "test.signature";
            v.failures.push_back(std::move(d));
            v.passed = false;
        }
    }
    if (sig.hasNonVoidReturn && sig.returnKind == InterpRunner::TypeKind::Struct) {
        Diagnostic d;
        d.path = "return";
        d.expectedText = "scalar / array return in " + std::string(modeLabel) + " mode";
        d.gotText      = "struct return (varying struct outputs are not yet supported)";
        d.toleranceSource = "test.signature";
        v.failures.push_back(std::move(d));
        v.passed = false;
    }
    return v.passed;
}

OracleVerdict runSweep(const TestCase& tc, InterpRunner& interp) {
    OracleVerdict v;
    v.passed = true;

    // Pre-flight the signature. Varying struct outputs cannot be materialized
    // by the current runBatch path; fail fast rather than crash in the bind.
    InterpRunner::FunctionSignature sig;
    try { sig = interp.signature(tc.functionName); }
    catch (const std::exception& e) {
        Diagnostic d;
        d.path = "<signature>";
        d.expectedText = "resolvable CTL function";
        d.gotText = e.what();
        d.toleranceSource = "test.signature";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }
    if (!rejectVaryingStructOutputs(sig, v, "sweep")) return v;

    CsvTable inputsTable = readCsv(tc.sweep.inputsCsvPath);
    if (inputsTable.rows.empty()) {
        Diagnostic d;
        d.path = "<sweep.inputs>";
        d.expectedText = "at least one row";
        d.gotText = "empty file (header only)";
        d.toleranceSource = "test.sweep";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }

    if (tc.oracle.kind != OracleSpec::Kind::Csv) {
        Diagnostic d;
        d.path = "<oracle>";
        d.expectedText = "oracle.csv";
        d.gotText = "sweep mode currently requires oracle.csv";
        d.toleranceSource = "test.oracle";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }

    CsvTable expectedTable = readCsv(tc.oracle.csvPath);

    // Strict column-shape check. Every input column must name an input arg;
    // every expected column must name an output arg or the return alias.
    // strict=true to Diagnostics; strict=false to stderr warnings, keep going.
    {
        std::vector<std::string> extraInputs;
        std::vector<std::string> missingInputs;
        std::vector<std::string> extraExpected;
        const std::set<std::string> inputSet(sig.inputNames.begin(), sig.inputNames.end());
        std::set<std::string> outputSet(sig.outputNames.begin(), sig.outputNames.end());
        if (sig.hasNonVoidReturn) outputSet.insert(tc.returnsName);

        for (const auto& col : inputsTable.columns) {
            if (!inputSet.count(col)) extraInputs.push_back(col);
        }
        const std::set<std::string> inputCols(inputsTable.columns.begin(), inputsTable.columns.end());
        for (const auto& arg : sig.inputNames) {
            if (!inputCols.count(arg)) missingInputs.push_back(arg);
        }
        for (const auto& col : expectedTable.columns) {
            if (!outputSet.count(col)) extraExpected.push_back(col);
        }

        auto reportShape = [&](const char* kind, const std::vector<std::string>& cols) {
            if (cols.empty()) return;
            std::ostringstream os;
            os << kind << ": ";
            for (size_t i = 0; i < cols.size(); ++i) {
                if (i) os << ", ";
                os << cols[i];
            }
            if (tc.sweep.strict) {
                Diagnostic d;
                d.path = "<sweep.columns>";
                d.expectedText = "columns matching the CTL signature";
                d.gotText      = os.str();
                d.toleranceSource = "test.sweep.strict";
                v.failures.push_back(std::move(d));
            } else {
                std::fprintf(stderr,
                             "warning: sweep CSV column mismatch (strict: false): %s\n",
                             os.str().c_str());
            }
        };
        reportShape("extra input columns",    extraInputs);
        reportShape("missing input columns",  missingInputs);
        reportShape("extra expected columns", extraExpected);
        if (!v.failures.empty()) { v.passed = false; return v; }
    }

    if (expectedTable.rows.size() != inputsTable.rows.size()) {
        Diagnostic d;
        d.path = "<oracle.csv>";
        std::ostringstream os;
        os << "row count " << inputsTable.rows.size()
           << " (from sweep.inputs)";
        d.expectedText = os.str();
        std::ostringstream gs;
        gs << expectedTable.rows.size() << " rows";
        d.gotText = gs.str();
        d.toleranceSource = "test.oracle.csv";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }

    const auto inputRows    = tableAsRows(inputsTable);
    const auto expectedRows = tableAsRows(expectedTable);

    const auto resultRows = interp.runBatch(tc.functionName, inputRows, tc.returnsName);
    if (resultRows.size() != inputRows.size()) {
        Diagnostic d;
        d.path = "<sweep.results>";
        std::ostringstream os;
        os << inputRows.size() << " rows";
        d.expectedText = os.str();
        std::ostringstream gs;
        gs << resultRows.size() << " rows";
        d.gotText = gs.str();
        d.toleranceSource = "test.sweep";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }

    const Tolerance& baseTol = tc.tolerance;
    const std::string baseSource = "test.tolerance";

    auto isIgnored = [&](const std::string& name) {
        return std::find(tc.oracle.ignoreOutputs.begin(),
                         tc.oracle.ignoreOutputs.end(),
                         name) != tc.oracle.ignoreOutputs.end();
    };

    for (size_t row = 0; row < resultRows.size(); ++row) {
        const auto& actual   = resultRows[row];
        const auto& expected = expectedRows[row];

        std::ostringstream rowTag;
        rowTag << "row[" << row << "]";
        const std::string rowPrefix = rowTag.str();

        for (const auto& kv : expected) {
            const std::string& name = kv.first;
            if (isIgnored(name)) continue;
            auto it = actual.find(name);
            if (it == actual.end()) {
                Diagnostic d;
                d.path = rowPrefix + "." + name;
                d.expectedText = kv.second.describe();
                d.gotText = "<no such output>";
                d.toleranceSource = "test.oracle.csv";
                v.failures.push_back(std::move(d));
                continue;
            }
            compareTyped(rowPrefix + "." + name, kv.second, it->second,
                         baseTol, baseSource, v.failures);
        }

        // Per-row: every actual output must be addressed by a column or ignored.
        for (const auto& kv : actual) {
            if (expected.count(kv.first)) continue;
            if (isIgnored(kv.first)) continue;
            Diagnostic d;
            d.path = rowPrefix + "." + kv.first;
            d.expectedText = "<unmentioned; add a CSV column or ignore_outputs entry>";
            d.gotText = kv.second.describe();
            d.toleranceSource = "test.oracle.csv";
            v.failures.push_back(std::move(d));
        }
    }

    if (!v.failures.empty()) v.passed = false;
    return v;
}

OracleVerdict runImage(const TestCase& tc, InterpRunner& interp) {
    OracleVerdict v;
    v.passed = true;

    InterpRunner::FunctionSignature sig;
    try { sig = interp.signature(tc.functionName); }
    catch (const std::exception& e) {
        Diagnostic d;
        d.path = "<signature>";
        d.expectedText = "resolvable CTL function";
        d.gotText = e.what();
        d.toleranceSource = "test.signature";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }
    if (!rejectVaryingStructOutputs(sig, v, "image")) return v;

    if (tc.oracle.kind != OracleSpec::Kind::Exr) {
        Diagnostic d;
        d.path = "<oracle>";
        d.expectedText = "oracle.exr";
        d.gotText = "image mode requires oracle.exr";
        d.toleranceSource = "test.oracle";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }

    // 1. Load source image.
    Image src;
    try {
        src = readImage(tc.image.inputExrPath);
    } catch (const ImageIOError& e) {
        Diagnostic d;
        d.path = "<image.input>";
        d.expectedText = "readable EXR";
        d.gotText = e.what();
        d.toleranceSource = "test.image";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }

    // 2. Load reference image up front so channel shape drives the diff.
    Image expected;
    try {
        expected = readImage(tc.oracle.exrRefPath);
    } catch (const ImageIOError& e) {
        Diagnostic d;
        d.path = "<oracle.exr>";
        d.expectedText = "readable reference EXR";
        d.gotText = e.what();
        d.toleranceSource = "test.oracle";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }

    // 3. Resolve channel maps. Empty map to natural pass-through where CTL
    //    arg names coincide with EXR channel names.
    std::map<std::string, std::string> inMap  = tc.image.inputChannelMap;
    std::map<std::string, std::string> outMap = tc.image.outputChannelMap;

    if (inMap.empty()) {
        for (const auto& kv : src.channels) inMap.emplace(kv.first, kv.first);
    }
    if (outMap.empty()) {
        for (const auto& kv : expected.channels) outMap.emplace(kv.first, kv.first);
    }

    // 4. Validate source channels exist.
    for (const auto& kv : inMap) {
        if (!src.channels.count(kv.second)) {
            Diagnostic d;
            d.path = std::string("<image.input_channels[") + kv.first + "]>";
            d.expectedText = std::string("source channel '") + kv.second + "'";
            d.gotText = "missing from input EXR";
            d.toleranceSource = "test.image";
            v.failures.push_back(std::move(d));
            v.passed = false;
        }
    }
    if (!v.passed) return v;

    // 5. Build per-pixel row maps. Each row supplies every CTL input named in
    //    inMap; inputs not present in inMap fall back to default value.
    const size_t N = src.pixelCount();
    std::vector<std::map<std::string, Value>> rows;
    rows.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        std::map<std::string, Value> row;
        for (const auto& kv : inMap) {
            const std::string& argName = kv.first;
            const std::string& chName  = kv.second;
            row.emplace(argName, Value::makeFloat(src.channels[chName][i]));
        }
        rows.push_back(std::move(row));
    }

    // 6. Dispatch. runBatch handles lane batching up to maxSamples() internally.
    std::vector<std::map<std::string, Value>> results;
    try {
        results = interp.runBatch(tc.functionName, rows, tc.returnsName);
    } catch (const RunError& e) {
        Diagnostic d;
        d.path = "<image.runBatch>";
        d.expectedText = "successful per-pixel dispatch";
        d.gotText = e.what();
        d.toleranceSource = "test.image";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }
    if (results.size() != N) {
        Diagnostic d;
        d.path = "<image.runBatch>";
        std::ostringstream os; os << N << " rows";
        d.expectedText = os.str();
        std::ostringstream gs; gs << results.size() << " rows";
        d.gotText = gs.str();
        d.toleranceSource = "test.image";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }

    // 7. Collect output channels into actual Image.
    Image actual;
    actual.width  = src.width;
    actual.height = src.height;
    for (const auto& kv : outMap) {
        actual.channels[kv.first].assign(N, 0.0f);
    }
    // First row drives sanity: every mapped CTL output must be present.
    if (!results.empty()) {
        for (const auto& kv : outMap) {
            if (!results[0].count(kv.second)) {
                Diagnostic d;
                d.path = std::string("<image.output_channels[") + kv.first + "]>";
                d.expectedText = std::string("CTL output '") + kv.second + "'";
                d.gotText = "missing from function result";
                d.toleranceSource = "test.image";
                v.failures.push_back(std::move(d));
                v.passed = false;
            }
        }
        if (!v.passed) return v;
    }
    for (size_t i = 0; i < N; ++i) {
        const auto& r = results[i];
        for (const auto& kv : outMap) {
            const std::string& chName  = kv.first;
            const std::string& argName = kv.second;
            auto it = r.find(argName);
            if (it == r.end()) continue;   // already diagnosed above
            actual.channels[chName][i] = static_cast<float>(it->second.f);
        }
    }

    // 8. Compare + emit failure artifacts.
    ImageCompareResult cmp = compareImages(expected, actual, tc.tolerance,
                                           tc.oracle.max_failing_pixels);
    v.passed = cmp.passed;
    for (auto& d : cmp.failures) v.failures.push_back(std::move(d));

    std::ostringstream summary;
    summary << cmp.mismatchCount << " mismatches across "
            << static_cast<size_t>(expected.width) * expected.height
            << " pixel*channel samples";
    v.summary = summary.str();

    if (!cmp.passed) {
        try {
            writeFailureArtifacts(tc.oracle.exrRefPath, expected, actual);
        } catch (const ImageIOError& e) {
            Diagnostic d;
            d.path = "<failure_artifacts>";
            d.expectedText = "writable sidecars";
            d.gotText = e.what();
            d.toleranceSource = "test.image";
            v.failures.push_back(std::move(d));
        }
    }

    return v;
}

OracleVerdict runCtlNative(const TestCase& tc, InterpRunner& interp) {
    OracleVerdict v;
    v.passed = true;

    std::vector<TestAssertion> asserts =
        interp.runCtlNative(tc.functionName);

    if (asserts.empty()) {
        // An empty test is suspicious -- the author probably forgot to wire
        // testkit calls into the function body. Surface it as a failure so
        // it can't silently green.
        Diagnostic d;
        d.path         = "<ctl_native>";
        d.expectedText = "at least one testkit::* assertion";
        d.gotText      = "no assertions recorded";
        d.toleranceSource = "test.mode.ctl_native";
        v.failures.push_back(std::move(d));
        v.passed = false;
        return v;
    }

    size_t passCount = 0;
    for (size_t i = 0; i < asserts.size(); ++i) {
        const TestAssertion& a = asserts[i];
        if (a.passed) { ++passCount; continue; }

        Diagnostic d;
        std::ostringstream path;
        path << "assertion[" << i << "]";
        d.path            = path.str();
        d.toleranceSource = "test.ctl_native.testkit";

        switch (a.kind) {
          case TestAssertion::Kind::Fail:
            d.expectedText = "(no failure)";
            d.gotText      = a.message.empty() ? "testkit::fail()" : a.message;
            break;
          case TestAssertion::Kind::ExpectTrue:
            d.expectedText = "true";
            d.gotText      = "false";
            break;
          case TestAssertion::Kind::ExpectNearF: {
            std::ostringstream es; es << a.expected;
            std::ostringstream gs; gs << a.actual;
            d.expectedText = es.str();
            d.gotText      = gs.str();
            d.abs_err      = a.abs_err;
            Tolerance applied;
            applied.abs    = a.abs_tol;
            d.applied      = applied;
            break;
          }
        }
        v.failures.push_back(std::move(d));
    }

    if (!v.failures.empty()) {
        v.passed = false;
    }
    std::ostringstream sum;
    sum << passCount << "/" << asserts.size() << " assertions passed";
    v.summary = sum.str();
    return v;
}

CaseResult runOne(const TestCase& tc) {
    CaseResult r;
    r.testcase = &tc;
    auto t0 = std::chrono::steady_clock::now();

    try {
        InterpRunner interp;
        if (!tc.modulePaths.empty()) interp.setModulePaths(tc.modulePaths);
        interp.loadModules(tc.extraModules);

        if (tc.mode == TestCase::Mode::Sweep) {
            r.verdict = runSweep(tc, interp);
        } else if (tc.mode == TestCase::Mode::Image) {
            r.verdict = runImage(tc, interp);
        } else if (tc.mode == TestCase::Mode::CtlNative) {
            r.verdict = runCtlNative(tc, interp);
        } else {
            auto outputs = interp.run(tc.functionName, tc.namedInputs, tc.returnsName);

            // Validate per_field tolerance keys against the shape of the
            // computed outputs. Non-FP leaves or unresolved paths are load-time
            // errors that turn into up-front Diagnostics on first run.
            OracleVerdict tolV;
            Value shape = Value::makeMap(outputs);
            validateTolerancePaths("", tc.tolerance, shape, tolV.failures);
            if (!tolV.failures.empty()) {
                tolV.passed = false;
                r.verdict = tolV;
            } else {
                auto oracle = makeOracle(tc);
                r.verdict = oracle->check(tc, outputs);
            }
        }

        if (r.verdict.passed) {
            r.outcome = tc.knownFailure
                ? CaseResult::Outcome::UnexpectedPass
                : CaseResult::Outcome::Pass;
        } else {
            r.outcome = tc.knownFailure
                ? CaseResult::Outcome::Pass
                : CaseResult::Outcome::Fail;
        }
    } catch (const Iex::BaseExc& e) {
        r.outcome = CaseResult::Outcome::Error;
        r.errorMessage = std::string("CTL runtime: ") + e.what();
    } catch (const std::exception& e) {
        r.outcome = CaseResult::Outcome::Error;
        r.errorMessage = e.what();
    } catch (...) {
        r.outcome = CaseResult::Outcome::Error;
        r.errorMessage = "unknown exception";
    }

    auto t1 = std::chrono::steady_clock::now();
    r.duration = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);
    return r;
}

} // namespace

RunCounts runSuite(const Suite& suite, Reporter& reporter) {
    reporter.onSuiteBegin(suite.name, suite.tests.size());

    RunCounts c;
    for (const auto& tc : suite.tests) {
        CaseResult r = runOne(tc);
        switch (r.outcome) {
          case CaseResult::Outcome::Pass:            ++c.passed; break;
          case CaseResult::Outcome::Fail:            ++c.failed; break;
          case CaseResult::Outcome::Error:           ++c.errored; break;
          case CaseResult::Outcome::Skipped:         ++c.skipped; break;
          case CaseResult::Outcome::UnexpectedPass:  ++c.unexpectedPass; break;
        }
        reporter.onCaseResult(r);
    }

    reporter.onSuiteEnd(c.passed, c.failed, c.errored, c.skipped, c.unexpectedPass);
    return c;
}

} // namespace ctltest
