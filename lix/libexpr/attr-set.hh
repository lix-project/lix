#pragma once
///@file

#include "lix/libexpr/nixexpr.hh"
#include "lix/libexpr/symbol-table.hh"

#include <algorithm>

namespace nix {


class EvalMemory;
struct Value;

/**
 * Map one attribute name to its value.
 */
struct Attr
{
    /* the placement of `name` and `pos` in this struct is important.
       both of them are uint32 wrappers, they are next to each other
       to make sure that Attr has no padding on 64 bit machines. that
       way we keep Attr size at two words with no wasted space. */
    Symbol name;
    PosIdx pos;
    mutable Value value;
    Attr(Symbol name, Value value, PosIdx pos = noPos) : name(name), pos(pos), value(value) {}
    Attr() { };
    bool operator < (const Attr & a) const
    {
        return name < a.name;
    }
};

static_assert(sizeof(Attr) == 2 * sizeof(uint32_t) + sizeof(Value *),
    "performance of the evaluator is highly sensitive to the size of Attr. "
    "avoid introducing any padding into Attr if at all possible, and do not "
    "introduce new fields that need not be present for almost every instance.");

/**
 * Bindings contains all the attributes of an attribute set. It is defined
 * by its size and its capacity, the capacity being the number of Attr
 * elements allocated after this structure, while the size corresponds to
 * the number of elements already inserted in this structure.
 */
class alignas(Value::TAG_ALIGN) Bindings
{
public:
    using Size = uint32_t;
    PosIdx pos;

    static Bindings EMPTY;

private:
    Size size_ = 0;
    Attr attrs[0];

    Bindings() = default;
    Bindings(const Bindings & bindings) = delete;

public:
    Size size() const { return size_; }

    bool empty() const { return !size_; }

    typedef Attr * iterator;

    void push_back(const Attr & attr)
    {
        attrs[size_++] = attr;
    }

    const Attr * get(Symbol name)
    {
        Attr key(name, {});
        iterator i = std::lower_bound(begin(), end(), key);
        if (i != end() && i->name == name) return &*i;
        return nullptr;
    }

    iterator begin() { return &attrs[0]; }
    iterator end() { return &attrs[size_]; }

    Attr & operator[](Size pos)
    {
        return attrs[pos];
    }

    void sort();

    /**
     * Returns the attributes in lexicographically sorted order.
     */
    std::vector<const Attr *> lexicographicOrder(const SymbolTable & symbols) const
    {
        std::vector<const Attr *> res;
        res.reserve(size_);
        for (Size n = 0; n < size_; n++)
            res.emplace_back(&attrs[n]);
        std::sort(res.begin(), res.end(), [&](const Attr * a, const Attr * b) {
            std::string_view sa = symbols[a->name], sb = symbols[b->name];
            return sa < sb;
        });
        return res;
    }

    friend class EvalMemory;
};

/**
 * A wrapper around Bindings that ensures that its always in sorted
 * order at the end. The only way to consume a BindingsBuilder is to
 * call finish(), which sorts the bindings.
 */
class BindingsBuilder
{
public:
    using Size = Bindings::Size;

private:
    Bindings * bindings;
    EvalMemory & mem;
    SymbolTable & symbols;
    Size capacity;

public:
    // needed by std::back_inserter
    using value_type = Attr;

    BindingsBuilder(EvalMemory & mem, SymbolTable & symbols, Bindings * bindings, Size capacity)
        : bindings(bindings)
        , mem(mem)
        , symbols(symbols)
        , capacity(capacity)
    {
    }

    void insert(Symbol name, Value value, PosIdx pos = noPos)
    {
        insert(Attr(name, value, pos));
    }

    void insert(const Attr & attr)
    {
        push_back(attr);
    }

    void push_back(const Attr & attr)
    {
        assert(bindings->size() < capacity);
        bindings->push_back(attr);
    }

    Value & alloc(Symbol name, PosIdx pos = noPos);

    Value & alloc(std::string_view name, PosIdx pos = noPos);

    [[nodiscard("must use created bindings")]]
    Bindings * finish()
    {
        bindings->sort();
        return bindings;
    }

    Bindings * alreadySorted()
    {
        return bindings;
    }
};

}
