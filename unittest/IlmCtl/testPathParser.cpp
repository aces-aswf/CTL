// Regression coverage for Ctl::Type::childElementV multi-segment path
// parsing. The parser had historically been exercised in-tree only with
// single-segment paths (see ctlrender/transform.cc using "%d") and the
// moduletest framework uncovered a silent off-by-one that truncated every
// path segment followed by another '/'. This test addresses:
//
//   * fixed 2-D float arrays addressed as "i/j"
//   * structs whose member is itself an array, addressed as "member/k"
//   * arrays whose element is a struct, addressed as "i/field"
//
// Each case round-trips a unique marker through TypeStorage::set/get so an
// offset error manifests as a wrong readback, not a silent no-op.
//
// The checks use CHECK(cond) rather than assert() so that regressions are
// caught in release builds too (assert() is defined to nothing under
// NDEBUG, which is the default in this project's Release configuration).

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include <CtlFunctionCall.h>
#include <CtlSimdInterpreter.h>
#include <CtlType.h>
#include <CtlTypeStorage.h>

#include "testPathParser.h"

using namespace Ctl;

namespace {

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                         "testPathParser: CHECK(%s) failed at %s:%d\n",    \
                         #cond, __FILE__, __LINE__);                       \
            std::abort();                                                  \
        }                                                                  \
    } while (0)

void
testMatrix3x3(SimdInterpreter &interp)
{
    FunctionCallPtr fn = interp.newFunctionCall("pathParser::fnMat3");
    CHECK(fn);
    FunctionArgPtr m = fn->outputArg(0);
    CHECK(m);

    // Populate every cell with a distinct marker through the "i/j" path.
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float v = static_cast<float>(i * 10 + j);
            std::string path = std::to_string(i) + "/" + std::to_string(j);
            m->set(&v, 0, 0, 1, path);
        }
    }

    // Read each cell back; a path-parser off-by-one would collapse the
    // first segment to empty and write/read at offset 0 for every call,
    // making every read return the last-written value.
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float got = -1.0f;
            std::string path = std::to_string(i) + "/" + std::to_string(j);
            m->get(&got, 0, 0, 1, path);
            float want = static_cast<float>(i * 10 + j);
            CHECK(got == want);
        }
    }
}

void
testStructWithArrayMember(SimdInterpreter &interp)
{
    FunctionCallPtr fn = interp.newFunctionCall("pathParser::fnStructWithArr");
    CHECK(fn);
    FunctionArgPtr w = fn->outputArg(0);
    CHECK(w);

    int n_in = 42;
    w->set(&n_in, 0, 0, 1, std::string("n"));

    for (int k = 0; k < 3; ++k) {
        float v = 1.5f + static_cast<float>(k);
        w->set(&v, 0, 0, 1, "v/" + std::to_string(k));
    }

    int n_out = 0;
    w->get(&n_out, 0, 0, 1, std::string("n"));
    CHECK(n_out == 42);

    for (int k = 0; k < 3; ++k) {
        float got = 0.0f;
        w->get(&got, 0, 0, 1, "v/" + std::to_string(k));
        float want = 1.5f + static_cast<float>(k);
        CHECK(got == want);
    }
}

void
testArrayOfStruct(SimdInterpreter &interp)
{
    FunctionCallPtr fn = interp.newFunctionCall("pathParser::fnArrOfStruct");
    CHECK(fn);
    FunctionArgPtr arr = fn->outputArg(0);
    CHECK(arr);

    for (int i = 0; i < 4; ++i) {
        float x = static_cast<float>(i);
        float y = static_cast<float>(i) + 0.5f;
        arr->set(&x, 0, 0, 1, std::to_string(i) + "/x");
        arr->set(&y, 0, 0, 1, std::to_string(i) + "/y");
    }

    for (int i = 0; i < 4; ++i) {
        float gx = 0.0f, gy = 0.0f;
        arr->get(&gx, 0, 0, 1, std::to_string(i) + "/x");
        arr->get(&gy, 0, 0, 1, std::to_string(i) + "/y");
        CHECK(gx == static_cast<float>(i));
        CHECK(gy == static_cast<float>(i) + 0.5f);
    }
}

} // namespace

void
testPathParser()
{
    std::cout << "Testing path parser multi-segment handling" << std::endl;
    try {
        SimdInterpreter interp;
        interp.loadModule("testPathParser");

        testMatrix3x3(interp);
        testStructWithArrayMember(interp);
        testArrayOfStruct(interp);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "testPathParser caught: %s\n", e.what());
        std::abort();
    }
    std::cout << "ok\n" << std::endl;
}
