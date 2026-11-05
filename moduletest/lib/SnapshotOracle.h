#ifndef CTLTEST_SNAPSHOT_ORACLE_H
#define CTLTEST_SNAPSHOT_ORACLE_H

#include "Oracle.h"

namespace ctltest {

// SnapshotOracle -- record-and-replay oracle backed by a YAML file.
//
// Three-gate write protection:
//   gate 1: env var CTL_TEST_UPDATE_SNAPSHOTS is set
//   gate 2: per-test oracle.snapshot.writable: true
//   gate 3: for *new* files, also CTL_TEST_ALLOW_NEW_SNAPSHOTS must be set
//           (defensive -- CI should never silently accept "no snapshot yet")
//
// Behavior:
//   - file exists, match           to PASS
//   - file exists, mismatch,
//     gates 1+2+"force"            to overwrite, PASS
//   - file exists, mismatch, no gates to FAIL with per-key diagnostics
//   - file missing, gates 1+2+3    to write, PASS
//   - file missing, no gates       to FAIL with "no snapshot recorded"
//
// The on-disk format mirrors oracle.inline: a single top-level mapping
// <output_name> to <value>. This lets authors promote a snapshot to an
// inline oracle (or vice-versa) with no reformatting.
class SnapshotOracle: public Oracle {
public:
    OracleVerdict check(const TestCase& tc,
                        const std::map<std::string, Value>& outputs) const override;
};

} // namespace ctltest

#endif
