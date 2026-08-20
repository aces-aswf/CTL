// ctltest_run_one -- executes a single YAML suite file and exits non-zero if
// any case did not pass. Intended to be invoked by CTest via add_test().

#include "CaseModel.h"
#include "ConsoleReporter.h"
#include "Runner.h"
#include "YamlLoader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

static void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s <yaml_path> [--no-color]\n"
                 "  runs every case in the suite and exits non-zero on any\n"
                 "  non-pass result.\n",
                 argv0);
}

int main(int argc, char** argv) {
    std::string yamlPath;
    bool color = true;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--no-color") == 0) {
            color = false;
        } else if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", a);
            usage(argv[0]);
            return 2;
        } else if (yamlPath.empty()) {
            yamlPath = a;
        } else {
            std::fprintf(stderr, "too many positional args\n");
            usage(argv[0]);
            return 2;
        }
    }
    if (yamlPath.empty()) {
        usage(argv[0]);
        return 2;
    }

    ctltest::Suite suite;
    try {
        suite = ctltest::loadSuite(yamlPath);
    } catch (const ctltest::LoadError& e) {
        std::fprintf(stderr, "load error: %s\n", e.what());
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "load error: %s\n", e.what());
        return 2;
    }

    ctltest::ConsoleReporter reporter(std::cout, color);
    ctltest::RunCounts counts = ctltest::runSuite(suite, reporter);

    return counts.nonPassing() == 0 ? 0 : 1;
}
