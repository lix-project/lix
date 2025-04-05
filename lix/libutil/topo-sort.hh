#pragma once
///@file

#include "lix/libutil/error.hh"
#include "lix/libutil/result.hh"
#include <kj/async.h>

namespace nix {

template<typename T>
std::vector<T> topoSort(std::set<T> items,
        std::function<std::set<T>(const T &)> getChildren,
        std::function<Error(const T &, const T &)> makeCycleError)
{
    std::vector<T> sorted;
    std::set<T> visited, parents;

    std::function<void(const T & path, const T * parent)> dfsVisit;

    dfsVisit = [&](const T & path, const T * parent) {
        if (parents.count(path)) {
            throw makeCycleError(path, *parent); // NOLINT(lix-foreign-exceptions): type dependent
        }

        if (!visited.insert(path).second) return;
        parents.insert(path);

        std::set<T> references = getChildren(path);

        for (auto & i : references)
            /* Don't traverse into items that don't exist in our starting set. */
            if (i != path && items.count(i))
                dfsVisit(i, &path);

        sorted.push_back(path);
        parents.erase(path);
    };

    for (auto & i : items)
        dfsVisit(i, nullptr);

    std::reverse(sorted.begin(), sorted.end());

    return sorted;
}

template<typename T>
kj::Promise<Result<std::vector<T>>> topoSortAsync(std::set<T> items,
        std::function<kj::Promise<Result<std::set<T>>>(const T &)> getChildren,
        std::function<Error(const T &, const T &)> makeCycleError)
try {
    std::vector<T> sorted;
    std::set<T> visited, parents;

    std::function<kj::Promise<Result<void>>(const T & path, const T * parent)> dfsVisit;

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    dfsVisit = [&](const T & path, const T * parent) -> kj::Promise<Result<void>> {
        try {
            if (parents.count(path)) {
                // NOLINTNEXTLINE(lix-foreign-exceptions): type dependent
                throw makeCycleError(path, *parent);
            }

            if (!visited.insert(path).second) co_return result::success();
            parents.insert(path);

            std::set<T> references = TRY_AWAIT(getChildren(path));

            for (auto & i : references)
                /* Don't traverse into items that don't exist in our starting set. */
                if (i != path && items.count(i))
                    TRY_AWAIT(dfsVisit(i, &path));

            sorted.push_back(path);
            parents.erase(path);
            co_return result::success();
        } catch (...) {
            co_return result::current_exception();
        }
    };

    for (auto & i : items)
        TRY_AWAIT(dfsVisit(i, nullptr));

    std::reverse(sorted.begin(), sorted.end());

    co_return sorted;
} catch (...) {
    co_return result::current_exception();
}

}
