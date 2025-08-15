#include "lix/libexpr/json-to-value.hh"
#include "lix/libexpr/value.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libutil/json.hh"

#include <limits>

namespace nix {

/*
 * Used for `JSONObjectState`
 */
using ValueMap = GcMap<Symbol, Value *>;

// for more information, refer to
// https://github.com/nlohmann/json/blob/master/include/nlohmann/detail/input/json_sax.hpp
class JSONSax : nlohmann::json_sax<JSON> {
    class JSONState {
    protected:
        std::unique_ptr<JSONState> parent;
        RootValue v;
    public:
        virtual std::unique_ptr<JSONState> resolve(EvalState &)
        {
            assert(false && "tried to close toplevel json parser state");
        }
        explicit JSONState(std::unique_ptr<JSONState> && p) : parent(std::move(p)) {}
        explicit JSONState(Value * v) : v(allocRootValue(v)) {}
        JSONState(JSONState & p) = delete;
        Value & value(EvalState & state)
        {
            if (!v)
                v = allocRootValue(state.ctx.mem.allocValue());
            return **v;
        }
        virtual ~JSONState() {}
        virtual void add() {}
    };

    class JSONObjectState : public JSONState {
        using JSONState::JSONState;
        ValueMap attrs;
        std::unique_ptr<JSONState> resolve(EvalState & state) override
        {
            auto attrs2 = state.ctx.buildBindings(attrs.size());
            for (auto & i : attrs)
                attrs2.insert(i.first, i.second);
            parent->value(state).mkAttrs(attrs2.alreadySorted());
            return std::move(parent);
        }
        void add() override { v = nullptr; }
    public:
        void key(string_t & name, EvalState & state)
        {
            attrs.insert_or_assign(state.ctx.symbols.create(name), &value(state));
        }
    };

    class JSONListState : public JSONState {
        ValueVector values;
        std::unique_ptr<JSONState> resolve(EvalState & state) override
        {
            Value & v = parent->value(state);
            v = state.ctx.mem.newList(values.size());
            for (size_t n = 0; n < values.size(); ++n) {
                v.listElems()[n] = values[n];
            }
            return std::move(parent);
        }
        void add() override {
            values.push_back(*v);
            v = nullptr;
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
    JSONSax(EvalState & state, Value & v) : state(state), rs(new JSONState(&v)) {};

    bool null() override
    {
        rs->value(state).mkNull();
        rs->add();
        return true;
    }

    bool boolean(bool val) override
    {
        rs->value(state).mkBool(val);
        rs->add();
        return true;
    }

    bool number_integer(number_integer_t val) override
    {
        rs->value(state).mkInt(val);
        rs->add();
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
        rs->value(state).mkInt(val);
        rs->add();
        return true;
    }

    bool number_float(number_float_t val, const string_t & s) override
    {
        rs->value(state).mkFloat(val);
        rs->add();
        return true;
    }

    bool string(string_t & val) override
    {
        rs->value(state).mkString(val);
        rs->add();
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
        rs->add();
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

void parseJSON(EvalState & state, const std::string_view & s_, Value & v)
{
    JSONSax parser(state, v);
    bool res = JSON::sax_parse(s_, &parser);
    if (!res)
        throw JSONParseError("Invalid JSON Value");
}

}
