#include "lix/libexpr/json-to-value.hh"
#include "gc-alloc.hh"
#include "libutil/types.hh"
#include "lix/libexpr/value.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libutil/json.hh"

#include <limits>
#include <variant>

namespace nix {

// for more information, refer to
// https://github.com/nlohmann/json/blob/master/include/nlohmann/detail/input/json_sax.hpp
class JSONSax : nlohmann::json_sax<JSON> {
    struct WIPObject
    {
        GcMap<Symbol, Value> entries;
        Symbol nextKey;
    };
    using WIPArray = GcVector<Value>;

    // A stack of all of the currently opened and not yet closed JSON objects and arrays at the current moment
    // of parsing
    std::vector<std::variant<WIPObject, WIPArray>> stack;

    std::optional<Value> final_value;

    void addValue(Value v)
    {
        if (stack.size() == 0) {
            assert(!final_value);
            final_value = v;
            return;
        }
        std::visit(
            overloaded{
                [&](WIPObject & currentObject) {
                    currentObject.entries.insert_or_assign(currentObject.nextKey, v);
                    currentObject.nextKey = {}; // clear the current symbol
                },
                [&](WIPArray & arr) { arr.push_back(v); }
            },
            stack.back()
        );
    }

    EvalState & state;

public:
    JSONSax(EvalState & state) : state(state) {};

    Value result()
    {
        return final_value.value();
    }

    bool null() override
    {
        addValue(Value::VNULL);
        return true;
    }

    bool boolean(bool val) override
    {
        addValue({NewValueAs::boolean, val});
        return true;
    }

    bool number_integer(number_integer_t val) override
    {
        addValue({NewValueAs::integer, val});
        return true;
    }

    bool number_unsigned(number_unsigned_t val_) override
    {
        if (val_ > std::numeric_limits<NixInt::Inner>::max()) {
            // Parse as a float for consistency with signed integers
            // and interoperability with JSON’s single numeric type.
            return number_float(static_cast<number_float_t>(val_), "");
        }
        NixInt::Inner val = val_;
        addValue({NewValueAs::integer, val});
        return true;
    }

    bool number_float(number_float_t val, const string_t & s) override
    {
        addValue({NewValueAs::floating, val});
        return true;
    }

    bool string(string_t & val) override
    {
        addValue({NewValueAs::string, val});
        return true;
    }

#if NLOHMANN_JSON_VERSION_MAJOR >= 3 && NLOHMANN_JSON_VERSION_MINOR >= 8
    bool binary(binary_t&) override
    {
        // This function ought to be unreachable
        assert(false);
        return true;
    }
#endif

    bool start_object(std::size_t len) override
    {
        stack.emplace_back(std::in_place_type<WIPObject>);
        return true;
    }

    bool key(string_t & name) override
    {
        auto & back = stack.back();
        std::visit(
            overloaded{
                [&](WIPObject & frame) {
                    assert(!frame.nextKey);
                    frame.nextKey = state.ctx.symbols.create(name);
                },
                [](const WIPArray &) { assert(false && "tried adding a json key to an array value??"); }
            },
            back
        );
        return true;
    }

    bool end_object() override {
        auto back = std::move(stack.back());
        stack.pop_back();
        std::visit(
            overloaded{
                [&](const WIPObject & frame) {
                    auto attrs2 = state.ctx.buildBindings(frame.entries.size());
                    for (auto & i : frame.entries) {
                        attrs2.insert(i.first, i.second);
                    }
                    addValue({NewValueAs::attrs, attrs2.alreadySorted()});
                },
                [](const WIPArray &) { assert(false && "tried to close a JSON object while in an array"); }
            },
            back
        );
        return true;
    }

    bool end_array() override {
        auto back = std::move(stack.back());
        stack.pop_back();
        std::visit(
            overloaded{
                [](const WIPObject &) { assert(false && "tried to close a JSON array while in an object"); },
                [&](const WIPArray & values) {
                    auto list = state.ctx.mem.newList(values.size());
                    std::ranges::copy(values, list->elems);
                    addValue({NewValueAs::list, list});
                }
            },
            back
        );
        return true;
    }

    bool start_array(size_t len) override {
        auto v = WIPArray{};
        v.reserve(len != std::numeric_limits<size_t>::max() ? len : 128);
        stack.push_back(std::move(v));
        return true;
    }

    bool
    parse_error(std::size_t, const std::string &, const nlohmann::detail::exception & ex) override
    {
        throw JSONParseError("%s", ex.what());
    }
};

Value parseJSON(EvalState & state, const std::string_view & s_)
{
    JSONSax parser(state);
    bool res = JSON::sax_parse(s_, &parser);
    if (!res)
        throw JSONParseError("Invalid JSON Value");
    return parser.result();
}

}
