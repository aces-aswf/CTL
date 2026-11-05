// ctltest -- standalone CLI for ctltest, installed alongside ctlrender.
//
// Usage:
//   ctltest [options] <path> [<path> ...]
//
// Each <path> is either a YAML suite file or a directory. Directories are
// walked for *.yaml/*.yml; each file is loaded as its own suite. Exits
// with a non-zero status if any case did not pass.
//
// Options:
//   --reporter {console|tap|junit}   Default: console
//   --output FILE                    Write reporter output to FILE. Default: stdout.
//   --filter GLOB                    Include only cases whose id matches GLOB (shell-style).
//   --no-color                       Disable ANSI color in console reporter.
//   --update-snapshots[=force]       Write snapshot oracles. `force` also overwrites existing.
//   --allow-new-snapshots            Record missing snapshots on first run without failing.
//   -h, --help                       This help text.
//
// --update-snapshots and --allow-new-snapshots set the CTL_TEST_UPDATE_SNAPSHOTS
// and CTL_TEST_ALLOW_NEW_SNAPSHOTS env vars that SnapshotOracle reads directly;
// the per-test `writable: true` gate in YAML still has to opt in.

#include "CaseModel.h"
#include "ConsoleReporter.h"
#include "JUnitReporter.h"
#include "Reporter.h"
#include "Runner.h"
#include "TapReporter.h"
#include "YamlLoader.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// setenv is POSIX. MSVC provides _putenv_s instead, which takes the same
// name and value but has no overwrite flag; it always overwrites, which is
// what every call here wants.
namespace {
int ctlSetEnv(const char* name, const char* value)
{
#ifdef _WIN32
    return _putenv_s(name, value);
#else
    return setenv(name, value, 1);
#endif
}
}  // namespace


namespace fs = std::filesystem;

namespace {

enum class ReporterKind { Console, Tap, JUnit };

struct Options {
    ReporterKind reporter = ReporterKind::Console;
    std::string  outputPath;
    std::string  filter;
    bool         color = true;
    std::vector<std::string> inputs;
};

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s [options] <path> [<path> ...]\n"
        "  <path> is a YAML suite file or a directory to walk for *.yaml/*.yml.\n"
        "options:\n"
        "  --reporter {console|tap|junit}   default: console\n"
        "  --output FILE                    write reporter output to FILE\n"
        "  --filter GLOB                    only run cases whose id matches GLOB\n"
        "  --no-color                       disable ANSI color (console)\n"
        "  --update-snapshots[=force]       write snapshot oracles (force overwrites existing)\n"
        "  --allow-new-snapshots            record missing snapshots without failing the run\n"
        "  -h, --help                       show this help\n",
        argv0);
}

bool matchesGlob(const std::string& s, const std::string& pat) {
    // Minimal shell-style matcher: supports '*' and '?'. No brackets, no escaping.
    auto match = [](const std::string& s, const std::string& p) {
        const size_t N = s.size(), M = p.size();
        size_t i = 0, j = 0, star = std::string::npos, back = 0;
        while (i < N) {
            if (j < M && (p[j] == '?' || p[j] == s[i])) { ++i; ++j; continue; }
            if (j < M && p[j] == '*') { star = j++; back = i; continue; }
            if (star != std::string::npos) { j = star + 1; i = ++back; continue; }
            return false;
        }
        while (j < M && p[j] == '*') ++j;
        return j == M;
    };
    return match(s, pat);
}

int parseArgs(int argc, char** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto needsValue = [&](int& idx) -> const char* {
            if (idx + 1 >= argc) {
                std::fprintf(stderr, "option %s requires a value\n", argv[idx]);
                return nullptr;
            }
            return argv[++idx];
        };
        if (!std::strcmp(a, "--reporter")) {
            const char* v = needsValue(i); if (!v) return 2;
            std::string s = v;
            if      (s == "console") out.reporter = ReporterKind::Console;
            else if (s == "tap")     out.reporter = ReporterKind::Tap;
            else if (s == "junit")   out.reporter = ReporterKind::JUnit;
            else { std::fprintf(stderr, "unknown reporter '%s'\n", v); return 2; }
        } else if (!std::strcmp(a, "--output")) {
            const char* v = needsValue(i); if (!v) return 2;
            out.outputPath = v;
        } else if (!std::strcmp(a, "--filter")) {
            const char* v = needsValue(i); if (!v) return 2;
            out.filter = v;
        } else if (!std::strcmp(a, "--no-color")) {
            out.color = false;
        } else if (!std::strcmp(a, "--update-snapshots")) {
            ctlSetEnv("CTL_TEST_UPDATE_SNAPSHOTS", "1");
        } else if (!std::strcmp(a, "--update-snapshots=force")) {
            ctlSetEnv("CTL_TEST_UPDATE_SNAPSHOTS", "force");
        } else if (!std::strcmp(a, "--allow-new-snapshots")) {
            ctlSetEnv("CTL_TEST_ALLOW_NEW_SNAPSHOTS", "1");
        } else if (!std::strcmp(a, "-h") || !std::strcmp(a, "--help")) {
            usage(argv[0]);
            return 1;
        } else if (a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", a);
            usage(argv[0]);
            return 2;
        } else {
            out.inputs.emplace_back(a);
        }
    }
    if (out.inputs.empty()) { usage(argv[0]); return 2; }
    return 0;
}

