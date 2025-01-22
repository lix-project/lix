#pragma once
/// @file

#include <boost/outcome/std_outcome.hpp>
#include <boost/outcome/std_result.hpp>
#include <boost/outcome/success_failure.hpp>
#include <exception>

namespace nix {

template<typename T, typename E = std::exception_ptr>
using Result = boost::outcome_v2::std_result<T, E>;

template<typename T, typename D, typename E = std::exception_ptr>
using Outcome = boost::outcome_v2::std_outcome<T, D, E>;

namespace result {

using boost::outcome_v2::success;
using boost::outcome_v2::failure;

inline auto current_exception()
{
    return failure(std::current_exception());
}

}

}
