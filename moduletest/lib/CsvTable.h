#ifndef CTLTEST_CSV_TABLE_H
#define CTLTEST_CSV_TABLE_H

#include "CaseModel.h"

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace ctltest {

class CsvError: public std::runtime_error {
public:
    CsvError(const std::string& path, size_t line, const std::string& msg);
    const std::string& path() const { return _path; }
    size_t line() const { return _line; }
private:
    std::string _path;
    size_t _line;
};

// Minimal CSV reader. Supports:
//   - header row (first non-empty, non-comment line)
//   - comma-separated fields
//   - double-quoted fields with escaped quotes ("" to ")
//   - CRLF or LF line endings
//   - '#' comment lines outside of quoted fields (convenient for fixtures)
//
// Does NOT support: embedded newlines inside quoted fields, alternate delimiters.
// Deliberately simple; grow if a concrete test actually needs more.
struct CsvTable {
    std::string path;                                // source file (diagnostics)
    std::vector<std::string> columns;                // header row, in-order
    std::vector<std::vector<std::string>> rows;      // cells, in header order

    size_t colIndex(const std::string& name) const;  // returns size_t(-1) if absent
};

CsvTable readCsv(const std::string& path);

// Parse a single CSV cell into a Value. Tries bool/int/float/string in order,
// mirroring YAML's scalar heuristic so the two front-ends behave consistently.
// Empty cells become an empty string Value.
Value cellToValue(const std::string& cell);

// Materialize a CsvTable into one map<col_name, Value> per row. Column order
// is preserved only inside the CsvTable itself; the returned maps are keyed by
// header name.
std::vector<std::map<std::string, Value>> tableAsRows(const CsvTable& t);

} // namespace ctltest

#endif
