#include "lix/libexpr/eval.hh"
#include "lix/libexpr/extra-primops.hh"
#include "lix/libexpr/value.hh"

#include <sstream>
#include <toml.hpp>

namespace nix {

Value prim_fromTOML(EvalState & state, Value ** args)
{
    auto toml = state.forceStringNoCtx(
        *args[0], noPos, "while evaluating the argument passed to builtins.fromTOML"
    );

    std::istringstream tomlStream(std::string{toml});

    auto visit = [&](this const auto & self, toml::value t) -> Value {
        switch (t.type()) {
        case toml::value_t::table: {
            auto table = toml::get<toml::table>(t);
            auto attrs = state.ctx.buildBindings(table.size());

            for (auto & elem : table) {
                attrs.insert(elem.first, self(elem.second));
            }

            return {NewValueAs::attrs, attrs};
        }
        case toml::value_t::array: {
            auto array = toml::get<std::vector<toml::value>>(t);

            size_t size = array.size();
            auto list = state.ctx.mem.newList(size);
            for (size_t i = 0; i < size; ++i) {
                list->elems[i] = self(array[i]);
            }
            return {NewValueAs::list, list};
        }
        case toml::value_t::boolean:
            return {NewValueAs::boolean, toml::get<bool>(t)};
        case toml::value_t::integer:
            return {NewValueAs::integer, toml::get<int64_t>(t)};
        case toml::value_t::floating:
            return {NewValueAs::floating, toml::get<NixFloat>(t)};
        case toml::value_t::string:
            return {NewValueAs::string, toml::get<std::string>(t)};
        case toml::value_t::local_datetime:
        case toml::value_t::offset_datetime:
        case toml::value_t::local_date:
        case toml::value_t::local_time:
            // NOLINTNEXTLINE(lix-foreign-exceptions)
            throw std::runtime_error("Dates and times are not supported");
        case toml::value_t::empty:
            return Value::VNULL;
        }
    };

    try {
        return visit(
            toml::parse(
                tomlStream,
                "fromTOML", /* the "filename" */
                toml::spec::v(1, 0, 0) // Be explicit that we are parsing TOML 1.0.0 without extensions
            )
        );
    } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions) // TODO: toml::syntax_error
        state.ctx.errors.make<EvalError>("while parsing TOML: %s", e.what()).debugThrow();
    }
}

}
