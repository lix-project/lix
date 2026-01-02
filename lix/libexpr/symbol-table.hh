#pragma once
///@file

#include <unordered_map>

#include "lix/libutil/types.hh"
#include "lix/libutil/chunked-vector.hh"

#include "lix/libexpr/value.hh"

namespace nix {

/**
 * This class mainly exists to give us an operator<< for ostreams. We could also
 * return plain strings from SymbolTable, but then we'd have to wrap every
 * instance of a symbol that is fmt()ed, which is inconvenient and error-prone.
 */
class SymbolStr
{
    friend class SymbolTable;
    friend class InternedSymbol;

private:
    std::string_view s;

    explicit SymbolStr(std::string_view symbol) : s(symbol) {}

public:
    bool operator == (std::string_view s2) const
    {
        return s == s2;
    }

    operator const std::string_view () const
    {
        return s;
    }

    friend std::ostream & operator <<(std::ostream & os, const SymbolStr & symbol);
};

class InternedSymbol
{
private:
    /*
     * The type that actually stores the string contained inside of the Value.
     */
    std::unique_ptr<Value::Str, Value::Str::Deleter> contents;

    Value::String strcb;

    /*
     * A value containing a string that can be immediately passed to the evaluator.
     */
    Value underlyingValue;

public:
    explicit InternedSymbol(std::string_view s)
        : contents(Value::Str::copy(s))
        , strcb{.content = contents.get(), .context = nullptr}
        , underlyingValue(NewValueAs::string, &strcb)
    {
    }

    InternedSymbol(InternedSymbol &&) = default;
    InternedSymbol & operator=(InternedSymbol &&) = default;

    KJ_DISALLOW_COPY(InternedSymbol);

    operator SymbolStr() const
    {
        return SymbolStr(contents->str());
    }

    bool operator==(std::string_view s2) const
    {
        return contents->str() == s2;
    }

    operator std::string_view() const
    {
        return contents->str();
    }

    Value toValue() const
    {
        return underlyingValue;
    }

    friend std::ostream & operator<<(std::ostream & os, const InternedSymbol & symbol);
};

/**
 * Symbols have the property that they can be compared efficiently
 * (using an equality test), because the symbol table stores only one
 * copy of each string.
 */
class Symbol
{
    friend class SymbolTable;

private:
    uint32_t id;

    explicit Symbol(uint32_t id): id(id) {}

public:
    Symbol() : id(0) {}

    explicit operator bool() const { return id > 0; }

    constexpr auto operator<=>(const Symbol & other) const noexcept = default;
};

/**
 * Symbol table used by the parser and evaluator to represent and look
 * up identifiers and attributes efficiently.
 */
class SymbolTable
{
private:
    /**
     * Map from string view (backed by ChunkedVector) -> offset into the store.
     * ChunkedVector references are never invalidated.
     */
    std::unordered_map<std::string_view, uint32_t> symbols;
    ChunkedVector<InternedSymbol, 8192> store{16};

public:

    /**
     * Converts a string into a symbol.
     */
    Symbol create(std::string_view s)
    {
        // Most symbols are looked up more than once, so we trade off insertion performance
        // for lookup performance.
        // TODO: could probably be done more efficiently with transparent Hash and Equals
        // on the original implementation using unordered_set
        // FIXME: make this thread-safe.
        auto it = symbols.find(s);
        if (it != symbols.end()) {
            return Symbol(it->second + 1);
        }

        const auto & [rawSym, idx] = store.add(s);
        symbols.emplace(rawSym, idx);
        return Symbol(idx + 1);
    }

    const InternedSymbol & operator[](Symbol s) const
    {
        if (s.id == 0 || s.id > store.size())
            abort();
        return store[s.id - 1];
    }

    size_t size() const
    {
        return store.size();
    }

    size_t totalSize() const;

