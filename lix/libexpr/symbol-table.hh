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
    std::string contents;

    Value::String strcb;

    /*
     * A value containing a string that can be immediately passed to the evaluator.
     */
    Value underlyingValue;

public:
    explicit InternedSymbol(std::string_view s)
        : contents(s)
        , strcb{.content = contents.c_str(), .context = nullptr}
        , underlyingValue(NewValueAs::string, &strcb)
    {
    }

    InternedSymbol(InternedSymbol &&) = default;
    InternedSymbol & operator=(InternedSymbol &&) = default;

    KJ_DISALLOW_COPY(InternedSymbol);

    operator SymbolStr() const
    {
        return SymbolStr(contents);
    }

    bool operator==(std::string_view s2) const
    {
        return contents == s2;
    }

    operator std::string_view() const
    {
        return contents;
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

}
