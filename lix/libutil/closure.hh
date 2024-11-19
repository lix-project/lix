#pragma once
///@file

#include <functional>
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

}
