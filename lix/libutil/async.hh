#pragma once
///@file

#include "lix/libutil/result.hh"

namespace nix {
namespace detail {
inline void materializeResult(Result<void> r)
{
    r.value();
}

template<typename T>
inline T materializeResult(Result<T> r)
{
    return std::move(r.value());
}
}
}

// force materialization of the value. result::value() returns only an rvalue reference
// and is thus unsuitable for use in e.g. range for without materialization. ideally we
// would wrap the expression in `auto()`, but apple clang fails when given `auto(void)`
#define LIX_TRY_AWAIT(...) (::nix::detail::materializeResult(co_await (__VA_ARGS__)))

#if LIX_UR_COMPILER_UWU
# define TRY_AWAIT LIX_TRY_AWAIT
#endif
