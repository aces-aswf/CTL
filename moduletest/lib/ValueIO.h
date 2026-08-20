#ifndef CTLTEST_VALUE_IO_H
#define CTLTEST_VALUE_IO_H

#include "CaseModel.h"

#include <iosfwd>
#include <stdexcept>
#include <string>

namespace ctltest {

class ValueIOError: public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Load a top-level mapping <string, Value> from a YAML file. Shape matches
// oracle.inline: every top-level key is an output name. Used by SnapshotOracle.
std::map<std::string, Value> loadValueMap(const std::string& path);

// Write a top-level mapping <string, Value> to `path` in YAML form. Formatting
// is deterministic (alphabetical keys, numeric scalars with enough precision
// to round-trip a double). Atomic: writes to a tmp file next to `path` and
// renames on success.
void saveValueMap(const std::string& path,
                  const std::map<std::string, Value>& values);

} // namespace ctltest

#endif
