#pragma once
/// @file Aliases and wrapper functions that are transparently GC-enabled
/// if Lix is compiled with BoehmGC enabled.

#include <cstddef>
#include <cstring>
#include <list>
#include <map>
#include <new>
#include <string_view>
#include <vector>

#include "lix/libutil/checked-arithmetic.hh"

#if HAVE_BOEHMGC
#define GC_INCLUDE_NEW
#include <gc/gc.h>
#include <gc/gc_allocator.h>
#include <gc/gc_cpp.h>

/// calloc, transparently GC-enabled.
#define LIX_GC_CALLOC(size) GC_MALLOC(size)

/// strdup, transaprently GC-enabled.
#define LIX_GC_STRDUP(str) GC_STRDUP(str)

/// Atomic GC malloc() with GC enabled, or regular malloc() otherwise.
#define LIX_GC_MALLOC_ATOMIC(size) GC_MALLOC_ATOMIC(size)

namespace nix
{

template<typename T>
using TraceableAllocator = traceable_allocator<T>;

}

#else

#include <cstdlib>

/// calloc, transparently GC-enabled.
#define LIX_GC_CALLOC(size) calloc(size, 1)

/// strdup, transparently GC-enabled.
#define LIX_GC_STRDUP(str) strdup(str)

/// Atomic GC malloc() with GC enabled, or regular malloc() otherwise.
/// The returned memory must never contain pointers.
#define LIX_GC_MALLOC_ATOMIC(size) malloc(size)

namespace nix
{

template<typename T>
using TraceableAllocator = std::allocator<T>;

}

#endif

namespace nix
{

/// Alias for std::map which uses BoehmGC's allocator conditional on this Lix
/// build having GC enabled.
template<typename KeyT, typename ValueT>
using GcMap = std::map<
    KeyT,
    ValueT,
    std::less<KeyT>,
    TraceableAllocator<std::pair<KeyT const, ValueT>>
>;

/// Alias for std::vector which uses BoehmGC's allocator conditional on this Lix
/// build having GC enabled.
template<typename ItemT>
using GcVector = std::vector<ItemT, TraceableAllocator<ItemT>>;

/// Alias for std::list which uses BoehmGC's allocator conditional on this Lix
/// build having GC enabled.
template<typename ItemT>
using GcList = std::list<ItemT, TraceableAllocator<ItemT>>;

[[gnu::always_inline]]
inline void * gcAllocBytes(size_t n)
{
    // Note: various places expect the allocated memory to be zero.
    // Hence: calloc().
    void * ptr = LIX_GC_CALLOC(n);
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }

    return ptr;
}

[[gnu::always_inline]]
inline size_t checkedArrayAllocSize(size_t size, size_t howMany)
{
    // NOTE: size_t * size_t, which can definitely overflow.
    // Unsigned integer overflow is definitely a bug, but isn't undefined
    // behavior, so we can just check if we overflowed after the fact.
    // However, people can and do request zero sized allocations, so we need
    // to check that neither of our multiplicands were zero before complaining
    // about it.
    auto checkedSz = checked::Checked<size_t>(howMany) * size;
    if (checkedSz.overflowed()) {
        // Congrats, you done did an overflow.
        throw std::bad_alloc();
    }

    return checkedSz.valueWrapping();
}

/// Typed, safe wrapper around calloc() (transparently GC-enabled). Allocates
/// enough for the requested count of the specified type. Also checks for
/// nullptr (and throws @ref std::bad_alloc), and casts the void pointer to
/// a pointer of the specified type, for type-convenient goodness.
template<typename T>
[[gnu::always_inline]]
inline T * gcAllocType(size_t howMany = 1)
{
    return static_cast<T *>(gcAllocBytes(checkedArrayAllocSize(sizeof(T), howMany)));
}

/// GC-transparently allocates a buffer for a C-string of @ref size *bytes*,
/// meaning you should include the size needed by the NUL terminator in the
/// passed size. Memory allocated with this function must never contain other
/// pointers.
inline char * gcAllocString(size_t size)
{
    char * cstr = static_cast<char *>(LIX_GC_MALLOC_ATOMIC(size));
    if (cstr == nullptr) {
        throw std::bad_alloc();
    }
    return cstr;
}

/// Returns a C-string copied from @ref toCopyFrom, or a single, static empty
/// string if @ref toCopyFrom is also empty.
char const * gcCopyStringIfNeeded(std::string_view toCopyFrom);

}
