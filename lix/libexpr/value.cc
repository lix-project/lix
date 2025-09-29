#include "lix/libexpr/value.hh"

#include <ostream>

#include "lix/libexpr/eval.hh"
#include "lix/libexpr/print.hh"


namespace nix
{

static const Value::List emptyListData{.size = 0};
Value Value::EMPTY_LIST{Value::list_t{}, &emptyListData};

const Value::Null Value::NULL_ACB = {{Value::Acb::tNull}};

static_assert(alignof(Value::String) >= Value::TAG_ALIGN);
static_assert(alignof(Bindings) >= Value::TAG_ALIGN);
static_assert(alignof(Value::List) >= Value::TAG_ALIGN);
static_assert(alignof(Value::Thunk) >= Value::TAG_ALIGN);
static_assert(alignof(Value::App) >= Value::TAG_ALIGN);

static_assert(alignof(Value::External) >= Value::Acb::TAG_ALIGN);
static_assert(alignof(Value::Float) >= Value::Acb::TAG_ALIGN);
static_assert(alignof(Value::Null) >= Value::Acb::TAG_ALIGN);
static_assert(alignof(Value::PrimOp) >= Value::Acb::TAG_ALIGN);
static_assert(alignof(Value::Int) >= Value::Acb::TAG_ALIGN);
static_assert(alignof(Value::Lambda) >= Value::Acb::TAG_ALIGN);

static void copyContextToValue(Value::String & s, const NixStringContext & context)
{
    if (!context.empty()) {
        size_t n = 0;
        s.context = gcAllocType<char const *>(context.size() + 1);
        for (auto & i : context)
            s.context[n++] = gcCopyStringIfNeeded(i.to_string());
        s.context[n] = 0;
    }
}

Value::Value(primop_t, PrimOp & primop) : raw(tag(tAuxiliary, &primop)) {}

void Value::print(EvalState & state, std::ostream & str, PrintOptions options)
{
    printValue(state, str, *this, options);
}

bool Value::isTrivial() const
{
    return internalType() != tApp
        && (internalType() != tThunk
            || (thunk().expr->try_cast<ExprSet>()
                && static_cast<ExprSet *>(thunk().expr)->dynamicAttrs.empty())
            || thunk().expr->try_cast<ExprLambda>() || thunk().expr->try_cast<ExprList>());
}

void Value::mkPrimOp(PrimOp * p)
{
    *this = {NewValueAs::primop, *p};
}

void Value::mkString(std::string_view s)
{
    mkString(gcCopyStringIfNeeded(s));
}

void Value::mkString(std::string_view s, const NixStringContext & context)
{
    mkString(s);
    copyContextToValue(*untag<String *>(), context);
}

void Value::mkStringMove(const char * s, const NixStringContext & context)
{
    mkString(s);
    copyContextToValue(*untag<String *>(), context);
}

void Value::mkPath(const SourcePath & path)
{
    *this = Value(NewValueAs::path, path);
}

#ifndef __APPLE__
[[gnu::section(".debug_gdb_scripts"), gnu::used]]
static const char printer_script[] =
    "\4"
    R"(lix-value-printer
class ValuePrinter(gdb.ValuePrinter):
    def __init__(self, val):
          self._val = val
          self._t_thunk = gdb.lookup_type('nix::Value::Thunk').pointer()
          self._t_app = gdb.lookup_type('nix::Value::App').pointer()
          self._t_int = gdb.lookup_type('intptr_t')
          self._t_string = gdb.lookup_type('nix::Value::String').pointer()
          self._t_attrs = gdb.lookup_type('nix::Bindings').pointer()
          self._t_list = gdb.lookup_type('nix::Value::List').pointer()
          self._t_aux = gdb.lookup_type('nix::Value::Acb').pointer()

    def _addr(self, t = None):
          addr = self._val['raw'] & ~self._val['TAG_MASK']
          return addr.cast(t).referenced_value() if t else addr