std::vector<std::string> expandInputs(const std::vector<std::string>& inputs) {
    std::vector<std::string> out;
    for (const auto& in : inputs) {
        fs::path p(in);
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            for (auto it = fs::recursive_directory_iterator(p, ec);
                 it != fs::recursive_directory_iterator();
                 it.increment(ec))
            {
                if (ec) break;
                // `snapshots/` holds oracle outputs, not suites -- don't descend.
                if (it->is_directory(ec) && it->path().filename() == "snapshots") {
                    it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file(ec)) continue;
                const std::string ext = it->path().extension().string();
                if (ext == ".yaml" || ext == ".yml") {
                    out.push_back(it->path().string());
                }
            }
        } else {
            out.push_back(in);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// Reporter wrapper that filters TestCases before handing them to the inner reporter.
class FilteringReporter: public ctltest::Reporter {
public:
    FilteringReporter(ctltest::Reporter& inner, const std::string& glob)
        : _inner(inner), _glob(glob) {}

    void onSuiteBegin(const std::string& s, size_t n) override {
        _inner.onSuiteBegin(s, n);
    }
    void onCaseResult(const ctltest::CaseResult& r) override {
        if (!_glob.empty() && r.testcase && !matchesGlob(r.testcase->id, _glob))
            return;
        _inner.onCaseResult(r);
    }
    void onSuiteEnd(size_t p, size_t f, size_t e, size_t s, size_t u) override {
        _inner.onSuiteEnd(p, f, e, s, u);
    }

private:
    ctltest::Reporter& _inner;
    std::string        _glob;
};

} // namespace

int main(int argc, char** argv) {
    Options opts;
    if (int rc = parseArgs(argc, argv, opts); rc != 0) {
        return rc == 1 ? 0 : rc;
    }

    // Destination stream.
    std::ofstream fileOut;
    std::ostream* osp = &std::cout;
    if (!opts.outputPath.empty()) {
        fileOut.open(opts.outputPath);
        if (!fileOut) {
            std::fprintf(stderr, "could not open output file '%s'\n", opts.outputPath.c_str());
            return 2;
        }
        osp = &fileOut;
    }

    std::unique_ptr<ctltest::Reporter> reporter;
    ctltest::TapReporter*   tap   = nullptr;
    ctltest::JUnitReporter* junit = nullptr;
    switch (opts.reporter) {
      case ReporterKind::Console:
          reporter.reset(new ctltest::ConsoleReporter(*osp, opts.color));
          break;
      case ReporterKind::Tap: {
          auto* r = new ctltest::TapReporter(*osp);
          tap = r;
          reporter.reset(r);
          break;
      }
      case ReporterKind::JUnit: {
          auto* r = new ctltest::JUnitReporter(*osp);
          junit = r;
          reporter.reset(r);
          break;
      }
    }

    // Expand paths.
    std::vector<std::string> files = expandInputs(opts.inputs);
    if (files.empty()) {
        std::fprintf(stderr, "no YAML suite files found in inputs\n");
        return 2;
    }

    // Run suites.
    ctltest::RunCounts totals;
    for (const auto& path : files) {
        ctltest::Suite suite;
        try {
            suite = ctltest::loadSuite(path);
        } catch (const ctltest::LoadError& e) {
            std::fprintf(stderr, "load error in %s: %s\n", path.c_str(), e.what());
            ++totals.errored;
            continue;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "load error in %s: %s\n", path.c_str(), e.what());
            ++totals.errored;
            continue;
        }

        FilteringReporter filt(*reporter, opts.filter);
        ctltest::Reporter& run = opts.filter.empty()
            ? static_cast<ctltest::Reporter&>(*reporter)
            : static_cast<ctltest::Reporter&>(filt);

        const ctltest::RunCounts c = ctltest::runSuite(suite, run);
        totals.passed         += c.passed;
        totals.failed         += c.failed;
        totals.errored        += c.errored;
        totals.skipped        += c.skipped;
        totals.unexpectedPass += c.unexpectedPass;
    }

    if (tap)   tap->finalizePlan();
    if (junit) junit->finalize();

    return totals.nonPassing() == 0 ? 0 : 1;
}
