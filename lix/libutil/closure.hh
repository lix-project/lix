#pragma once
///@file

#include "lix/libutil/async.hh"
#include "lix/libutil/result.hh"
#include <functional>
#include <kj/async.h>
#include <set>

namespace nix {

template<typename T>
std::set<T> computeClosure(
    std::set<T> startElts,
    std::function<std::set<T>(const T &)> getEdges
)
{
    std::set<T> res, queue = std::move(startElts);

    while (!queue.empty()) {
        std::set<T> next;

        for (auto & e : queue) {
            if (res.insert(e).second) {
                next.merge(getEdges(e));
            }
        }

        queue = std::move(next);
    }

    return res;
}

template<typename T>
kj::Promise<Result<std::set<T>>> computeClosureAsync(
    std::set<T> startElts,
    std::function<kj::Promise<Result<std::set<T>>>(const T &)> getEdges
)
try {
    std::set<T> res, queue = std::move(startElts);

    while (!queue.empty()) {
        std::set<T> next;

        for (auto & e : queue) {
            if (res.insert(e).second) {
                next.merge(LIX_TRY_AWAIT(getEdges(e)));
            }
        }

        queue = std::move(next);
    }

    co_return res;
} catch (...) {
    co_return result::current_exception();
}

}
