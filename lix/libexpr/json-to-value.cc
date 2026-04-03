#include "lix/libexpr/json-to-value.hh"
#include "gc-alloc.hh"
#include "lix/libexpr/value.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libutil/json.hh"

#include <limits>

namespace nix {

// for more information, refer to
// https://github.com/nlohmann/json/blob/master/include/nlohmann/detail/input/json_sax.hpp
class JSONSax : nlohmann::json_sax<JSON> {
    class JSONState {
    protected:
        std::unique_ptr<JSONState> parent;
        JSONState() = default;
    public:
        virtual std::unique_ptr<JSONState> resolve(EvalState &) = 0;
        explicit JSONState(std::unique_ptr<JSONState> && p) : parent(std::move(p)) {}
        JSONState(JSONState & p) = delete;
        virtual Value & finalValue()
        {
            assert(false && "tried to read a final value from a non-toplevel json parser state");
        }
        virtual ~JSONState() {}
        virtual void addValue(Value v) = 0;
    };

    class TopLevelJSONState : public JSONState
    {
        RootValue v;
    public:
        std::unique_ptr<JSONState> resolve(EvalState &) override
        {
            assert(false && "tried to close toplevel json parser state");
        }
        TopLevelJSONState() = default;
        TopLevelJSONState(TopLevelJSONState & p) = delete;
        Value & finalValue() override
        {
            assert(v && "tried to read nonexistent final value from json parser");
            return *v;
        }
        void addValue(Value v) override
        {
            assert(!this->v && "duplicate value in toplevel JSON scope");
            this->v = allocRootValue(v);
        }
    };

    class JSONObjectState : public JSONState {
        using JSONState::JSONState;
        GcMap<Symbol, Value> attrs;
        Symbol _key;
        std::unique_ptr<JSONState> resolve(EvalState & state) override
        {
            auto attrs2 = state.ctx.buildBindings(attrs.size());
            for (auto & i : attrs)
                attrs2.insert(i.first, i.second);
            parent->addValue({NewValueAs::attrs, attrs2.alreadySorted()});
            return std::move(parent);
        }
        void addValue(Value v) override
        {
            attrs.insert_or_assign(_key, v);
        }
    public:
        void key(string_t & name, EvalState & state)
        {
            _key = state.ctx.symbols.create(name);
        }
    };

    class JSONListState : public JSONState {
        GcVector<Value> values;
        std::unique_ptr<JSONState> resolve(EvalState & state) override
        {
            auto list = state.ctx.mem.newList(values.size());
            parent->addValue({NewValueAs::list, list});
            for (size_t n = 0; n < values.size(); ++n) {
                list->elems[n] = values[n];
            }
            return std::move(parent);
        }
        void addValue(Value v) override
        {
            values.push_back(v);
        }
    public:
        JSONListState(std::unique_ptr<JSONState> && p, std::size_t reserve) : JSONState(std::move(p))
        {
            values.reserve(reserve);
        }
    };

    EvalState & state;
    std::unique_ptr<JSONState> rs;

public:
    JSONSax(EvalState & state) : state(state), rs(new TopLevelJSONState()) {};

    Value result()
    {
        return rs->finalValue();
    }

    bool null() override
    {
        rs->addValue(Value::VNULL);
        return true;
    }

    bool boolean(bool val) override
    {
        rs->addValue({NewValueAs::boolean, val});
        return true;
    }

    bool number_integer(number_integer_t val) override
    {
        rs->addValue({NewValueAs::integer, val});
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
        rs->addValue({NewValueAs::integer, val});
        return true;
    }

    bool number_float(number_float_t val, const string_t & s) override
    {
        rs->addValue({NewValueAs::floating, val});
        return true;
    }

    bool string(string_t & val) override
    {
        rs->addValue({NewValueAs::string, val});
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
        rs = std::make_unique<JSONObjectState>(std::move(rs));
        return true;
    }

    bool key(string_t & name) override
    {
        dynamic_cast<JSONObjectState*>(rs.get())->key(name, state);
        return true;
    }

    bool end_object() override {
        rs = rs->resolve(state);
        return true;
    }

    bool end_array() override {
        return end_object();
    }

    bool start_array(size_t len) override {
        rs = std::make_unique<JSONListState>(std::move(rs),
            len != std::numeric_limits<size_t>::max() ? len : 128);
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
