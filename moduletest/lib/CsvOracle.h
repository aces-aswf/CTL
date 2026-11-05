#ifndef CTLTEST_CSV_ORACLE_H
#define CTLTEST_CSV_ORACLE_H

#include "Oracle.h"

namespace ctltest {

// Expects one row at `tc.oracle.csvPath` for unit-mode tests. Columns map to
// output argument names (including the return value, addressed via
// tc.returnsName). Any additional rows in the CSV are ignored for unit mode.
//
// Sweep-mode tests do NOT go through this single-row Oracle; the Runner reads
// the CSV row-by-row and drives per-lane comparisons itself. See Runner::runOne.
class CsvOracle: public Oracle {
public:
    OracleVerdict check(const TestCase& tc,
                        const std::map<std::string, Value>& outputs) const override;
};

} // namespace ctltest

#endif
