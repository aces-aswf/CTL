///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

#ifndef INCLUDED_TEST_REQUIRE_H
#define INCLUDED_TEST_REQUIRE_H

#include <cstdio>
#include <cstdlib>

// REQUIRE(cond): runtime check that fires under NDEBUG (where assert()
// expands to (void)0).  Use in test code that needs to catch failures
// in Release/PGO builds, which is what CI runs by default.
#define REQUIRE(cond)                                                   \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr,                                        \
                "REQUIRE failed: %s\n  at %s:%d\n",                     \
                #cond, __FILE__, __LINE__);                             \
            std::abort();                                               \
        }                                                               \
    } while (0)

#endif
