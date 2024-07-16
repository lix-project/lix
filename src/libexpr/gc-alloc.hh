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

#if HAVE_BOEHMGC
#include <functional> // std::less
#include <utility> // std::pair
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

/// Alias for std::map which uses BoehmGC's allocator conditional on this Lix
/// build having GC enabled.
template<typename KeyT, typename ValueT>
using GcMap = std::map<
    KeyT,
    ValueT,
    std::less<KeyT>,
    traceable_allocator<std::pair<KeyT const, ValueT>>
>;

/// Alias for std::vector which uses BoehmGC's allocator conditional on this Lix
/// build having GC enabled.
template<typename ItemT>
using GcVector = std::vector<ItemT, traceable_allocator<ItemT>>;

/// Alias for std::list which uses BoehmGC's allocator conditional on this Lix
/// build having GC enabled.
template<typename ItemT>
using GcList = std::list<ItemT, traceable_allocator<ItemT>>;

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

/// Alias for std::map which uses BoehmGC's allocator conditional on this Lix
/// build having GC enabled.
template<typename KeyT, typename ValueT>
using GcMap = std::map<KeyT, ValueT>;

/// Alias for std::vector which uses BoehmGC's allocator conditional on this Lix
/// build having GC enabled.
template<typename ItemT>
using GcVector = std::vector<ItemT>;

/// Alias for std::list which uses BoehmGC's allocator conditional on this Lix
/// build having GC enabled.
template<typename ItemT>
using GcList = std::list<ItemT>;

}

#endif

namespace nix
{

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