    def _tag(self):
          return self._val['raw'] & self._val['TAG_MASK']
    def _v_int(self):
          return self._val['raw'].cast(self._t_int) >> self._val['TAG_BITS']

    def to_string(self):
          if self._tag() == 7:
                return self._addr(self._t_aux)
          elif not self.children():
                return f'undecoded {self._val['raw'].format_string(format='x')}'
          return None

    def children(self):
          match self._tag():
                case 0:
                      return [('thunk', self._addr(self._t_thunk))]
                case 1:
                      return [('app', self._addr(self._t_app))]
                case 2:
                      return [('int', self._v_int())]
                case 3:
                      return [('bool', (self._val['raw'] >> self._val['TAG_BITS']) != 0)]
                case 4:
                      return [('string', self._addr(self._t_string))]
                case 5:
                      return [('attrs', self._addr(self._t_attrs))]
                case 6:
                      return [('list', self._addr(self._t_list))]
                case _:
                      return []

class ThunkPrinter(gdb.ValuePrinter):
    def __init__(self, val):
          self._val = val
          self._t_value = gdb.lookup_type('nix::Value')
          self._t_env = gdb.lookup_type('nix::Env').pointer()

    def children(self):
          if self._val['expr'] == 0:
                return [('result', self._val['_result'].cast(self._t_value))]
          else:
                return [
                      ('env', self._val['_env'].cast(self._t_env).referenced_value()),
                      ('expr', self._val['expr'].referenced_value()),
                ]

class AppPrinter(gdb.ValuePrinter):
    def __init__(self, val):
          self._val = val
          self._t_value = gdb.lookup_type('nix::Value')

    def children(self):
          if self._val['_n'] + 1 == 0:
                return [('result', self._val['_left'].cast(self._t_value))]
          else:
                n = int(self._val['_n'])
                arr = self._t_value.array(0, n - 1)
                return [
                      ('fn', self._val['_left'].cast(self._t_value)),
                      ('n', n),
                      ('args', self._val['_args'][0].cast(arr)),
                ]

class AcbPrinter(gdb.ValuePrinter):
    def __init__(self, val):
          self._val = val
          self._t_external = gdb.lookup_type('nix::Value::External').pointer()
          self._t_float = gdb.lookup_type('nix::Value::Float').pointer()
          self._t_primop = gdb.lookup_type('nix::Value::PrimOp')
          self._t_primopdetails = gdb.lookup_type('nix::PrimOpDetails')
          self._t_lambda = gdb.lookup_type('nix::Value::Lambda').pointer()
          self._t_int = gdb.lookup_type('nix::Value::Int').pointer()

    def _addr(self, t = None):
          addr = self._val['raw'] & ~self._val['TAG_MASK']
          return addr.cast(t).referenced_value() if t else addr

    def _tag(self):
          return self._val['raw'] & self._val['TAG_MASK']

    def to_string(self):
          match self._tag():
                case 2:
                      return 'null'
                case 3:
                      op = self._val.cast(self._t_primop).cast(self._t_primopdetails)
                      return f'primop {op['name']}'
                case _:
                      return None

    def children(self):
          match self._tag():
                case 0:
                      return [('external', self._addr(self._t_external))]
                case 1:
                      return [('float', self._addr(self._t_float)['value'])]
                case 4:
                      return [('lambda', self._addr(self._t_lambda))]
                case 5:
                      return [('int', self._addr(self._t_int)['value'])]
                case _:
                      return []

def value_lookup_function(val):
    lookup_tag = val.type.tag
    if lookup_tag is None:
        return None
    if lookup_tag == 'nix::Value':
        return ValuePrinter(val)
    elif lookup_tag == 'nix::Value::Thunk':
        return ThunkPrinter(val)
    elif lookup_tag == 'nix::Value::App':
        return AppPrinter(val)
    elif lookup_tag == 'nix::Value::Acb':
        return AcbPrinter(val)
    return None

def register_printers(objfile):
      objfile.pretty_printers.append(value_lookup_function)

register_printers(gdb.current_objfile())
)";
#endif
}
