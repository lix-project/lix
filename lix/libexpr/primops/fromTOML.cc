#include "libutil/types.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/extra-primops.hh"
#include "lix/libexpr/value.hh"
#include "lix/lix-rs/main.gen.hh"
#include "lix/lix-rs/utils.hh"

#include <sstream>

namespace nix {

Value prim_fromTOML(EvalState & state, Value ** args)
{
    auto toml = state.forceStringNoCtx(
        *args[0], noPos, "while evaluating the argument passed to builtins.fromTOML"
    );

    auto visit = [&](this const auto & self, rust::Ref<rust::toml::Value> t) -> Value {
        return std::visit(
            overloaded{
                [&](rust::Ref<rust::toml::Value::String> s) {
                    return Value{NewValueAs::string, to_std_string_view(s.f0)};
                },
                [&](rust::Ref<rust::toml::Value::Integer> s) { return Value{NewValueAs::integer, s.f0}; },
                [&](rust::Ref<rust::toml::Value::Float> s) { return Value{NewValueAs::floating, s.f0}; },
                [&](rust::Ref<rust::toml::Value::Boolean> s) {
                    return Value{NewValueAs::boolean, s.f0.clone()};
                },
                [&](rust::Ref<rust::toml::Value::Datetime> s, nix::NeverAsync = {}) -> Value {
                    state.ctx.errors.make<EvalError>("while parsing TOML: Dates and times are not supported")
                        .debugThrow();
                },
                [&](rust::Ref<rust::toml::Value::Array> a) {
                    auto & arr = a.f0;
                    auto list = state.ctx.mem.newList(arr.len());
                    size_t i = 0;
                    for (auto & el : arr.iter()) {
                        list->elems[i] = self(el);
                        i++;
                    }
                    return Value{NewValueAs::list, list};
                },
                [&](rust::Ref<rust::toml::Value::Table> t) {
                    auto & tab = t.f0;
                    auto attrs = state.ctx.buildBindings(tab.len());

                    for (auto & elem : tab.iter()) {
                        attrs.insert(to_std_string(elem.f0), self(elem.f1));
                    }

                    return Value{NewValueAs::attrs, attrs};
                }
            },
            t.match_ref()
        );
    };

    return match_result(
        rust::lix::toml::from_slice(rust::to_slice(toml)),
        [&](auto v) { return visit(v); },
        [&](auto err) -> Value {
            state.ctx.errors.make<EvalError>("while parsing TOML: %s", to_std_string(err.to_string()))
                .debugThrow(nix::always_progresses);
        }
    );
}

}
