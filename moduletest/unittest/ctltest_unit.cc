// ctltest_unit -- C++ unit tests for ctltest_core's public API. Separate
// from the self-test YAML suites under moduletest/tests/selftest/, which
// exercise the framework end-to-end against real CTL modules. This binary
// drives the pure-library pieces directly:
//
//   * Value / Tolerance construction and merge semantics
//   * Oracle comparison (abs / rel / ulp, half rounding, tree walk)
//   * Tolerance path validation
//   * CsvTable parsing (quoting, CRLF, comments) + cellToValue heuristics
//   * YamlLoader happy/sad paths
//   * Marshal round-trip across every scalar kind on flat + nested types
//     (exercises the v1.1 CtlType::childElementV fix end-to-end)
//
// Written as a single hand-rolled harness that matches the style of
// unittest/IlmCtl/main.cpp -- one `TEST(name)` per suite, early abort on
// CHECK failure so regressions are visible in both Debug and Release.

#include "CaseModel.h"
#include "ConsoleReporter.h"
#include "CsvTable.h"
#include "ImageCompare.h"
#include "ImageIO.h"
#include "InlineOracle.h"
#include "CsvOracle.h"
#include "InterpRunner.h"
#include "JUnitReporter.h"
#include "Marshal.h"
#include "Oracle.h"
#include "Reporter.h"
#include "Result.h"
#include "Runner.h"
#include "SnapshotOracle.h"
#include "TapReporter.h"
#include "TestKit.h"
#include "ValueIO.h"
#include "YamlLoader.h"

#include <half.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
// dup/dup2/close for the stdout capture below.  POSIX puts them in
// <unistd.h>; MSVC puts the same three in <io.h> under underscore-prefixed
// names, so give them one spelling here rather than at each call site.
#ifdef _WIN32
  #include <io.h>
  #include <process.h>
  namespace {
      inline int ctlDup (int fd)          { return _dup (fd); }
      inline int ctlDup2 (int a, int b)   { return _dup2 (a, b); }
      inline int ctlClose (int fd)        { return _close (fd); }
      // _putenv_s with an empty value removes the variable.
      inline void ctlSetEnv (const char* n, const char* v) { _putenv_s (n, v); }
      inline void ctlUnsetEnv (const char* n)              { _putenv_s (n, ""); }
      inline FILE* ctlPopen (const char* c, const char* m) { return _popen (c, m); }
      inline int   ctlPclose (FILE* f)                     { return _pclose (f); }
      inline int   ctlGetPid ()                            { return _getpid (); }
      // _pclose already hands back the child's exit code.
      inline int   ctlExitCode (int rc)                    { return rc; }
  }
#else
  #include <unistd.h>
  #include <sys/wait.h>
  namespace {
      inline int ctlDup (int fd)          { return dup (fd); }
      inline int ctlDup2 (int a, int b)   { return dup2 (a, b); }
      inline int ctlClose (int fd)        { return close (fd); }
      inline void ctlSetEnv (const char* n, const char* v) { ::setenv (n, v, 1); }
      inline void ctlUnsetEnv (const char* n)              { ::unsetenv (n); }
      inline FILE* ctlPopen (const char* c, const char* m) { return ::popen (c, m); }
      inline int   ctlPclose (FILE* f)                     { return ::pclose (f); }
      inline int   ctlGetPid ()                            { return ::getpid (); }
      inline int   ctlExitCode (int rc)
      { return WIFEXITED (rc) ? WEXITSTATUS (rc) : -1; }
  }
#endif
#include <vector>

using namespace ctltest;

