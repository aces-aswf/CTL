#ifndef CTLTEST_INLINE_ORACLE_H
#define CTLTEST_INLINE_ORACLE_H

#include "Oracle.h"

namespace ctltest {

class InlineOracle: public Oracle {
public:
    OracleVerdict check(const TestCase& tc,
                        const std::map<std::string, Value>& outputs) const override;
};

} // namespace ctltest

#endif
