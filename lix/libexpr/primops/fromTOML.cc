#include "lix/libexpr/eval.hh"
#include "lix/libexpr/extra-primops.hh"

#include <sstream>
#include <toml.hpp>

namespace nix {

void prim_fromTOML(EvalState & state, Value ** args, Value & val)
{
    auto toml = state.forceStringNoCtx(
        *args[0], noPos, "while evaluating the argument passed to builtins.fromTOML"
    );

    std::istringstream tomlStream(std::string{toml});

    auto visit = [&](this const auto & self, Value & v, toml::value t) -> void {
        switch (t.type()) {
        case toml::value_t::table: {
            auto table = toml::get<toml::table>(t);

            size_t size = 0;
            for (auto & i : table) {
                (void) i;
                size++;
            }

            auto attrs = state.ctx.buildBindings(size);

            for (auto & elem : table) {
                self(attrs.alloc(elem.first), elem.second);
            }

            v.mkAttrs(attrs);
        } break;
        case toml::value_t::array: {
            auto array = toml::get<std::vector<toml::value>>(t);

            size_t size = array.size();
            v = state.ctx.mem.newList(size);
            for (size_t i = 0; i < size; ++i) {
                self(*(v.listElems()[i] = state.ctx.mem.allocValue()), array[i]);
            }
        } break;
        case toml::value_t::boolean:
            v.mkBool(toml::get<bool>(t));
            break;
        case toml::value_t::integer:
            v.mkInt(toml::get<int64_t>(t));
            break;
        case toml::value_t::floating:
            v.mkFloat(toml::get<NixFloat>(t));
            break;
        case toml::value_t::string:
            v.mkString(toml::get<std::string>(t));
            break;
        case toml::value_t::local_datetime:
        case toml::value_t::offset_datetime:
        case toml::value_t::local_date:
        case toml::value_t::local_time: {
            if (experimentalFeatureSettings.isEnabled(Xp::ParseTomlTimestamps)) {
                auto attrs = state.ctx.buildBindings(2);
                attrs.alloc("_type").mkString("timestamp");
                std::ostringstream s;
                s << t;
                attrs.alloc("value").mkString(s.str());
                v.mkAttrs(attrs);
            } else {
                // NOLINTNEXTLINE(lix-foreign-exceptions)
                throw std::runtime_error("Dates and times are not supported");
            }
        } break;
        case toml::value_t::empty:
            v.mkNull();
            break;
        }
    };

    try {
        visit(val, toml::parse(tomlStream, "fromTOML" /* the "filename" */));
    } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions) // TODO: toml::syntax_error
        state.ctx.errors.make<EvalError>("while parsing TOML: %s", e.what()).debugThrow();
    }
}

}