namespace {

int g_failures = 0;
const char* g_current = "<none>";

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                         "FAIL [%s] %s:%d  CHECK(%s)\n",                   \
                         g_current, __FILE__, __LINE__, #cond);            \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                     \
    do {                                                                   \
        auto _a = (a); auto _b = (b);                                      \
        if (!((_a) == (_b))) {                                             \
            std::ostringstream _os;                                        \
            _os << "FAIL [" << g_current << "] " << __FILE__ << ":"        \
                << __LINE__ << "  CHECK_EQ(" #a ", " #b ")  got=" << _a    \
                << " want=" << _b << "\n";                                 \
            std::fprintf(stderr, "%s", _os.str().c_str());                 \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

struct Section {
    explicit Section(const char* name) { g_current = name; std::cout << "  " << name << std::endl; }
    ~Section() { g_current = "<none>"; }
};

// ----------------------------------------------------------- harness helpers

// Substring assertion. Used to keep reporter-output format checks readable
// without locking down full strings.
#define CHECK_CONTAINS(haystack, needle)                                   \
    do {                                                                   \
        const std::string _h = (haystack);                                 \
        const std::string _n = (needle);                                   \
        if (_h.find(_n) == std::string::npos) {                            \
            std::fprintf(stderr,                                           \
                "FAIL [%s] %s:%d  CHECK_CONTAINS missing %s in:\n%s\n",    \
                g_current, __FILE__, __LINE__, _n.c_str(), _h.c_str());    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                              \
    do {                                                                   \
        const double _a = static_cast<double>(a);                          \
        const double _b = static_cast<double>(b);                          \
        const double _e = static_cast<double>(eps);                        \
        if (std::fabs(_a - _b) > _e) {                                     \
            std::fprintf(stderr,                                           \
                "FAIL [%s] %s:%d  CHECK_NEAR(%s, %s, %s)  |%.17g - %.17g| > %.17g\n", \
                g_current, __FILE__, __LINE__,                             \
                #a, #b, #eps, _a, _b, _e);                                 \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// RAII redirect of a stdio FILE* to a temp file; capture the bytes that would
// have been written to stdout/stderr by code under test. The dtor restores
// the original fd. Used by every reporter and CLI test.
class StreamCapture {
public:
    explicit StreamCapture(int fd) : _fd(fd) {
        std::fflush(_fd == 1 ? stdout : stderr);
        _saved = ctlDup(_fd);
        _path  = (std::filesystem::temp_directory_path() / "ctltest_capture").string()
               + "_" + std::to_string(ctlGetPid())
               + "_" + std::to_string(++_counter) + ".txt";
        _file  = std::fopen(_path.c_str(), "w+");
        if (_file) ctlDup2(fileno(_file), _fd);
    }

    ~StreamCapture() {
        if (_done) return;
        finish();
    }

    std::string str() {
        if (_done) return _content;
        finish();
        return _content;
    }

private:
    void finish() {
        std::fflush(_fd == 1 ? stdout : stderr);
        if (_saved >= 0) { ctlDup2(_saved, _fd); ctlClose(_saved); _saved = -1; }
        if (_file) {
            std::fseek(_file, 0, SEEK_END);
            const long sz = std::ftell(_file);
            std::rewind(_file);
            if (sz > 0) {
                _content.resize(static_cast<size_t>(sz));
                size_t n = std::fread(&_content[0], 1, _content.size(), _file);
                _content.resize(n);
            }
            std::fclose(_file); _file = nullptr;
        }
        std::filesystem::remove(_path);
        _done = true;
    }

    int _fd;
    int _saved = -1;
    FILE* _file = nullptr;
    std::string _path;
    std::string _content;
    bool _done = false;
    static int _counter;
};
int StreamCapture::_counter = 0;

struct CaptureStdout : StreamCapture { CaptureStdout(): StreamCapture(1) {} };
struct CaptureStderr : StreamCapture { CaptureStderr(): StreamCapture(2) {} };

// RAII scratch directory under build/Testing/Temporary. Deletes itself on dtor.
// Used by SnapshotOracle, ImageIO, writeFailureArtifacts, CSV oracle tests.
class TempDir {
public:
    TempDir() {
        namespace fs = std::filesystem;
        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_int_distribution<int> d(0, 0x7fffffff);
        for (int i = 0; i < 32; ++i) {
            std::ostringstream os;
            os << (fs::temp_directory_path() / "ctltest_unit-").string()
                   << ctlGetPid() << "-" << d(rng);
            std::error_code ec;
            if (fs::create_directory(os.str(), ec)) {
                _path = os.str();
                return;
            }
        }
        // Fallback: just use a known path; tests will fail visibly.
        _path = (std::filesystem::temp_directory_path() / "ctltest_unit-fallback").string();
        std::filesystem::create_directory(_path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(_path, ec);
    }
    const std::string& path() const { return _path; }
    std::string operator/(const std::string& name) const { return _path + "/" + name; }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

private:
    std::string _path;
};

// RAII guard around an environment variable. Captures previous value (or
// absence) on construction and restores on dtor. Used by snapshot-oracle gate
// tests so env mutations don't leak between cases.
class EnvGuard {
public:
    EnvGuard(const char* name, const char* value) : _name(name), _had(false) {
        const char* prev = std::getenv(name);
        if (prev) { _had = true; _prev = prev; }
        if (value) ctlSetEnv(name, value);
        else       ctlUnsetEnv(name);
    }
    ~EnvGuard() {
        if (_had) ctlSetEnv(_name.c_str(), _prev.c_str());
        else      ctlUnsetEnv(_name.c_str());
    }
    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;
private:
    std::string _name;
    bool _had;
    std::string _prev;
};

// ---------------------------------------------------------------- Value

void testValue()
{
    Section s("Value");

    Value b = Value::makeBool(true);
    CHECK(b.kind == Value::Kind::Bool);
    CHECK(b.b == true);

    Value i = Value::makeInt(-7);
    CHECK(i.kind == Value::Kind::Int);
    CHECK_EQ(i.i, -7);

    Value u = Value::makeUInt(42u);
    CHECK(u.kind == Value::Kind::UInt);
    CHECK_EQ(u.u, 42u);

    Value f = Value::makeFloat(0.25);
    CHECK(f.kind == Value::Kind::Float);
    CHECK(f.f == 0.25);

    Value h = Value::makeHalf(0.5);
    CHECK(h.kind == Value::Kind::Half);
    CHECK(h.f == 0.5);

    Value str = Value::makeString("hello");
    CHECK(str.kind == Value::Kind::String);
    CHECK(str.s == "hello");

    Value seq = Value::makeSeq({ Value::makeFloat(1.0), Value::makeFloat(2.0) });
    CHECK(seq.kind == Value::Kind::Seq);
    CHECK_EQ(seq.seq.size(), size_t(2));

    Value mp = Value::makeMap({ {"x", Value::makeFloat(1.0)},
                                {"y", Value::makeFloat(2.0)} });
    CHECK(mp.kind == Value::Kind::Map);
    CHECK_EQ(mp.map.size(), size_t(2));

    // describe() must be non-empty for leaves.
    CHECK(!f.describe().empty());
    CHECK(!str.describe().empty());
    CHECK(!seq.describe().empty());
}

// ---------------------------------------------------------------- Tolerance::merge

void testToleranceMerge()
{
    Section s("Tolerance::merge");

    Tolerance base;
    base.abs = 1.0e-5;
    base.rel = 1.0e-4;

    Tolerance override1;
    override1.abs = 5.0e-4;

    // Override replaces fields it sets; leaves the rest from base.
    Tolerance merged = Tolerance::merge(base, override1);
    CHECK(merged.abs.has_value());
    CHECK(*merged.abs == 5.0e-4);
    CHECK(merged.rel.has_value());
    CHECK(*merged.rel == 1.0e-4);

    // Override with no fields set returns base semantics intact.
    Tolerance merged_empty = Tolerance::merge(base, Tolerance{});
    CHECK(merged_empty.abs.has_value() && *merged_empty.abs == 1.0e-5);
    CHECK(merged_empty.rel.has_value() && *merged_empty.rel == 1.0e-4);

    // per_field maps from base persist through merge with an override
    // that carries no per_field entries.
    Tolerance with_pf;
    with_pf.abs = 1.0e-5;
    with_pf.per_field["out/r"].abs = 3.0e-4;
    Tolerance merged_pf = Tolerance::merge(with_pf, override1);
    CHECK(merged_pf.per_field.count("out/r"));
}

// ---------------------------------------------------------------- compareTyped

void testCompareTyped()
{
    Section s("compareTyped");

    auto compareOne = [](const Value& exp, const Value& got, const Tolerance& t) {
        std::vector<Diagnostic> out;
        compareTyped("return", exp, got, t, "test.tolerance", out);
        return out;
    };

    // Exact match passes.
    {
        Tolerance t; t.abs = 1.0e-6;
        auto d = compareOne(Value::makeFloat(1.0), Value::makeFloat(1.0), t);
        CHECK(d.empty());
    }

    // abs tolerance passing just within bound.
    {
        Tolerance t; t.abs = 1.0e-3;
        auto d = compareOne(Value::makeFloat(1.0), Value::makeFloat(1.0005), t);
        CHECK(d.empty());
    }

    // abs tolerance failing beyond bound.
    {
        Tolerance t; t.abs = 1.0e-5;
        auto d = compareOne(Value::makeFloat(1.0), Value::makeFloat(1.1), t);
        CHECK(!d.empty());
        CHECK(d.front().path == "return");
    }

    // rel tolerance passes where abs alone would fail.
    {
        Tolerance t; t.rel = 0.01;  // 1%
        auto d = compareOne(Value::makeFloat(1000.0), Value::makeFloat(1005.0), t);
        CHECK(d.empty());
    }

    // Passing condition is "abs OR rel OR ulp" (any one satisfies).
    {
        Tolerance t; t.abs = 1.0e-9; t.rel = 1.0;  // 100% rel always passes
        auto d = compareOne(Value::makeFloat(1.0), Value::makeFloat(2.0), t);
        CHECK(d.empty());
    }

    // Half rounding: an authored expected of 0.1 must be rounded to the
    // nearest-even half (~0.099975586) before comparison. The Half branch of
    // compareTyped fires when `got.kind == Half` (mirroring the real path
    // where a CTL function returns a half and fromArg rebuilds it as Kind::Half).
    {
        Tolerance t; t.abs = 1.0e-6;
        // Author expected as Float 0.1 (raw, as one would write in YAML).
        // Observed value is Half carrying the nearest-even half of 0.1 --
        // after rounding, both sides reduce to the same representable half.
        Value exp = Value::makeFloat(0.1);
        Value got = Value::makeHalf(static_cast<double>(static_cast<float>(
                                         static_cast<half>(0.1f))));
        auto d = compareOne(exp, got, t);
        CHECK(d.empty());
    }

    // Type mismatch (string vs. number) is a diagnostic, not a hard throw.
    {
        Tolerance t; t.abs = 1.0e-6;
        auto d = compareOne(Value::makeString("1.0"), Value::makeFloat(1.0), t);
        CHECK(!d.empty());
    }

    // Nested sequence: path uses "[i]" notation; one failing leaf produces
    // one diagnostic.
    {
        Tolerance t; t.abs = 1.0e-6;
        Value exp = Value::makeSeq({ Value::makeFloat(1.0), Value::makeFloat(2.0) });
        Value got = Value::makeSeq({ Value::makeFloat(1.0), Value::makeFloat(9.0) });
        auto d = compareOne(exp, got, t);
        CHECK_EQ(d.size(), size_t(1));
        CHECK(d.front().path.find("[1]") != std::string::npos);
    }

    // Nested map: path uses "/field" notation per v1.1.
    {
        Tolerance t; t.abs = 1.0e-6;
        Value exp = Value::makeMap({ {"x", Value::makeFloat(1.0)},
                                     {"y", Value::makeFloat(2.0)} });
        Value got = Value::makeMap({ {"x", Value::makeFloat(1.0)},
                                     {"y", Value::makeFloat(9.0)} });
        auto d = compareOne(exp, got, t);
        CHECK_EQ(d.size(), size_t(1));
        CHECK(d.front().path.find("y") != std::string::npos);
    }
}

// ---------------------------------------------------------------- validateTolerancePaths

void testValidateTolerancePaths()
{
    Section s("validateTolerancePaths");

    // Valid per_field on an FP leaf: no diagnostic. Path syntax is the
    // dotted/bracketed form understood by resolvePath (return.x, out[2]).
    {
        Tolerance t;
        t.abs = 1.0e-6;
        t.per_field["return.x"].abs = 1.0e-4;
        Value shape = Value::makeMap(
            { {"return", Value::makeMap({{"x", Value::makeFloat(0.0)},
                                         {"y", Value::makeFloat(0.0)}})} });
        std::vector<Diagnostic> out;
        validateTolerancePaths("", t, shape, out);
        CHECK(out.empty());
    }

    // per_field pointing to a missing path: diagnostic.
    {
        Tolerance t;
        t.per_field["return.missing"].abs = 1.0e-4;
        Value shape = Value::makeMap(
            { {"return", Value::makeMap({{"x", Value::makeFloat(0.0)}})} });
        std::vector<Diagnostic> out;
        validateTolerancePaths("", t, shape, out);
        CHECK(!out.empty());
    }

    // per_field pointing at a non-FP leaf (bool/int/string): diagnostic.
    {
        Tolerance t;
        t.per_field["return.flag"].abs = 1.0e-4;
        Value shape = Value::makeMap(
            { {"return", Value::makeMap({{"flag", Value::makeBool(true)}})} });
        std::vector<Diagnostic> out;
        validateTolerancePaths("", t, shape, out);
        CHECK(!out.empty());
    }
}

// ---------------------------------------------------------------- CsvTable

std::string writeTempFile(const std::string& body, const std::string& suffix)
{
    std::string path =
        (std::filesystem::temp_directory_path() / ("ctltest_unit_" + suffix)).string();
    std::ofstream f(path);
    f << body;
    f.close();
    return path;
}

void testCsvTable()
{
    Section s("CsvTable");

    // Header + two data rows, LF line endings.
    {
        std::string p = writeTempFile("a,b,c\n1,2,3\n4,5,6\n", "basic.csv");
        CsvTable t = readCsv(p);
        CHECK_EQ(t.columns.size(), size_t(3));
        CHECK(t.columns[0] == "a");
        CHECK(t.columns[2] == "c");
        CHECK_EQ(t.rows.size(), size_t(2));
        CHECK(t.rows[1][2] == "6");
        CHECK_EQ(t.colIndex("b"), size_t(1));
        CHECK_EQ(t.colIndex("missing"), size_t(-1));
    }

    // Quoted fields with embedded comma + escaped quote.
    {
        std::string body = "name,note\n"
                           "\"smith, j\",\"he said \"\"hi\"\"\"\n";
        std::string p = writeTempFile(body, "quoted.csv");
        CsvTable t = readCsv(p);
        CHECK(t.rows[0][0] == "smith, j");
        CHECK(t.rows[0][1] == "he said \"hi\"");
    }

    // Comment lines are dropped.
    {
        std::string body = "# top comment\nx,y\n# middle comment\n1,2\n";
        std::string p = writeTempFile(body, "comments.csv");
        CsvTable t = readCsv(p);
        CHECK_EQ(t.columns.size(), size_t(2));
        CHECK_EQ(t.rows.size(), size_t(1));
    }

    // CRLF line endings.
    {
        std::string p = writeTempFile("a,b\r\n1,2\r\n", "crlf.csv");
        CsvTable t = readCsv(p);
        CHECK_EQ(t.columns.size(), size_t(2));
        CHECK_EQ(t.rows.size(), size_t(1));
        CHECK(t.rows[0][1] == "2");
    }

    // cellToValue heuristics.
    Value vb = cellToValue("true");
    CHECK(vb.kind == Value::Kind::Bool);

    Value vi = cellToValue("-17");
    CHECK(vi.kind == Value::Kind::Int);

    Value vf = cellToValue("3.14");
    CHECK(vf.kind == Value::Kind::Float);

    Value vs = cellToValue("hello");
    CHECK(vs.kind == Value::Kind::String);

    Value ve = cellToValue("");
    CHECK(ve.kind == Value::Kind::String);

    // tableAsRows round-trip.
    {
        std::string p = writeTempFile("x,y\n1.0,2.0\n3.0,4.0\n", "rows.csv");
        CsvTable t = readCsv(p);
        auto rows = tableAsRows(t);
        CHECK_EQ(rows.size(), size_t(2));
        CHECK(rows[0].at("x").kind == Value::Kind::Float);
        CHECK(rows[1].at("y").f == 4.0);
    }
}

// ---------------------------------------------------------------- YamlLoader

void testYamlLoader()
{
    Section s("YamlLoader");

    // Minimal happy path: one case with inline oracle.
    {
        std::string body =
            "version: 1\n"
            "suite: unit-test-suite\n"
            "modules: [st_scalar]\n"
            "tests:\n"
            "  - id: a\n"
            "    function: st_scalar::identity_f\n"
            "    inputs: { x: 0.25 }\n"
            "    oracle: { inline: { return: 0.25 } }\n";
        std::string p = writeTempFile(body, "min.yaml");
        Suite suite = loadSuite(p);
        CHECK_EQ(suite.tests.size(), size_t(1));
        CHECK(suite.tests[0].id == "a");
        CHECK(suite.tests[0].mode == TestCase::Mode::Unit);
        CHECK(!suite.tests[0].knownFailure);
        CHECK(suite.tests[0].returnsName == "return");
    }

    // known_failure + returns_name + tolerance inheritance.
    {
        std::string body =
            "version: 1\n"
            "modules: [st_scalar]\n"
            "defaults: { tolerance: { abs: 1.0e-5 } }\n"
            "tests:\n"
            "  - id: kf\n"
            "    function: st_scalar::identity_f\n"
            "    inputs: { x: 0.25 }\n"
            "    returns_name: out\n"
            "    known_failure: true\n"
            "    oracle: { inline: { out: 0.25 } }\n";
        std::string p = writeTempFile(body, "kf.yaml");
        Suite suite = loadSuite(p);
        CHECK(suite.tests[0].knownFailure);
        CHECK(suite.tests[0].returnsName == "out");
        CHECK(suite.defaultTolerance.abs.has_value());
    }

    // Load error: malformed YAML produces LoadError with line/col info.
    {
        std::string body = "version: 1\nmodules: [x\n";   // broken sequence
        std::string p = writeTempFile(body, "bad.yaml");
        bool threw = false;
        try { loadSuite(p); }
        catch (const LoadError& e) {
            threw = true;
            CHECK(!e.path().empty());
        }
        CHECK(threw);
    }
}

// ---------------------------------------------------------------- Marshal round-trip

// These tests drive InterpRunner against the existing st_scalar.ctl fixture,
// which is copied into the binary dir by the CMake rule. They validate that
// Marshal + TypeStorage round-trip every scalar kind on flat AND nested
// shapes after the v1.1 parser fix lifted the flat-type restriction.

// InterpRunner has a user-declared dtor which suppresses implicit move, and
// holds a unique_ptr so copy is deleted. Configure via a ref-parameter helper
// rather than returning by value. The fixture is copied next to this binary
// at build time (see moduletest/unittest/CMakeLists.txt).
void initInterp(InterpRunner& r)
{
    r.setModulePaths({"."});
    r.loadModules({"st_scalar"});
}

void testMarshalScalarRoundTrip()
{
    Section s("Marshal scalar round-trip");
    InterpRunner r;
    initInterp(r);

    // float identity
    {
        auto out = r.run("st_scalar::identity_f",
                         {{"x", Value::makeFloat(0.125)}},
                         "return");
        CHECK(out.count("return"));
        CHECK(out["return"].kind == Value::Kind::Float);
        CHECK(out["return"].f == 0.125);
    }

    // int add
    {
        auto out = r.run("st_scalar::add_i",
                         {{"a", Value::makeInt(3)}, {"b", Value::makeInt(4)}},
                         "return");
        CHECK(out["return"].kind == Value::Kind::Int);
        CHECK_EQ(out["return"].i, int64_t(7));
    }

    // bool and
    {
        auto out = r.run("st_scalar::both",
                         {{"a", Value::makeBool(true)}, {"b", Value::makeBool(false)}},
                         "return");
        CHECK(out["return"].kind == Value::Kind::Bool);
        CHECK(out["return"].b == false);
    }

    // half (0.5 is exact)
    {
        auto out = r.run("st_scalar::to_half",
                         {{"x", Value::makeFloat(0.5)}},
                         "return");
        CHECK(out["return"].kind == Value::Kind::Half);
        CHECK(out["return"].f == 0.5);
    }
}

void testMarshalFlatArrayRoundTrip()
{
    Section s("Marshal flat array round-trip");
    InterpRunner r;
    initInterp(r);

    auto out = r.run("st_scalar::lut_sum8",
                     {{"lut", Value::makeSeq({
                         Value::makeFloat(0.0), Value::makeFloat(0.1),
                         Value::makeFloat(0.2), Value::makeFloat(0.3),
                         Value::makeFloat(0.4), Value::makeFloat(0.5),
                         Value::makeFloat(0.6), Value::makeFloat(0.7),
                     })}},
                     "return");
    CHECK(out["return"].kind == Value::Kind::Float);
    CHECK(std::fabs(out["return"].f - 2.8) < 1.0e-5);
}

void testMarshalStructRoundTrip()
{
    Section s("Marshal struct round-trip");
    InterpRunner r;
    initInterp(r);

    auto out = r.run("st_scalar::make_point",
                     {{"x", Value::makeFloat(3.0)}, {"y", Value::makeFloat(4.0)}},
                     "return");
    CHECK(out["return"].kind == Value::Kind::Map);
    CHECK(out["return"].map.count("x"));
    CHECK(out["return"].map.count("y"));
    CHECK(out["return"].map["x"].f == 3.0);
    CHECK(out["return"].map["y"].f == 4.0);
}

void testMarshalNestedArrayRoundTrip()
{
    // v1.1 regression gate: float[3][3] in, float[3][3] out.
    Section s("Marshal nested array round-trip (v1.1)");
    InterpRunner r;
    initInterp(r);

    Value m = Value::makeSeq({
        Value::makeSeq({Value::makeFloat(1.0), Value::makeFloat(2.0), Value::makeFloat(3.0)}),
        Value::makeSeq({Value::makeFloat(4.0), Value::makeFloat(5.0), Value::makeFloat(6.0)}),
        Value::makeSeq({Value::makeFloat(7.0), Value::makeFloat(8.0), Value::makeFloat(9.0)}),
    });

    auto out = r.run("st_scalar::transpose3x3", {{"m", m}}, "return");
    CHECK(out.count("out"));
    const Value& tr = out["out"];
    CHECK(tr.kind == Value::Kind::Seq);
    CHECK_EQ(tr.seq.size(), size_t(3));

    // Expected: transpose of m.
    double expected[3][3] = { {1,4,7}, {2,5,8}, {3,6,9} };
    for (int i = 0; i < 3; ++i) {
        const Value& row = tr.seq[i];
        CHECK(row.kind == Value::Kind::Seq);
        for (int j = 0; j < 3; ++j) {
            CHECK(row.seq[j].f == expected[i][j]);
        }
    }
}

void testMarshalStructWithArrayMemberRoundTrip()
{
    // v1.1: struct containing an array member (struct to array to scalar path).
    Section s("Marshal struct-with-array round-trip (v1.1)");
    InterpRunner r;
    initInterp(r);

    auto out = r.run("st_scalar::make_tri",
                     {{"a", Value::makeFloat(1.0)},
                      {"b", Value::makeFloat(2.0)},
                      {"c", Value::makeFloat(3.0)},
                      {"n", Value::makeInt(7)}},
                     "return");
    CHECK(out["return"].kind == Value::Kind::Map);
    const Value& t = out["return"];
    CHECK(t.map.count("v"));
    CHECK(t.map.at("v").kind == Value::Kind::Seq);
    CHECK_EQ(t.map.at("v").seq.size(), size_t(3));
    CHECK(t.map.at("v").seq[0].f == 1.0);
    CHECK(t.map.at("v").seq[2].f == 3.0);
    CHECK(t.map.at("n").kind == Value::Kind::Int);
    CHECK_EQ(t.map.at("n").i, int64_t(7));
}

void testMarshalArrayOfStructRoundTrip()
{
    // v1.1: array of struct (array to struct to scalar path).
    Section s("Marshal array-of-struct round-trip (v1.1)");
    InterpRunner r;
    initInterp(r);

    Value q = Value::makeSeq({
        Value::makeMap({{"x", Value::makeFloat(1.0)}, {"y", Value::makeFloat(0.0)}}),
        Value::makeMap({{"x", Value::makeFloat(2.0)}, {"y", Value::makeFloat(0.0)}}),
        Value::makeMap({{"x", Value::makeFloat(3.0)}, {"y", Value::makeFloat(0.0)}}),
        Value::makeMap({{"x", Value::makeFloat(4.0)}, {"y", Value::makeFloat(0.0)}}),
    });
    auto out = r.run("st_scalar::quad_sum_x", {{"q", q}}, "return");
    CHECK(out["return"].kind == Value::Kind::Float);
    CHECK(std::fabs(out["return"].f - 10.0) < 1.0e-5);
}

// ============================================================================
// v1.2 hardening pass -- additional unit tests for previously-uncovered
// public surfaces. Organized in the same `void test*()` style as above.
// ============================================================================

// ---------------------------------------------------------------- Value::describe

void testValueDescribe()
{
    Section s("Value::describe");

    CHECK(Value::makeBool(true).describe()  == "true");
    CHECK(Value::makeBool(false).describe() == "false");
    CHECK(Value::makeInt(-7).describe()     == "-7");
    CHECK(Value::makeUInt(42u).describe()   == "42");

    // String describe wraps in quotes.
    CHECK_CONTAINS(Value::makeString("hi").describe(), "\"hi\"");

    // Half describe surfaces decimal + 16-bit hex pattern.
    const std::string halfDesc = Value::makeHalf(0.5).describe();
    CHECK_CONTAINS(halfDesc, "0.5");
    CHECK_CONTAINS(halfDesc, "half 0x");

    // Seq + Map describe uses brackets/braces, comma-separated.
    const std::string seqDesc =
        Value::makeSeq({Value::makeFloat(1.0), Value::makeFloat(2.0)}).describe();
    CHECK_CONTAINS(seqDesc, "[");
    CHECK_CONTAINS(seqDesc, ",");
    CHECK_CONTAINS(seqDesc, "]");

    const std::string mapDesc =
        Value::makeMap({{"x", Value::makeFloat(1.0)},
                        {"y", Value::makeFloat(2.0)}}).describe();
    CHECK_CONTAINS(mapDesc, "{");
    CHECK_CONTAINS(mapDesc, "x:");
    CHECK_CONTAINS(mapDesc, "y:");
    CHECK_CONTAINS(mapDesc, "}");
}

// ---------------------------------------------------------------- Value Seq+Map deep nesting

void testValueSeqAndMap()
{
    Section s("Value Seq+Map deep nesting");

    // Map of Seq of Map -- three-level nest. testValue covers the flat case;
    // this guards against regressions in the recursive Value::describe path
    // and the makeSeq/makeMap factory shape.
    Value nested = Value::makeMap({
        { "outer", Value::makeSeq({
            Value::makeMap({{"a", Value::makeFloat(1.0)},
                            {"b", Value::makeFloat(2.0)}}),
            Value::makeMap({{"a", Value::makeFloat(3.0)},
                            {"b", Value::makeFloat(4.0)}})
        })}
    });
    CHECK(nested.kind == Value::Kind::Map);
    CHECK_EQ(nested.map.size(), size_t(1));
    CHECK(nested.map.count("outer"));
    CHECK(nested.map["outer"].kind == Value::Kind::Seq);
    CHECK_EQ(nested.map["outer"].seq.size(), size_t(2));
    CHECK(nested.map["outer"].seq[0].kind == Value::Kind::Map);
    CHECK(nested.map["outer"].seq[1].map.at("b").f == 4.0);

    // describe() should recurse without crashing and contain every leaf.
    const std::string desc = nested.describe();
    CHECK_CONTAINS(desc, "outer");
    CHECK_CONTAINS(desc, "a:");
    CHECK_CONTAINS(desc, "b:");
    // Every leaf number reachable.
    CHECK_CONTAINS(desc, "1");
    CHECK_CONTAINS(desc, "4");

    // Mixed-kind sequences are legal: Seq is heterogeneous.
    Value mixed = Value::makeSeq({
        Value::makeFloat(1.0), Value::makeString("two"), Value::makeBool(true)
    });
    CHECK(mixed.kind == Value::Kind::Seq);
    CHECK(mixed.seq[0].kind == Value::Kind::Float);
    CHECK(mixed.seq[1].kind == Value::Kind::String);
    CHECK(mixed.seq[2].kind == Value::Kind::Bool);
}

// ---------------------------------------------------------------- Tolerance::empty + per_field bleed

void testToleranceEmptyAndPerField()
{
    Section s("Tolerance::empty + per_field");

    // empty() truth table.
    Tolerance t0;
    CHECK(t0.empty());

    Tolerance t1; t1.abs = 1e-5;
    CHECK(!t1.empty());

    Tolerance t2; t2.rel = 1e-4;
    CHECK(!t2.empty());

    Tolerance t3; t3.ulp = 2;
    CHECK(!t3.empty());

    Tolerance t4; t4.per_field["out.r"].abs = 1e-4;
    CHECK(!t4.empty());

    Tolerance t5; t5.per_channel["R"].abs = 1e-4;
    CHECK(!t5.empty());

    // per_field overrides at one path don't bleed to siblings: applying tol
    // to a leaf at a non-overridden path uses the base, not the override.
    Tolerance base; base.abs = 1.0e-5;
    base.per_field["a"].abs = 1.0e-2;     // very loose for "a"
    Value exp = Value::makeMap({{"a", Value::makeFloat(0.0)},
                                 {"b", Value::makeFloat(0.0)}});
    Value got = Value::makeMap({{"a", Value::makeFloat(0.005)},   // within 1e-2
                                 {"b", Value::makeFloat(0.001)}}); // outside 1e-5

    std::vector<Diagnostic> diags;
    compareTyped("", exp, got, base, "test.tolerance", diags);
    CHECK_EQ(diags.size(), size_t(1));
    CHECK_CONTAINS(diags.front().path, "b");
}

// ---------------------------------------------------------------- compareTyped -- deep nested diagnostic paths

void testCompareTypedDeepNestedPaths()
{
    Section s("compareTyped deep paths");

    // Map to Map to Seq to Map to leaf.  Path on mismatch should be
    // "out.v[0].x".
    Value exp = Value::makeMap({
        {"out", Value::makeMap({
            {"v", Value::makeSeq({
                Value::makeMap({{"x", Value::makeFloat(1.0)},
                                {"y", Value::makeFloat(2.0)}})
            })}
        })}
    });
    Value got = Value::makeMap({
        {"out", Value::makeMap({
            {"v", Value::makeSeq({
                Value::makeMap({{"x", Value::makeFloat(99.0)},   // mismatch
                                {"y", Value::makeFloat(2.0)}})
            })}
        })}
    });

    Tolerance t; t.abs = 1.0e-6;
    std::vector<Diagnostic> diags;
    compareTyped("", exp, got, t, "test.tolerance", diags);
    CHECK_EQ(diags.size(), size_t(1));
    CHECK_CONTAINS(diags.front().path, "out.v[0].x");
    CHECK(diags.front().abs_err > 90.0);
}

// ---------------------------------------------------------------- compareTyped -- map missing/extra keys

void testCompareTypedMapMissingExtraKeys()
{
    Section s("compareTyped map missing/extra");

    Value exp = Value::makeMap({{"a", Value::makeFloat(1.0)},
                                 {"b", Value::makeFloat(2.0)}});
    Value got = Value::makeMap({{"a", Value::makeFloat(1.0)},
                                 {"c", Value::makeFloat(3.0)}});

    Tolerance t; t.abs = 1.0e-6;
    std::vector<Diagnostic> diags;
    compareTyped("", exp, got, t, "test.tolerance", diags);

    // Two diagnostics: "c" extra (got has key not in expected),
    //                  "b" missing (expected has key not in got).
    CHECK_EQ(diags.size(), size_t(2));
    bool sawExtra = false, sawMissing = false;
    for (const auto& d : diags) {
        if (d.path == "c") {
            sawExtra = true;
            CHECK_CONTAINS(d.expectedText, "<missing>");
        }
        if (d.path == "b") {
            sawMissing = true;
            CHECK_CONTAINS(d.gotText, "<missing>");
        }
    }
    CHECK(sawExtra);
    CHECK(sawMissing);
}

// ---------------------------------------------------------------- compareTyped -- seq size + ULP tolerance

void testCompareTypedSeqSizeAndUlp()
{
    Section s("compareTyped seq size + ULP");

    // Sequence of different sizes: one diag at the seq root, no descent.
    {
        Value exp = Value::makeSeq({Value::makeFloat(1.0), Value::makeFloat(2.0)});
        Value got = Value::makeSeq({Value::makeFloat(1.0)});
        Tolerance t; t.abs = 1.0e-6;
        std::vector<Diagnostic> diags;
        compareTyped("return", exp, got, t, "test.tolerance", diags);
        CHECK_EQ(diags.size(), size_t(1));
        CHECK(diags.front().path == "return");
    }

    // ULP tolerance: 1.0 vs nextafter(1.0, 2.0) is exactly 1 ULP.
    {
        const float a = 1.0f;
        const float b = std::nextafterf(a, 2.0f);
        Value exp = Value::makeFloat(a);
        Value got = Value::makeFloat(b);

        Tolerance loose; loose.ulp = 1;
        std::vector<Diagnostic> d1;
        compareTyped("return", exp, got, loose, "test.tolerance", d1);
        CHECK(d1.empty());

        Tolerance tight; tight.ulp = 0;
        std::vector<Diagnostic> d2;
        compareTyped("return", exp, got, tight, "test.tolerance", d2);
        CHECK_EQ(d2.size(), size_t(1));
        CHECK(d2.front().ulp_err == 1);
    }
}

// ---------------------------------------------------------------- validateTolerancePaths -- deep paths + per_channel scope

void testValidateTolerancePathsDeepAndPerChannel()
{
    Section s("validateTolerancePaths deep + per_channel");

    // Deep dotted key resolves through three levels of map.
    {
        Tolerance t;
        t.per_field["return.outer.inner"].abs = 1e-4;

        Value shape = Value::makeMap({
            {"return", Value::makeMap({
                {"outer", Value::makeMap({
                    {"inner", Value::makeFloat(0.0)}
                })}
            })}
        });
        std::vector<Diagnostic> diags;
        validateTolerancePaths("", t, shape, diags);
        CHECK(diags.empty());
    }

    // Bracket index: "return[0].x" resolves through Seq to Map to Float.
    {
        Tolerance t;
        t.per_field["return[0].x"].abs = 1e-4;

        Value shape = Value::makeMap({
            {"return", Value::makeSeq({
                Value::makeMap({{"x", Value::makeFloat(0.0)}})
            })}
        });
        std::vector<Diagnostic> diags;
        validateTolerancePaths("", t, shape, diags);
        CHECK(diags.empty());
    }

    // per_channel keys are image-mode-only (resolved later in ImageCompare);
    // validateTolerancePaths must not flag them as missing paths.
    {
        Tolerance t;
        t.per_channel["R"].abs = 1e-4;
        t.per_channel["bogus"].abs = 1e-4;
        Value shape = Value::makeMap({{"return", Value::makeFloat(0.0)}});
        std::vector<Diagnostic> diags;
        validateTolerancePaths("", t, shape, diags);
        CHECK(diags.empty());
    }
}

// ---------------------------------------------------------------- Diagnostic field preservation

void testDiagnosticFields()
{
    Section s("Diagnostic field preservation");

    Diagnostic d;
    d.path = "out.r";
    d.expectedText = "1.0";
    d.gotText      = "1.5";
    d.abs_err = 0.5;
    d.rel_err = 0.5;
    d.ulp_err = 4194304;
    d.applied.abs = 1e-6;
    d.applied.rel = 1e-7;
    d.toleranceSource = "test.tolerance.per_field[out.r]";

    // Copy must preserve every field.
    Diagnostic copy = d;
    CHECK(copy.path == "out.r");
    CHECK(copy.expectedText == "1.0");
    CHECK(copy.gotText == "1.5");
    CHECK(copy.abs_err == 0.5);
    CHECK(copy.rel_err == 0.5);
    CHECK(copy.ulp_err == 4194304);
    CHECK(copy.applied.abs.has_value() && *copy.applied.abs == 1e-6);
    CHECK(copy.applied.rel.has_value() && *copy.applied.rel == 1e-7);
    CHECK(copy.toleranceSource == "test.tolerance.per_field[out.r]");
}

// ---------------------------------------------------------------- Reporter helpers

namespace {

// Build a TestCase shell sufficient to drive Reporter::onCaseResult. The
// reporter only reads testcase->id and testcase->description; nothing else.
TestCase makeShellCase(const std::string& id, const std::string& descr = "")
{
    TestCase tc;
    tc.id = id;
    tc.description = descr;
    return tc;
}

CaseResult makeResult(const TestCase* tc, CaseResult::Outcome o,
                      long long durationUs = 0)
{
    CaseResult r;
    r.testcase = tc;
    r.outcome  = o;
    r.duration = std::chrono::microseconds(durationUs);
    return r;
}

} // namespace

// ---------------------------------------------------------------- ConsoleReporter

void testConsoleReporterPassFailMix()
{
    Section s("ConsoleReporter pass/fail mix");

    TestCase a = makeShellCase("a", "first");
    TestCase b = makeShellCase("b");
    TestCase c = makeShellCase("c");
    TestCase d = makeShellCase("d");
    TestCase e = makeShellCase("e");

    CaseResult ra = makeResult(&a, CaseResult::Outcome::Pass, 100);
    CaseResult rb = makeResult(&b, CaseResult::Outcome::Fail, 50);
    Diagnostic diag;
    diag.path = "return";
    diag.expectedText = "0.5";
    diag.gotText      = "0.6";
    diag.abs_err = 0.1;
    diag.toleranceSource = "test.tolerance";
    rb.verdict.failures.push_back(diag);

    CaseResult rc = makeResult(&c, CaseResult::Outcome::Error, 75);
    rc.errorMessage = "module not found: bogus";
    CaseResult rd = makeResult(&d, CaseResult::Outcome::Skipped, 0);
    CaseResult re = makeResult(&e, CaseResult::Outcome::UnexpectedPass, 30);

    std::ostringstream os;
    ConsoleReporter rep(os, /*color=*/false);
    rep.onSuiteBegin("S", 5);
    rep.onCaseResult(ra);
    rep.onCaseResult(rb);
    rep.onCaseResult(rc);
    rep.onCaseResult(rd);
    rep.onCaseResult(re);
    rep.onSuiteEnd(/*passed=*/1, /*failed=*/1, /*errored=*/1,
                   /*skipped=*/1, /*unexpectedPass=*/1);

    const std::string out = os.str();
    CHECK_CONTAINS(out, "ctltest: S");
    CHECK_CONTAINS(out, "[PASS] a");
    CHECK_CONTAINS(out, "[FAIL] b");
    CHECK_CONTAINS(out, "[ERROR] c");
    CHECK_CONTAINS(out, "[SKIP] d");
    CHECK_CONTAINS(out, "[UNEXPECTED PASS] e");

    // Failure path emits the diagnostic body.
    CHECK_CONTAINS(out, "at return");
    CHECK_CONTAINS(out, "expected 0.5");
    CHECK_CONTAINS(out, "got 0.6");

    // Error path emits the error message.
    CHECK_CONTAINS(out, "module not found: bogus");

    // Summary: 1/5 passed plus all four non-pass categories.
    CHECK_CONTAINS(out, "summary: 1/5 passed");
    CHECK_CONTAINS(out, "1 failed");
    CHECK_CONTAINS(out, "1 errored");
    CHECK_CONTAINS(out, "1 skipped");
    CHECK_CONTAINS(out, "1 UNEXPECTED PASS");
}

// ---------------------------------------------------------------- TapReporter

void testTapReporterFormat()
{
    Section s("TapReporter format");

    TestCase a = makeShellCase("a");
    TestCase b = makeShellCase("b");
    TestCase c = makeShellCase("c");

    CaseResult ra = makeResult(&a, CaseResult::Outcome::Pass);
    CaseResult rb = makeResult(&b, CaseResult::Outcome::Fail);
    Diagnostic diag;
    diag.path = "return";
    diag.expectedText = "0.5";
    diag.gotText = "0.6";
    rb.verdict.failures.push_back(diag);
    CaseResult rc = makeResult(&c, CaseResult::Outcome::Skipped);

    std::ostringstream os;
    TapReporter rep(os);
    rep.onSuiteBegin("suite", 3);
    rep.onCaseResult(ra);
    rep.onCaseResult(rb);
    rep.onCaseResult(rc);
    rep.onSuiteEnd(1, 1, 0, 1, 0);
    rep.finalizePlan();

    const std::string out = os.str();
    CHECK_CONTAINS(out, "TAP version 14");
    CHECK_CONTAINS(out, "# suite: suite");
    CHECK_CONTAINS(out, "ok 1 - suite::a");
    CHECK_CONTAINS(out, "not ok 2 - suite::b");
    CHECK_CONTAINS(out, "ok 3 - suite::c # SKIP");
    CHECK_CONTAINS(out, "  ---");
    CHECK_CONTAINS(out, "severity: fail");
    CHECK_CONTAINS(out, "  ...");
    CHECK_CONTAINS(out, "1..3");
}

// ---------------------------------------------------------------- JUnitReporter

void testJUnitReporterXml()
{
    Section s("JUnitReporter XML");

    TestCase a = makeShellCase("alpha");
    TestCase b = makeShellCase("beta < gamma");   // exercise XML escape
    TestCase c = makeShellCase("delta");
    TestCase d = makeShellCase("epsilon");

    CaseResult ra = makeResult(&a, CaseResult::Outcome::Pass, 1500);
    CaseResult rb = makeResult(&b, CaseResult::Outcome::Fail, 2000);
    Diagnostic diag;
    diag.path = "return";
    diag.expectedText = "1";
    diag.gotText = "2";
    rb.verdict.failures.push_back(diag);

    CaseResult rc = makeResult(&c, CaseResult::Outcome::Error, 100);
    rc.errorMessage = "boom & crash <oops>";
    CaseResult rd = makeResult(&d, CaseResult::Outcome::Skipped, 0);

    std::ostringstream os;
    JUnitReporter rep(os);
    rep.onSuiteBegin("S", 4);
    rep.onCaseResult(ra);
    rep.onCaseResult(rb);
    rep.onCaseResult(rc);
    rep.onCaseResult(rd);
    rep.onSuiteEnd(1, 1, 1, 1, 0);
    rep.finalize();

    const std::string out = os.str();
    CHECK_CONTAINS(out, "<?xml version=\"1.0\"");
    CHECK_CONTAINS(out, "<testsuites");
    CHECK_CONTAINS(out, "tests=\"4\"");
    CHECK_CONTAINS(out, "failures=\"1\"");
    CHECK_CONTAINS(out, "errors=\"1\"");
    CHECK_CONTAINS(out, "skipped=\"1\"");
    CHECK_CONTAINS(out, "<testcase");
    CHECK_CONTAINS(out, "name=\"alpha\"");
    CHECK_CONTAINS(out, "name=\"beta &lt; gamma\"");   // attribute escape
    CHECK_CONTAINS(out, "<failure");
    CHECK_CONTAINS(out, "<error");
    // Element-content escape: '&' to &amp;, '<' to &lt;, '>' to &gt;.
    CHECK_CONTAINS(out, "boom &amp; crash &lt;oops&gt;");
    CHECK_CONTAINS(out, "<skipped/>");
}

// ---------------------------------------------------------------- Reporters -- duration handling

void testReporterTimeReporting()
{
    Section s("Reporter time reporting");

    TestCase a = makeShellCase("a");
    CaseResult ra = makeResult(&a, CaseResult::Outcome::Pass, /*durationUs=*/1234);
    CaseResult rzero = makeResult(&a, CaseResult::Outcome::Pass, /*durationUs=*/0);

    // Console: shows microseconds inline.
    {
        std::ostringstream os;
        ConsoleReporter rep(os, /*color=*/false);
        rep.onSuiteBegin("S", 2);
        rep.onCaseResult(ra);
        rep.onCaseResult(rzero);
        rep.onSuiteEnd(2, 0, 0, 0, 0);
        const std::string out = os.str();
        CHECK_CONTAINS(out, "(1234 ");      // duration field present
        CHECK_CONTAINS(out, "(0 ");
    }

    // JUnit: time attribute is fractional seconds. Zero must still parse.
    {
        std::ostringstream os;
        JUnitReporter rep(os);
        rep.onSuiteBegin("S", 2);
        rep.onCaseResult(ra);
        rep.onCaseResult(rzero);
        rep.onSuiteEnd(2, 0, 0, 0, 0);
        rep.finalize();
        const std::string out = os.str();
        CHECK_CONTAINS(out, "time=\"0.001234\"");
        CHECK_CONTAINS(out, "time=\"0.000000\"");
    }
}



// ---------------------------------------------------------------- ImageIO round-trip

namespace {

Image makeRgbaTestImage(int w, int h)
{
    Image img;
    img.width = w; img.height = h;
    const size_t N = static_cast<size_t>(w) * h;
    img.channels["R"].assign(N, 0.0f);
    img.channels["G"].assign(N, 0.0f);
    img.channels["B"].assign(N, 0.0f);
    img.channels["A"].assign(N, 1.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * w + x;
            img.channels["R"][i] = static_cast<float>(x) / std::max(1, w - 1);
            img.channels["G"][i] = static_cast<float>(y) / std::max(1, h - 1);
            img.channels["B"][i] = 0.5f;
        }
    }
    return img;
}

} // namespace

void testImageIORoundTripFloat()
{
    Section s("ImageIO float round-trip");

    TempDir td;
    const std::string path = td / "round.exr";

    Image src = makeRgbaTestImage(4, 3);
    writeImage(path, src);

    Image back = readImage(path);
    CHECK_EQ(back.width, src.width);
    CHECK_EQ(back.height, src.height);
    CHECK_EQ(back.channels.size(), src.channels.size());
    for (const auto& kv : src.channels) {
        CHECK(back.channels.count(kv.first));
        const auto& bvec = back.channels.at(kv.first);
        CHECK_EQ(bvec.size(), kv.second.size());
        for (size_t i = 0; i < bvec.size(); ++i) {
            // float->float through Imf must be bit-exact.
            CHECK(bvec[i] == kv.second[i]);
        }
    }
}

void testImageIOMultiChannel()
{
    Section s("ImageIO multi-channel");

    // EXR header iteration is alphabetical; confirm channels round-trip in
    // any insertion order and all are recoverable by name.
    TempDir td;
    const std::string path = td / "multi.exr";

    Image src;
    src.width = 2; src.height = 2;
    src.channels["Z"]    = {1.0f, 2.0f, 3.0f, 4.0f};
    src.channels["luma"] = {0.1f, 0.2f, 0.3f, 0.4f};
    src.channels["A"]    = {0.0f, 0.0f, 0.0f, 0.0f};
    writeImage(path, src);

    Image back = readImage(path);
    CHECK(back.channels.count("Z"));
    CHECK(back.channels.count("luma"));
    CHECK(back.channels.count("A"));
    CHECK(back.channels["Z"][3] == 4.0f);
    CHECK(back.channels["luma"][2] == 0.3f);
}

void testImageIOErrors()
{
    Section s("ImageIO errors");

    // Missing file to ImageIOError mentioning the path.
    {
        bool threw = false;
        try { readImage((std::filesystem::temp_directory_path()
                         / "__ctltest_does_not_exist.exr").string()); }
        catch (const ImageIOError& e) {
            threw = true;
            CHECK_CONTAINS(std::string(e.what()), "__ctltest_does_not_exist");
        }
        CHECK(threw);
    }

    // writeImage refuses to write an empty image.
    {
        TempDir td;
        Image empty;
        bool threw = false;
        try { writeImage(td / "empty.exr", empty); }
        catch (const ImageIOError&) { threw = true; }
        CHECK(threw);
    }
}

// ---------------------------------------------------------------- ImageCompare -- abs/rel/ulp

void testCompareImagesAbsRelUlp()
{
    Section s("compareImages abs/rel/ulp");

    Image expected;
    expected.width = 2; expected.height = 1;
    expected.channels["R"] = {1.0f, 1000.0f};

    // ABS: difference 1e-3 -- passes with abs=1e-2, fails with abs=1e-6.
    {
        Image actual = expected;
        actual.channels["R"][0] = 1.001f;
        Tolerance loose; loose.abs = 1e-2;
        ImageCompareResult r1 = compareImages(expected, actual, loose, 0);
        CHECK(r1.passed);
        CHECK_EQ(r1.mismatchCount, size_t(0));

        Tolerance tight; tight.abs = 1e-6;
        ImageCompareResult r2 = compareImages(expected, actual, tight, 0);
        CHECK(!r2.passed);
        CHECK_EQ(r2.mismatchCount, size_t(1));
    }

    // REL: difference 1.0 against 1000 -- 1e-3 relative; passes with rel=1e-2.
    {
        Image actual = expected;
        actual.channels["R"][1] = 1001.0f;
        Tolerance t; t.rel = 1e-2;
        ImageCompareResult r = compareImages(expected, actual, t, 0);
        CHECK(r.passed);
    }

    // ULP: 1.0 vs nextafter -- 1 ULP. Pass with ulp=1, fail with ulp=0.
    {
        Image actual = expected;
        actual.channels["R"][0] = std::nextafterf(1.0f, 2.0f);
        Tolerance ok;   ok.ulp = 1;
        ImageCompareResult r1 = compareImages(expected, actual, ok, 0);
        CHECK(r1.passed);
        Tolerance tight; tight.ulp = 0;
        ImageCompareResult r2 = compareImages(expected, actual, tight, 0);
        CHECK(!r2.passed);
    }

    // per_channel sharpens base.
    {
        Tolerance base; base.abs = 1e-2;
        base.per_channel["R"].abs = 1e-6;
        Image actual = expected;
        actual.channels["R"][0] = 1.001f;
        ImageCompareResult r = compareImages(expected, actual, base, 0);
        CHECK(!r.passed);   // per_channel override demanded tighter abs
    }
}

void testCompareImagesMaxFailingPixels()
{
    Section s("compareImages max_failing_pixels");

    // Three mismatches.
    Image expected;
    expected.width = 4; expected.height = 1;
    expected.channels["R"] = {1.0f, 1.0f, 1.0f, 1.0f};
    Image actual = expected;
    actual.channels["R"] = {2.0f, 1.0f, 2.0f, 2.0f};   // 3 mismatches

    Tolerance t; t.abs = 1e-6;
    // <0: no cap, must be zero mismatches to pass.
    {
        ImageCompareResult r = compareImages(expected, actual, t, /*max=*/-1);
        CHECK(!r.passed);
        CHECK_EQ(r.mismatchCount, size_t(3));
    }
    // 0: any single mismatch fails.
    {
        ImageCompareResult r = compareImages(expected, actual, t, /*max=*/0);
        CHECK(!r.passed);
    }
    // 2: budget undershoots, still fails.
    {
        ImageCompareResult r = compareImages(expected, actual, t, /*max=*/2);
        CHECK(!r.passed);
    }
    // 3: budget covers all three, passes.
    {
        ImageCompareResult r = compareImages(expected, actual, t, /*max=*/3);
        CHECK(r.passed);
        CHECK_EQ(r.mismatchCount, size_t(3));
    }
}

void testWriteFailureArtifacts()
{
    Section s("writeFailureArtifacts");

    TempDir td;
    const std::string ref = td / "ref.exr";

    Image expected;
    expected.width = 2; expected.height = 1;
    expected.channels["R"] = {1.0f, 2.0f};
    expected.channels["G"] = {3.0f, 4.0f};
    Image actual = expected;
    actual.channels["R"] = {1.5f, 2.5f};
    actual.channels["G"] = {3.5f, 4.5f};

    writeImage(ref, expected);
    writeFailureArtifacts(ref, expected, actual);

    namespace fs = std::filesystem;
    CHECK(fs::exists(ref + ".actual.exr"));
    CHECK(fs::exists(ref + ".diff.exr"));

    Image actualBack = readImage(ref + ".actual.exr");
    CHECK(actualBack.channels.at("R")[0] == 1.5f);

    Image diff = readImage(ref + ".diff.exr");
    // diff = actual - expected = 0.5 across the board.
    for (const auto& kv : diff.channels) {
        for (float d : kv.second) CHECK_NEAR(d, 0.5, 1e-6);
    }
}

// ---------------------------------------------------------------- InlineOracle

void testInlineOracleHappyAndIgnore()
{
    Section s("InlineOracle happy + ignore");

    InlineOracle oracle;

    // Happy: actual outputs match every key in inlineExpected exactly.
    {
        TestCase tc;
        tc.tolerance.abs = 1e-6;
        tc.oracle.kind = OracleSpec::Kind::Inline;
        tc.oracle.inlineExpected["return"] = Value::makeFloat(0.5);

        std::map<std::string, Value> outputs;
        outputs["return"] = Value::makeFloat(0.5);

        OracleVerdict v = oracle.check(tc, outputs);
        CHECK(v.passed);
        CHECK(v.failures.empty());
    }

    // Mismatch yields one diagnostic.
    {
        TestCase tc;
        tc.tolerance.abs = 1e-6;
        tc.oracle.kind = OracleSpec::Kind::Inline;
        tc.oracle.inlineExpected["return"] = Value::makeFloat(0.5);

        std::map<std::string, Value> outputs;
        outputs["return"] = Value::makeFloat(0.7);

        OracleVerdict v = oracle.check(tc, outputs);
        CHECK(!v.passed);
        CHECK_EQ(v.failures.size(), size_t(1));
    }

    // ignoreOutputs suppresses an unmentioned actual output (the oracle's
    // "every non-ignored output must be addressed" rule).
    {
        TestCase tc;
        tc.tolerance.abs = 1e-6;
        tc.oracle.kind = OracleSpec::Kind::Inline;
        tc.oracle.inlineExpected["return"] = Value::makeFloat(0.5);
        tc.oracle.ignoreOutputs.push_back("debug");

        std::map<std::string, Value> outputs;
        outputs["return"] = Value::makeFloat(0.5);
        outputs["debug"]  = Value::makeFloat(99.0);

        OracleVerdict v = oracle.check(tc, outputs);
        CHECK(v.passed);
    }

    // Without ignoreOutputs, the same shape fails because "debug" isn't
    // addressed by inlineExpected.
    {
        TestCase tc;
        tc.tolerance.abs = 1e-6;
        tc.oracle.kind = OracleSpec::Kind::Inline;
        tc.oracle.inlineExpected["return"] = Value::makeFloat(0.5);

        std::map<std::string, Value> outputs;
        outputs["return"] = Value::makeFloat(0.5);
        outputs["debug"]  = Value::makeFloat(99.0);

        OracleVerdict v = oracle.check(tc, outputs);
        CHECK(!v.passed);
    }
}

// ---------------------------------------------------------------- CsvOracle

void testCsvOracleAlignment()
{
    Section s("CsvOracle column alignment");

    TempDir td;
    const std::string csv = td / "expected.csv";
    {
        std::ofstream f(csv);
        f << "x,return\n0.5,1.0\n";
    }

    CsvOracle oracle;

    // Happy: outputs map matches the first row of the CSV.
    {
        TestCase tc;
        tc.tolerance.abs = 1e-6;
        tc.oracle.kind = OracleSpec::Kind::Csv;
        tc.oracle.csvPath = csv;
        std::map<std::string, Value> outputs;
        outputs["x"]      = Value::makeFloat(0.5);
        outputs["return"] = Value::makeFloat(1.0);
        OracleVerdict v = oracle.check(tc, outputs);
        CHECK(v.passed);
    }

    // Mismatch: outputs differ from the row.
    {
        TestCase tc;
        tc.tolerance.abs = 1e-6;
        tc.oracle.kind = OracleSpec::Kind::Csv;
        tc.oracle.csvPath = csv;
        std::map<std::string, Value> outputs;
        outputs["x"]      = Value::makeFloat(0.5);
        outputs["return"] = Value::makeFloat(2.0);   // differs
        OracleVerdict v = oracle.check(tc, outputs);
        CHECK(!v.passed);
    }

    // Header-only CSV to empty rows error.
    {
        const std::string emptyCsv = td / "empty.csv";
        std::ofstream f(emptyCsv);
        f << "x,return\n";
        f.close();

        TestCase tc;
        tc.tolerance.abs = 1e-6;
        tc.oracle.kind = OracleSpec::Kind::Csv;
        tc.oracle.csvPath = emptyCsv;
        std::map<std::string, Value> outputs;
        outputs["x"]      = Value::makeFloat(0.5);
        outputs["return"] = Value::makeFloat(1.0);
        OracleVerdict v = oracle.check(tc, outputs);
        CHECK(!v.passed);
        CHECK(!v.failures.empty());
    }

    // Missing CSV file to load failure diagnostic.
    {
        TestCase tc;
        tc.tolerance.abs = 1e-6;
        tc.oracle.kind = OracleSpec::Kind::Csv;
        tc.oracle.csvPath = td / "no_such_file.csv";
        std::map<std::string, Value> outputs;
        outputs["return"] = Value::makeFloat(0.0);
        OracleVerdict v = oracle.check(tc, outputs);
        CHECK(!v.passed);
    }
}

// ---------------------------------------------------------------- SnapshotOracle gates

void testSnapshotOracleGates()
{
    Section s("SnapshotOracle three-gate matrix");

    SnapshotOracle oracle;
    TempDir td;

    auto makeTC = [&](const std::string& path, bool writable) {
        TestCase tc;
        tc.tolerance.abs = 1e-6;
        tc.oracle.kind = OracleSpec::Kind::Snapshot;
        tc.oracle.snapshotPath = path;
        tc.oracle.snapshotWritable = writable;
        return tc;
    };

    std::map<std::string, Value> outputs;
    outputs["return"] = Value::makeFloat(0.5);

    // Missing file, no gates to FAIL with "no snapshot recorded".
    {
        EnvGuard g1("CTL_TEST_UPDATE_SNAPSHOTS",    nullptr);
        EnvGuard g2("CTL_TEST_ALLOW_NEW_SNAPSHOTS", nullptr);
        TestCase tc = makeTC(td / "fresh.yaml", /*writable=*/false);
        OracleVerdict v = oracle.check(tc, outputs);
        CHECK(!v.passed);
        CHECK(!v.failures.empty());
        CHECK(!std::filesystem::exists(tc.oracle.snapshotPath));
    }

    // Missing file, gates 1+2+3 all set to write + PASS.
    const std::string newPath = td / "newfile.yaml";
    {
        EnvGuard g1("CTL_TEST_UPDATE_SNAPSHOTS",    "1");
        EnvGuard g2("CTL_TEST_ALLOW_NEW_SNAPSHOTS", "1");
        TestCase tc = makeTC(newPath, /*writable=*/true);
        OracleVerdict v = oracle.check(tc, outputs);
        CHECK(v.passed);
        CHECK(std::filesystem::exists(newPath));
    }

    // Existing file matches to PASS without env gates.
    {
        EnvGuard g1("CTL_TEST_UPDATE_SNAPSHOTS",    nullptr);
        EnvGuard g2("CTL_TEST_ALLOW_NEW_SNAPSHOTS", nullptr);
        TestCase tc = makeTC(newPath, /*writable=*/false);
        OracleVerdict v = oracle.check(tc, outputs);
        CHECK(v.passed);
    }

    // Existing file mismatches, no gates to FAIL with diagnostics.
    {
        EnvGuard g1("CTL_TEST_UPDATE_SNAPSHOTS",    nullptr);
        EnvGuard g2("CTL_TEST_ALLOW_NEW_SNAPSHOTS", nullptr);
        TestCase tc = makeTC(newPath, /*writable=*/false);
        std::map<std::string, Value> drift;
        drift["return"] = Value::makeFloat(0.6);
        OracleVerdict v = oracle.check(tc, drift);
        CHECK(!v.passed);
        CHECK(!v.failures.empty());
    }

    // Existing file mismatches, gates 1+2+force to overwrite + PASS.
    {
        EnvGuard g1("CTL_TEST_UPDATE_SNAPSHOTS", "force");
        TestCase tc = makeTC(newPath, /*writable=*/true);
        std::map<std::string, Value> drift;
        drift["return"] = Value::makeFloat(0.6);
        OracleVerdict v = oracle.check(tc, drift);
        CHECK(v.passed);

        // Re-check without gates: file should now contain 0.6 to match drift.
        EnvGuard g3("CTL_TEST_UPDATE_SNAPSHOTS", nullptr);
        TestCase tc2 = makeTC(newPath, /*writable=*/false);
        OracleVerdict v2 = oracle.check(tc2, drift);
        CHECK(v2.passed);
    }
}

// ---------------------------------------------------------------- InterpRunner

void testInterpRunnerRunUnit()
{
    Section s("InterpRunner run (unit)");

    InterpRunner r;
    initInterp(r);

    auto out = r.run("st_scalar::add_f",
                     {{"a", Value::makeFloat(1.5)}, {"b", Value::makeFloat(2.25)}},
                     "return");
    CHECK(out.count("return"));
    CHECK(out["return"].kind == Value::Kind::Float);
    CHECK_NEAR(out["return"].f, 3.75, 1e-9);
}

void testInterpRunnerSignature()
{
    Section s("InterpRunner signature");

    InterpRunner r;
    initInterp(r);

    auto sig = r.signature("st_scalar::square_v3");
    CHECK_EQ(sig.inputNames.size(),  size_t(1));
    CHECK(sig.inputNames[0]  == "v");
    CHECK_EQ(sig.outputNames.size(), size_t(1));
    CHECK(sig.outputNames[0] == "out");
    CHECK(sig.outputKinds[0] == InterpRunner::TypeKind::Array);
    CHECK(!sig.hasNonVoidReturn);

    // Struct return is reflected in returnKind.
    auto sigPoint = r.signature("st_scalar::make_point");
    CHECK(sigPoint.hasNonVoidReturn);
    CHECK(sigPoint.returnKind == InterpRunner::TypeKind::Struct);
}

void testInterpRunnerDefaultArg()
{
    Section s("InterpRunner default argument");

    // st_scalar::with_default(float x, float k = 2.0): x*k. Pass only x;
    // hasDefaultValue/setDefaultValue should fill in k=2.0.
    InterpRunner r;
    initInterp(r);

    auto out = r.run("st_scalar::with_default",
                     {{"x", Value::makeFloat(3.0)}},
                     "return");
    CHECK(out.count("return"));
    CHECK_NEAR(out["return"].f, 6.0, 1e-9);
}

void testInterpRunnerLoadError()
{
    Section s("InterpRunner load error");

    InterpRunner r;
    r.setModulePaths({"."});
    bool threw = false;
    try { r.loadModules({"definitely_not_a_module"}); }
    catch (const RunError& e) {
        threw = true;
        const std::string msg = e.what();
        CHECK_CONTAINS(msg, "definitely_not_a_module");
    }
    CHECK(threw);
}

void testTestKitDrainAndCtlNative()
{
    Section s("TestKit drain + ctl_native");

    InterpRunner r;
    r.setModulePaths({"."});
    r.loadModules({"ut_native"});

    // Passing test: 2 assertions, both passing.
    {
        auto a = r.runCtlNative("ut_native::test_pass");
        CHECK_EQ(a.size(), size_t(2));
        for (const auto& x : a) CHECK(x.passed);
    }

    // Failing test: 2 assertions, second fails.
    {
        auto a = r.runCtlNative("ut_native::test_fail");
        CHECK_EQ(a.size(), size_t(2));
        CHECK(a[0].passed);
        CHECK(!a[1].passed);
        // ExpectNearF: actual=0, expected=1, abs_err~1
        CHECK(a[1].kind == TestAssertion::Kind::ExpectNearF);
        CHECK_NEAR(a[1].abs_err, 1.0, 1e-6);
    }

    // Drain is per-call: a second runCtlNative on a passing test sees only
    // its own assertions, not the previous run's.
    {
        auto a = r.runCtlNative("ut_native::test_pass");
        CHECK_EQ(a.size(), size_t(2));
    }
}

// ---------------------------------------------------------------- Runner

namespace {

// Probe that records every reporter event in the order it was received.
class RecordingReporter: public Reporter {
public:
    void onSuiteBegin(const std::string& name, size_t total) override {
        events.push_back("begin:" + name + ":" + std::to_string(total));
    }
    void onCaseResult(const CaseResult& r) override {
        std::ostringstream os;
        os << "case:" << (r.testcase ? r.testcase->id : "<no id>")
           << ":" << static_cast<int>(r.outcome);
        events.push_back(os.str());
    }
    void onSuiteEnd(size_t p, size_t f, size_t e, size_t s, size_t u) override {
        std::ostringstream os;
        os << "end:" << p << "/" << f << "/" << e << "/" << s << "/" << u;
        events.push_back(os.str());
    }
    std::vector<std::string> events;
};

// Build a TestCase that runs identity_f against st_scalar with a chosen
// expected return value. If `expected` matches the input, the case passes;
// otherwise it fails through the oracle.
TestCase makeScalarCase(const std::string& id,
                        double inputX,
                        double expectedReturn,
                        bool knownFailure = false)
{
    TestCase tc;
    tc.id = id;
    tc.mode = TestCase::Mode::Unit;
    tc.modulePaths = {"."};
    tc.extraModules = {"st_scalar"};
    tc.functionName = "st_scalar::identity_f";
    tc.namedInputs["x"] = Value::makeFloat(inputX);
    tc.tolerance.abs = 1e-9;
    tc.oracle.kind = OracleSpec::Kind::Inline;
    tc.oracle.inlineExpected["return"] = Value::makeFloat(expectedReturn);
    tc.knownFailure = knownFailure;
    return tc;
}

} // namespace

void testRunSuiteCountsAndExit()
{
    Section s("runSuite counts + exit");

    Suite suite;
    suite.name = "synth";
    suite.tests.push_back(makeScalarCase("p", 0.5, 0.5));     // PASS
    suite.tests.push_back(makeScalarCase("f", 0.5, 0.6));     // FAIL
    {
        // ERROR: unresolvable function name.
        TestCase err;
        err.id = "e";
        err.mode = TestCase::Mode::Unit;
        err.modulePaths = {"."};
        err.extraModules = {"st_scalar"};
        err.functionName = "st_scalar::no_such_function";
        err.namedInputs["x"] = Value::makeFloat(0.0);
        err.oracle.kind = OracleSpec::Kind::Inline;
        err.oracle.inlineExpected["return"] = Value::makeFloat(0.0);
        suite.tests.push_back(err);
    }

    RecordingReporter rep;
    RunCounts counts = runSuite(suite, rep);
    CHECK_EQ(counts.passed,  size_t(1));
    CHECK_EQ(counts.failed,  size_t(1));
    CHECK_EQ(counts.errored, size_t(1));
    CHECK_EQ(counts.skipped, size_t(0));
    CHECK_EQ(counts.unexpectedPass, size_t(0));

    // Exit-relevant aggregate covers fail + error + xpass.
    CHECK_EQ(counts.nonPassing(), size_t(2));
    CHECK_EQ(counts.total(), size_t(3));

    // Reporter saw begin, then 3 cases (in order), then end.
    CHECK_EQ(rep.events.size(), size_t(5));
    CHECK_CONTAINS(rep.events[0], "begin:synth:3");
    CHECK_CONTAINS(rep.events[1], "case:p:");
    CHECK_CONTAINS(rep.events[2], "case:f:");
    CHECK_CONTAINS(rep.events[3], "case:e:");
    CHECK_CONTAINS(rep.events[4], "end:1/1/1/0/0");
}

void testRunSuiteKnownFailureInversion()
{
    Section s("runSuite known_failure inversion");

    Suite suite;
    suite.name = "kf";
    // A case that would ordinarily pass, marked known_failure to UnexpectedPass.
    suite.tests.push_back(makeScalarCase("xpass", 0.5, 0.5, /*knownFailure=*/true));
    // A case that would ordinarily fail, marked known_failure to Pass.
    suite.tests.push_back(makeScalarCase("xfail", 0.5, 0.6, /*knownFailure=*/true));

    RecordingReporter rep;
    RunCounts counts = runSuite(suite, rep);
    CHECK_EQ(counts.passed,         size_t(1));   // the inverted-fail
    CHECK_EQ(counts.unexpectedPass, size_t(1));   // the inverted-pass
    CHECK_EQ(counts.failed,         size_t(0));
    CHECK_EQ(counts.errored,        size_t(0));
}

void testRunSuiteEmpty()
{
    Section s("runSuite empty");

    Suite suite;
    suite.name = "empty";
    RecordingReporter rep;
    RunCounts counts = runSuite(suite, rep);
    CHECK_EQ(counts.total(), size_t(0));
    CHECK_EQ(counts.nonPassing(), size_t(0));

    // begin + end only -- no case events.
    CHECK_EQ(rep.events.size(), size_t(2));
    CHECK_CONTAINS(rep.events[0], "begin:empty:0");
    CHECK_CONTAINS(rep.events[1], "end:0/0/0/0/0");
}

// ---------------------------------------------------------------- CLI subprocess

namespace {

#ifndef CTLTEST_CLI_BINARY
#define CTLTEST_CLI_BINARY "ctltest"   // fallback for IDE indexing only
#endif

// Run a shell command and capture stdout+stderr + exit code.
struct CmdResult {
    int exitCode = -1;
    std::string output;
};

CmdResult runShell(const std::string& cmd)
{
    CmdResult r;
    const std::string full = cmd + " 2>&1";
    FILE* p = ctlPopen(full.c_str(), "r");
    if (!p) return r;
    char buf[1024];
    while (size_t n = std::fread(buf, 1, sizeof(buf), p)) {
        r.output.append(buf, n);
    }
    int rc = ctlPclose(p);
    r.exitCode = (rc == -1) ? -1 : ctlExitCode(rc);
    return r;
}

// Write a CLI-runnable suite YAML that resolves st_scalar.ctl from the
// process's current working directory (the unit-test binary dir).
std::string writeCliSuite(const std::string& path,
                          const std::string& modulePathsAbs,
                          const std::vector<std::string>& caseIds)
{
    std::ofstream f(path);
    f << "version: 1\n"
      << "suite: cli\n"
      << "modules: [st_scalar]\n"
      << "module_paths: ['" << modulePathsAbs << "']\n"
      << "tests:\n";
    for (const auto& id : caseIds) {
        f << "  - id: " << id << "\n"
          << "    function: st_scalar::identity_f\n"
          << "    inputs: { x: 0.25 }\n"
          << "    oracle: { inline: { return: 0.25 } }\n";
    }
    return path;
}

} // namespace

void testCliFilter()
{
    Section s("CLI --filter");

    TempDir td;
    const std::string yaml = td / "filter.yaml";
    const std::string moduleDir = std::filesystem::current_path().string();
    writeCliSuite(yaml, moduleDir, {"alpha", "beta"});

    // No filter: both cases run and appear in console output.
    {
        const std::string cmd = std::string(CTLTEST_CLI_BINARY) + " " + yaml;
        CmdResult r = runShell(cmd);
        CHECK_EQ(r.exitCode, 0);
        CHECK_CONTAINS(r.output, "alpha");
        CHECK_CONTAINS(r.output, "beta");
    }

    // Filter "alpha": only alpha appears in output.
    {
        const std::string cmd = std::string(CTLTEST_CLI_BINARY) + " --filter alpha " + yaml;
        CmdResult r = runShell(cmd);
        CHECK_EQ(r.exitCode, 0);
        CHECK_CONTAINS(r.output, "alpha");
        // beta should NOT appear in output (FilteringReporter suppresses it).
        CHECK(r.output.find("beta") == std::string::npos);
    }
}

void testCliReporterSelection()
{
    Section s("CLI --reporter");

    TempDir td;
    const std::string yaml = td / "rep.yaml";
    const std::string moduleDir = std::filesystem::current_path().string();
    writeCliSuite(yaml, moduleDir, {"a"});

    // TAP: header line marks the format.
    {
        const std::string cmd = std::string(CTLTEST_CLI_BINARY) + " --reporter tap " + yaml;
        CmdResult r = runShell(cmd);
        CHECK_EQ(r.exitCode, 0);
        CHECK_CONTAINS(r.output, "TAP version 14");
        CHECK_CONTAINS(r.output, "ok 1 - cli::a");
        CHECK_CONTAINS(r.output, "1..1");
    }

    // JUnit: <testsuites> root tag.
    {
        const std::string cmd = std::string(CTLTEST_CLI_BINARY) + " --reporter junit " + yaml;
        CmdResult r = runShell(cmd);
        CHECK_EQ(r.exitCode, 0);
        CHECK_CONTAINS(r.output, "<testsuites");
        CHECK_CONTAINS(r.output, "<testcase");
    }

    // Console (default): "summary:" line.
    {
        const std::string cmd = std::string(CTLTEST_CLI_BINARY) + " --no-color " + yaml;
        CmdResult r = runShell(cmd);
        CHECK_EQ(r.exitCode, 0);
        CHECK_CONTAINS(r.output, "summary:");
    }
}

void testCliBadArgs()
{
    Section s("CLI bad args");

    // Unknown option to exit 2 with "unknown option" on stderr.
    {
        CmdResult r = runShell(std::string(CTLTEST_CLI_BINARY) + " --bogus-flag");
        CHECK(r.exitCode != 0);
        CHECK_CONTAINS(r.output, "unknown option");
    }
    // Missing positional argument to exit 2 with usage.
    {
        CmdResult r = runShell(std::string(CTLTEST_CLI_BINARY));
        CHECK(r.exitCode != 0);
        CHECK_CONTAINS(r.output, "usage:");
    }
    // -h is a successful exit (returns 0) and prints usage.
    {
        CmdResult r = runShell(std::string(CTLTEST_CLI_BINARY) + " --help");
        CHECK_EQ(r.exitCode, 0);
        CHECK_CONTAINS(r.output, "usage:");
    }
}

// ---------------------------------------------------------------- YAML edge cases

void testYamlLoaderTagsAndAnchors()
{
    Section s("YamlLoader anchors");

    // Define a tolerance block once, alias it from two cases. Both cases
    // must end up with the same effective tolerance.
    std::string body =
        "version: 1\n"
        "modules: [st_scalar]\n"
        "tolerance_anchor: &tol\n"
        "  abs: 1.0e-4\n"
        "tests:\n"
        "  - id: a\n"
        "    function: st_scalar::identity_f\n"
        "    inputs: { x: 0.25 }\n"
        "    tolerance: *tol\n"
        "    oracle: { inline: { return: 0.25 } }\n"
        "  - id: b\n"
        "    function: st_scalar::identity_f\n"
        "    inputs: { x: 0.25 }\n"
        "    tolerance: *tol\n"
        "    oracle: { inline: { return: 0.25 } }\n";
    std::string p = writeTempFile(body, "anchors.yaml");
    Suite suite = loadSuite(p);
    CHECK_EQ(suite.tests.size(), size_t(2));
    CHECK(suite.tests[0].tolerance.abs.has_value());
    CHECK(suite.tests[1].tolerance.abs.has_value());
    CHECK(*suite.tests[0].tolerance.abs == *suite.tests[1].tolerance.abs);
    CHECK_NEAR(*suite.tests[0].tolerance.abs, 1.0e-4, 0.0);
}

void testYamlLoaderMissingRequiredFields()
{
    Section s("YamlLoader missing required fields");

    // No `modules:` key to required-key error.
    {
        std::string body =
            "version: 1\n"
            "tests:\n"
            "  - id: a\n"
            "    function: st_scalar::identity_f\n"
            "    inputs: { x: 0.25 }\n"
            "    oracle: { inline: { return: 0.25 } }\n";
        std::string p = writeTempFile(body, "missing_modules.yaml");
        bool threw = false;
        try { loadSuite(p); }
        catch (const LoadError& e) {
            threw = true;
            const std::string msg = e.what();
            CHECK_CONTAINS(msg, "modules");
        }
        CHECK(threw);
    }

    // Test missing `function:` to required-key error.
    {
        std::string body =
            "version: 1\n"
            "modules: [st_scalar]\n"
            "tests:\n"
            "  - id: a\n"
            "    inputs: { x: 0.25 }\n"
            "    oracle: { inline: { return: 0.25 } }\n";
        std::string p = writeTempFile(body, "missing_function.yaml");
        bool threw = false;
        try { loadSuite(p); }
        catch (const LoadError&) { threw = true; }
        CHECK(threw);
    }

    // Test missing `id:` to required-key error.
    {
        std::string body =
            "version: 1\n"
            "modules: [st_scalar]\n"
            "tests:\n"
            "  - function: st_scalar::identity_f\n"
            "    inputs: { x: 0.25 }\n"
            "    oracle: { inline: { return: 0.25 } }\n";
        std::string p = writeTempFile(body, "missing_id.yaml");
        bool threw = false;
        try { loadSuite(p); }
        catch (const LoadError&) { threw = true; }
        CHECK(threw);
    }
}

void testYamlLoaderUnknownKeys()
{
    Section s("YamlLoader unknown keys (permissive)");

    // Document the loader's behavior on unknown keys: it parses the known
    // schema and silently ignores extra keys at suite + test level. This
    // test pins the lenient contract -- tighten it in a future schema bump.
    std::string body =
        "version: 1\n"
        "modules: [st_scalar]\n"
        "extra_top_level_key: hello\n"
        "tests:\n"
        "  - id: a\n"
        "    function: st_scalar::identity_f\n"
        "    inputs: { x: 0.25 }\n"
        "    oracle: { inline: { return: 0.25 } }\n"
        "    extra_test_key: 42\n";
    std::string p = writeTempFile(body, "unknown_keys.yaml");
    Suite suite = loadSuite(p);
    CHECK_EQ(suite.tests.size(), size_t(1));
    CHECK(suite.tests[0].id == "a");
}

// ---------------------------------------------------------------- harness

#define TEST(name) \
    do { std::cout << "[ RUN  ] " #name << std::endl; name(); } while (0)

} // namespace

int main(int argc, char* argv[])
{
    auto want = [&](const char* n) {
        return argc < 2 || std::strcmp(argv[1], n) == 0;
    };

    if (want("Value"))                              TEST(testValue);
    if (want("ToleranceMerge"))                     TEST(testToleranceMerge);
    if (want("CompareTyped"))                       TEST(testCompareTyped);
    if (want("ValidateTolerancePaths"))             TEST(testValidateTolerancePaths);
    if (want("CsvTable"))                           TEST(testCsvTable);
    if (want("YamlLoader"))                         TEST(testYamlLoader);
    if (want("MarshalScalar"))                      TEST(testMarshalScalarRoundTrip);
    if (want("MarshalFlatArray"))                   TEST(testMarshalFlatArrayRoundTrip);
    if (want("MarshalStruct"))                      TEST(testMarshalStructRoundTrip);
    if (want("MarshalNestedArray"))                 TEST(testMarshalNestedArrayRoundTrip);
    if (want("MarshalStructWithArray"))             TEST(testMarshalStructWithArrayMemberRoundTrip);
    if (want("MarshalArrayOfStruct"))               TEST(testMarshalArrayOfStructRoundTrip);

    // v1.2 hardening -- additional unit tests.
    if (want("ValueDescribe"))                      TEST(testValueDescribe);
    if (want("ValueSeqAndMap"))                     TEST(testValueSeqAndMap);
    if (want("ToleranceEmptyAndPerField"))          TEST(testToleranceEmptyAndPerField);
    if (want("CompareTypedDeepNestedPaths"))        TEST(testCompareTypedDeepNestedPaths);
    if (want("CompareTypedMapMissingExtraKeys"))    TEST(testCompareTypedMapMissingExtraKeys);
    if (want("CompareTypedSeqSizeAndUlp"))          TEST(testCompareTypedSeqSizeAndUlp);
    if (want("ValidateTolerancePathsDeepAndPerChannel"))    TEST(testValidateTolerancePathsDeepAndPerChannel);
    if (want("DiagnosticFields"))                   TEST(testDiagnosticFields);
    if (want("ConsoleReporterPassFailMix"))         TEST(testConsoleReporterPassFailMix);
    if (want("TapReporterFormat"))                  TEST(testTapReporterFormat);
    if (want("JUnitReporterXml"))                   TEST(testJUnitReporterXml);
    if (want("ReporterTimeReporting"))              TEST(testReporterTimeReporting);

    if (want("ImageIORoundTripFloat"))              TEST(testImageIORoundTripFloat);
    if (want("ImageIOMultiChannel"))                TEST(testImageIOMultiChannel);
    if (want("ImageIOErrors"))                      TEST(testImageIOErrors);
    if (want("CompareImagesAbsRelUlp"))             TEST(testCompareImagesAbsRelUlp);
    if (want("CompareImagesMaxFailingPixels"))      TEST(testCompareImagesMaxFailingPixels);
    if (want("WriteFailureArtifacts"))              TEST(testWriteFailureArtifacts);
    if (want("InlineOracleHappyAndIgnore"))         TEST(testInlineOracleHappyAndIgnore);
    if (want("CsvOracleAlignment"))                 TEST(testCsvOracleAlignment);
    if (want("SnapshotOracleGates"))                TEST(testSnapshotOracleGates);
    if (want("InterpRunnerRunUnit"))                TEST(testInterpRunnerRunUnit);
    if (want("InterpRunnerSignature"))              TEST(testInterpRunnerSignature);
    if (want("InterpRunnerDefaultArg"))             TEST(testInterpRunnerDefaultArg);
    if (want("InterpRunnerLoadError"))              TEST(testInterpRunnerLoadError);
    if (want("TestKitDrainAndCtlNative"))           TEST(testTestKitDrainAndCtlNative);

    if (want("RunSuiteCountsAndExit"))              TEST(testRunSuiteCountsAndExit);
    if (want("RunSuiteKnownFailureInversion"))      TEST(testRunSuiteKnownFailureInversion);
    if (want("RunSuiteEmpty"))                      TEST(testRunSuiteEmpty);
    if (want("CliFilter"))                          TEST(testCliFilter);
    if (want("CliReporterSelection"))               TEST(testCliReporterSelection);
    if (want("CliBadArgs"))                         TEST(testCliBadArgs);
    if (want("YamlLoaderTagsAndAnchors"))           TEST(testYamlLoaderTagsAndAnchors);
    if (want("YamlLoaderMissingRequiredFields"))    TEST(testYamlLoaderMissingRequiredFields);
    if (want("YamlLoaderUnknownKeys"))              TEST(testYamlLoaderUnknownKeys);

    if (g_failures == 0) {
        std::cout << "all unit tests passed" << std::endl;
        return 0;
    }
    std::fprintf(stderr, "%d CHECK failure(s)\n", g_failures);
    return 1;
}
