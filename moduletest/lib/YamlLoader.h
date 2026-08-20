#ifndef CTLTEST_YAML_LOADER_H
#define CTLTEST_YAML_LOADER_H

#include "CaseModel.h"

#include <stdexcept>
#include <string>

namespace ctltest {

class LoadError: public std::runtime_error {
public:
    LoadError(const std::string& path, int line, int col, const std::string& msg)
        : std::runtime_error(format(path, line, col, msg)),
          _path(path), _line(line), _col(col) {}

    const std::string& path() const { return _path; }
    int line() const { return _line; }
    int col()  const { return _col;  }

private:
    static std::string format(const std::string& path, int line, int col, const std::string& msg);
    std::string _path;
    int _line;
    int _col;
};

// Parse a suite YAML file into a Suite. Throws LoadError with file/line/col on
// malformed input. v0.1 schema subset:
//
//   version: 1
//   suite: <name>                          # optional
//   modules: [ACESlib.RRT, ...]            # required; at least one
//   module_paths: [../ctl]                 # optional; resolved vs. suite file dir
//   defaults:
//     tolerance: { abs: 1e-5 }             # optional
//   tests:
//     - id: <id>                           # required
//       description: <string>              # optional
//       function: <ns::name>               # required; "ns::fn" or "fn" within modules
//       inputs:                            # map<name, Value>
//         aces: [0.18, 0.18, 0.18]
//         amount: 1.0
//       returns_name: out                  # optional; default "return"
//       tolerance: { abs: 1e-5,            # optional
//                    per_field: { out.r: { abs: 5e-4 } } }
//       oracle:
//         inline:
//           return: [0.18, 0.18, 0.18]
//       ignore_outputs: [foo, bar]         # optional
//       tags: [roundtrip]                  # optional
//       known_failure: false               # optional; default false
//
Suite loadSuite(const std::string& yamlPath);

} // namespace ctltest

#endif
