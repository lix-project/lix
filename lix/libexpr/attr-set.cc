#include "lix/libexpr/attr-set.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/gc-alloc.hh"

#include <algorithm>


namespace nix {

Bindings Bindings::EMPTY{0};


/* Allocate a new array of attributes for an attribute set with a specific
   capacity. The space is implicitly reserved after the Bindings
   structure. */
Bindings * EvalMemory::allocBindings(size_t capacity)
{
    if (capacity == 0)
        return &Bindings::EMPTY;
    if (capacity > std::numeric_limits<Bindings::Size>::max())
        throw Error("attribute set of size %d is too big", capacity);
    stats.nrAttrsets++;
    stats.nrAttrsInAttrsets += capacity;
    return new (gcAllocBytes(sizeof(Bindings) + sizeof(Attr) * capacity)) Bindings((Bindings::Size) capacity);
}


Value & BindingsBuilder::alloc(Symbol name, PosIdx pos)
{
    auto value = mem.allocValue();
    bindings->push_back(Attr(name, value, pos));
    return *value;
}


Value & BindingsBuilder::alloc(std::string_view name, PosIdx pos)
{
    return alloc(symbols.create(name), pos);
}


void Bindings::sort()
{
    if (size_) std::sort(begin(), end());
}


Value & Value::mkAttrs(BindingsBuilder & bindings)
{
    mkAttrs(bindings.finish());
    return *this;
}


}