    template<typename T>
    void dump(T callback) const
    {
        store.forEach(callback);
    }
};

/**
 * NixSymbolTable extends the generic SymbolTable with pre-filled symbol constants for
 * all well-known symbol names used in the Nix language.
 * Besides the convenience aspect, this also improves performances as it does not require a table lookup for
 * these very commonly used symbols.
 */
class NixSymbolTable : public SymbolTable
{
public:
    /* So it turns out that in C++, not only all identifiers starting with `__` are reserved, but also
     * all identifiers *containing* `__`. However, given our prefix and the usage of triple underscores
     * we are exceedingly unlikely to ever accidentally hit an actual identifier collision, and given that
     * we are using custom types such a collision would almost certainly blow up with a loud bang and not
     * cause any silent failure. We can always easily change to a different (likely worse) naming convention
     * for our identifiers should we ever run into such a case.
     */
    // NOLINTBEGIN(bugprone-reserved-identifier)
    /* Magic primops */
    const Symbol sym___sub = create("__sub");
    const Symbol sym___lessThan = create("__lessThan");
    const Symbol sym___mul = create("__mul");
    const Symbol sym___div = create("__div");
    const Symbol sym___findFile = create("__findFile");
    const Symbol sym___nixPath = create("__nixPath");
    /* Parser keywords */
    const Symbol sym_or = create("or");
    const Symbol sym_body = create("body"); /* ancient let */
    /* __pos */
    const Symbol sym_file = create("file");
    const Symbol sym_line = create("line");
    const Symbol sym_column = create("column");
    /* Evaluator magic attrs */
    const Symbol sym___overrides = create("__overrides");
    const Symbol sym___functor = create("__functor");
    const Symbol sym___toString = create("__toString");
    /* Symbols for primops/builtins */
    const Symbol sym_path = create("path"); // builtins.getContext
    const Symbol sym_prefix = create("prefix"); // builtins.findFile
    const Symbol sym_startSet = create("startSet"); // builtins.genericClosure
    const Symbol sym_operator = create("operator"); // builtins.genericClosure
    const Symbol sym_key = create("key"); // builtins.genericClosure
    const Symbol sym_right = create("right"); // builtins.partition
    const Symbol sym_wrong = create("wrong"); // builtins.partition
    /* Derivation magic attrs */
    const Symbol sym___ignoreNulls = create("__ignoreNulls");
    const Symbol sym___structuredAttrs = create("__structuredAttrs");
    const Symbol sym___contentAddressed = create("__contentAddressed");
    const Symbol sym___impure = create("__impure");
    /* Derivation */
    const Symbol sym_outPath = create("outPath");
    const Symbol sym_drvPath = create("drvPath");
    const Symbol sym_meta = create("meta");
    const Symbol sym_outputs = create("outputs");
    const Symbol sym_outputName = create("outputName");
    const Symbol sym_allowedReferences = create("allowedReferences");
    const Symbol sym_allowedRequisites = create("allowedRequisites");
    const Symbol sym_disallowedReferences = create("disallowedReferences");
    const Symbol sym_disallowedRequisites = create("disallowedRequisites");
    const Symbol sym_maxSize = create("maxSize");
    const Symbol sym_maxClosureSize = create("maxClosureSize");
    const Symbol sym_builder = create("builder");
    const Symbol sym_args = create("args");
    const Symbol sym_outputHash = create("outputHash");
    const Symbol sym_outputHashAlgo = create("outputHashAlgo");
    const Symbol sym_outputHashMode = create("outputHashMode");
    const Symbol sym_recurseForDerivations = create("recurseForDerivations");
    const Symbol sym_outputSpecified = create("outputSpecified");
    /* Flakes */
    const Symbol sym_description = create("description");
    const Symbol sym_self = create("self");
    /* Various uses */
    const Symbol sym_name = create("name"); // Derivation name, name value pair, ...
    const Symbol sym_value = create("value"); // builtins.tryEval, name value pair, ...
    const Symbol sym_system = create("system"); // Derivation, user env, ...
    const Symbol sym_type = create("type");
    // NOLINTEND(bugprone-reserved-identifier)
};
}
