#include "Marshal.h"

#include <CtlType.h>
#include <CtlTypeStorage.h>
#include <half.h>

#include <sstream>

namespace ctltest {

namespace {

std::string join(const std::string& base, const std::string& leaf) {
    if (base.empty()) return leaf;
    if (leaf.empty()) return base;
    // '/' is what CtlType::childElementV expects as the segment separator,
    // so the joined path is valid for both TypeStorage::set/get and the
    // human-readable MarshalError text. v1.0 used '.' and restricted
    // nested aggregates because childElementV had an off-by-one that
    // silently truncated multi-segment paths; v1.1 fixes the parser and
    // lifts the restriction.
    return base + "/" + leaf;
}

// --- toArg helpers --------------------------------------------------------

void setScalar(Ctl::TypeStorage& ts, const Ctl::DataTypePtr& dt,
               const Value& v, const std::string& path, size_t lane);

void setAggregate(Ctl::TypeStorage& ts, const Ctl::DataTypePtr& dt,
                  const Value& v, const std::string& path, size_t lane);

void setScalar(Ctl::TypeStorage& ts, const Ctl::DataTypePtr& dt,
               const Value& v, const std::string& path, size_t lane)
{
    switch (dt->cDataType()) {
      case Ctl::BoolTypeEnum: {
          // NB: BoolType reports IntTypeEnum in this codebase. We detect bool
          // via RTTI in setAggregate before dispatching here.
          bool b = (v.kind == Value::Kind::Bool) ? v.b
                 : (v.kind == Value::Kind::Int ? v.i != 0
                 : (v.kind == Value::Kind::UInt ? v.u != 0 : v.f != 0.0));
          ts.set(&b, 0, lane, 1, path);
          return;
      }
      case Ctl::IntTypeEnum: {
          int iv = (v.kind == Value::Kind::Bool) ? (v.b ? 1 : 0)
                 : (v.kind == Value::Kind::Int) ? static_cast<int>(v.i)
                 : (v.kind == Value::Kind::UInt) ? static_cast<int>(v.u)
                 : static_cast<int>(v.f);
          ts.set(&iv, 0, lane, 1, path);
          return;
      }
      case Ctl::UIntTypeEnum: {
          unsigned int uv = (v.kind == Value::Kind::Int) ? static_cast<unsigned int>(v.i)
                          : (v.kind == Value::Kind::UInt) ? static_cast<unsigned int>(v.u)
                          : (v.kind == Value::Kind::Bool) ? (v.b ? 1u : 0u)
                          : static_cast<unsigned int>(v.f);
          ts.set(&uv, 0, lane, 1, path);
          return;
      }
      case Ctl::HalfTypeEnum: {
          half h = static_cast<float>(v.kind == Value::Kind::Float ? v.f
                                     : v.kind == Value::Kind::Int ? (double)v.i
                                     : v.kind == Value::Kind::UInt ? (double)v.u
                                     : v.kind == Value::Kind::Bool ? (v.b ? 1.0 : 0.0)
                                     : 0.0);
          ts.set(&h, 0, lane, 1, path);
          return;
      }
      case Ctl::FloatTypeEnum: {
          float fv = static_cast<float>(v.kind == Value::Kind::Float ? v.f
                                       : v.kind == Value::Kind::Int ? (double)v.i
                                       : v.kind == Value::Kind::UInt ? (double)v.u
                                       : v.kind == Value::Kind::Bool ? (v.b ? 1.0 : 0.0)
                                       : 0.0);
          ts.set(&fv, 0, lane, 1, path);
          return;
      }
      case Ctl::StringTypeEnum: {
          if (v.kind != Value::Kind::String)
              throw MarshalError("expected string for arg/field at '" + path + "'");
          std::string s = v.s;
          ts.set(&s, 0, lane, 1, path);
          return;
      }
      default:
          throw MarshalError("setScalar called on non-scalar type at '" + path + "'");
    }
}

void setAggregate(Ctl::TypeStorage& ts, const Ctl::DataTypePtr& dt,
                  const Value& v, const std::string& path, size_t lane)
{
    // BoolType in this codebase reports IntTypeEnum from cDataType() -- detect
    // it via RTTI so we set with the right overload.
    if (Ctl::BoolTypePtr bp = dt.cast<Ctl::BoolType>()) {
        (void)bp;
        bool b = (v.kind == Value::Kind::Bool) ? v.b
               : (v.kind == Value::Kind::Int ? v.i != 0
               : (v.kind == Value::Kind::UInt ? v.u != 0 : v.f != 0.0));
        ts.set(&b, 0, lane, 1, path);
        return;
    }

    switch (dt->cDataType()) {
      case Ctl::ArrayTypeEnum: {
          Ctl::ArrayTypePtr at = dt.cast<Ctl::ArrayType>();
          if (!at) throw MarshalError("expected ArrayType at '" + path + "'");
          if (v.kind != Value::Kind::Seq)
              throw MarshalError("expected sequence for array at '" + path + "'");
          if ((int)v.seq.size() != at->size()) {
              std::ostringstream os;
              os << "array size mismatch at '" << path << "': expected "
                 << at->size() << ", got " << v.seq.size();
              throw MarshalError(os.str());
          }
          for (int i = 0; i < at->size(); ++i) {
              std::ostringstream os;
              os << i;
              setAggregate(ts, at->elementType(), v.seq[i], join(path, os.str()), lane);
          }
          return;
      }
      case Ctl::StructTypeEnum: {
          Ctl::StructTypePtr st = dt.cast<Ctl::StructType>();
          if (!st) throw MarshalError("expected StructType at '" + path + "'");
          if (v.kind != Value::Kind::Map)
              throw MarshalError("expected map for struct at '" + path + "'");
          const auto& members = st->members();
          // v1 rule: require full literal. Missing/extra keys to error.
          for (const auto& m : members) {
              auto it = v.map.find(m.name);
              if (it == v.map.end())
                  throw MarshalError("missing struct field '" + m.name + "' at '" + path + "'");
              setAggregate(ts, m.type, it->second, join(path, m.name), lane);
          }
          for (const auto& kv : v.map) {
              bool known = false;
              for (const auto& m : members) if (m.name == kv.first) { known = true; break; }
              if (!known) throw MarshalError("unknown struct field '" + kv.first + "' at '" + path + "'");
          }
          return;
      }
      default:
          setScalar(ts, dt, v, path, lane);
          return;
    }
}

// --- fromArg helpers ------------------------------------------------------

Value getAggregate(Ctl::TypeStorage& ts, const Ctl::DataTypePtr& dt,
                   const std::string& path, size_t lane)
{
    // BoolType: see note in setAggregate.
    if (Ctl::BoolTypePtr bp = dt.cast<Ctl::BoolType>()) {
        (void)bp;
        bool b = false;
        ts.get(&b, 0, lane, 1, path);
        return Value::makeBool(b);
    }

    switch (dt->cDataType()) {
      case Ctl::IntTypeEnum: {
          int iv = 0;
          ts.get(&iv, 0, lane, 1, path);
          return Value::makeInt(iv);
      }
      case Ctl::UIntTypeEnum: {
          unsigned int uv = 0;
          ts.get(&uv, 0, lane, 1, path);
          return Value::makeUInt(uv);
      }
      case Ctl::HalfTypeEnum: {
          half h(0.0f);
          ts.get(&h, 0, lane, 1, path);
          return Value::makeHalf(static_cast<double>(static_cast<float>(h)));
      }
      case Ctl::FloatTypeEnum: {
          float fv = 0.0f;
          ts.get(&fv, 0, lane, 1, path);
          return Value::makeFloat(static_cast<double>(fv));
      }
      case Ctl::StringTypeEnum: {
          std::string s;
          ts.get(&s, 0, lane, 1, path);
          return Value::makeString(std::move(s));
      }
      case Ctl::ArrayTypeEnum: {
          Ctl::ArrayTypePtr at = dt.cast<Ctl::ArrayType>();
          if (!at) throw MarshalError("expected ArrayType at '" + path + "'");
          std::vector<Value> out;
          out.reserve(at->size());
          for (int i = 0; i < at->size(); ++i) {
              std::ostringstream os;
              os << i;
              out.push_back(getAggregate(ts, at->elementType(), join(path, os.str()), lane));
          }
          return Value::makeSeq(std::move(out));
      }
      case Ctl::StructTypeEnum: {
          Ctl::StructTypePtr st = dt.cast<Ctl::StructType>();
          if (!st) throw MarshalError("expected StructType at '" + path + "'");
          std::map<std::string, Value> out;
          for (const auto& m : st->members()) {
              out.emplace(m.name, getAggregate(ts, m.type, join(path, m.name), lane));
          }
          return Value::makeMap(std::move(out));
      }
      default:
          throw MarshalError("unsupported type at '" + path + "'");
    }
}

} // namespace

void toArg(const Value& v, Ctl::FunctionArgPtr arg)
{
    if (!arg) throw MarshalError("null FunctionArg");
    Ctl::DataTypePtr dt = arg->type();
    setAggregate(*arg, dt, v, std::string(), 0);
}

void toArgLane(const Value& v, Ctl::FunctionArgPtr arg, size_t lane)
{
    if (!arg) throw MarshalError("null FunctionArg");
    Ctl::DataTypePtr dt = arg->type();
    setAggregate(*arg, dt, v, std::string(), arg->isVarying() ? lane : 0);
}

Value fromArg(Ctl::FunctionArgPtr arg)
{
    if (!arg) throw MarshalError("null FunctionArg");
    Ctl::DataTypePtr dt = arg->type();
    return getAggregate(*arg, dt, std::string(), 0);
}

Value fromArgLane(Ctl::FunctionArgPtr arg, size_t lane)
{
    if (!arg) throw MarshalError("null FunctionArg");
    Ctl::DataTypePtr dt = arg->type();
    return getAggregate(*arg, dt, std::string(), arg->isVarying() ? lane : 0);
}

void bindNamedInput(const std::map<std::string, Value>& inputs, Ctl::FunctionArgPtr arg)
{
    if (!arg) throw MarshalError("null FunctionArg");
    auto it = inputs.find(arg->name());
    if (it == inputs.end()) {
        if (arg->hasDefaultValue()) {
            arg->setDefaultValue();
            return;
        }
        throw MarshalError("no value supplied for required input '" + arg->name() + "'");
    }
    toArg(it->second, arg);
}

void bindNamedInputLane(const std::map<std::string, Value>& inputs,
                        Ctl::FunctionArgPtr arg, size_t lane)
{
    if (!arg) throw MarshalError("null FunctionArg");
    auto it = inputs.find(arg->name());
    if (it == inputs.end()) {
        // Defaults apply once, at the head of the batch.
        if (lane == 0 && arg->hasDefaultValue()) {
            arg->setDefaultValue();
            return;
        }
        if (lane == 0) {
            throw MarshalError("no value supplied for required input '" + arg->name() + "'");
        }
        // Non-zero lane with no row value: rely on default/previous bind.
        return;
    }
    // Uniform args: only lane 0 participates (one value per call).
    if (!arg->isVarying()) {
        if (lane == 0) toArg(it->second, arg);
        return;
    }
    toArgLane(it->second, arg, lane);
}

} // namespace ctltest
