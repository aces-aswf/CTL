#ifndef CTLTEST_MARSHAL_H
#define CTLTEST_MARSHAL_H

#include "CaseModel.h"

#include <CtlFunctionCall.h>
#include <string>

namespace ctltest {

class MarshalError: public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Copy a ctltest Value into a CTL FunctionArg (uniform, lane 0 for varying).
// Uses TypeStorage::set with dotted-path into the argument's Type tree.
// Throws MarshalError if the Value shape doesn't match the arg's declared type.
void toArg(const Value& v, Ctl::FunctionArgPtr arg);

// Same as toArg but writes into a specific lane of a varying argument.
// For uniform args the lane is ignored (only lane 0 is meaningful).
void toArgLane(const Value& v, Ctl::FunctionArgPtr arg, size_t lane);

// Copy a CTL FunctionArg's value (uniform, lane 0 for varying) back into a
// freshly-constructed ctltest Value. Uses TypeStorage::get with dotted path.
Value fromArg(Ctl::FunctionArgPtr arg);

// Same as fromArg but reads from a specific lane of a varying argument.
Value fromArgLane(Ctl::FunctionArgPtr arg, size_t lane);

// If v has a mapped value for a named input, bind it; otherwise if the arg has
// a default value, apply it; otherwise throw MarshalError.
// Mirrors the binding pattern in ctlrender/transform.cc.
void bindNamedInput(const std::map<std::string, Value>& inputs, Ctl::FunctionArgPtr arg);

// Lane-aware bind for sweep dispatch. For varying args, writes into lane `lane`.
// For uniform args, writes only when lane == 0 (subsequent lanes no-op).
// Missing inputs fall back to hasDefaultValue() (only applied at lane 0 too).
void bindNamedInputLane(const std::map<std::string, Value>& inputs,
                        Ctl::FunctionArgPtr arg, size_t lane);

} // namespace ctltest

#endif
