#pragma once
/// @file

#include "lix/libutil/result.hh"
#include <concepts>
#include <kj/async.h>
#include <kj/common.h>
#include <kj/vector.h>
#include <list>
#include <optional>
#include <type_traits>

namespace nix {

template<typename K, typename V>
class AsyncCollect
{
public:
    using Item = std::conditional_t<std::is_void_v<V>, K, std::pair<K, V>>;

private:
    kj::ForkedPromise<void> allPromises;
    std::list<Item> results;
    size_t remaining;

    kj::ForkedPromise<void> signal;
    kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> notify;

    void oneDone(Item item)
    {
        results.emplace_back(std::move(item));
        remaining -= 1;
        KJ_IF_MAYBE (n, notify) {
            (*n)->fulfill();
            notify = nullptr;
        }
    }

    kj::Promise<void> collectorFor(K key, kj::Promise<V> promise)
    {
        if constexpr (std::is_void_v<V>) {
            return promise.then([this, key{std::move(key)}] { oneDone(std::move(key)); });
        } else {
            return promise.then([this, key{std::move(key)}](V v) {
                oneDone(Item{std::move(key), std::move(v)});
            });
        }
    }

    kj::ForkedPromise<void> waitForAll(kj::Array<std::pair<K, kj::Promise<V>>> & promises)
    {
        kj::Vector<kj::Promise<void>> wrappers;
        for (auto & [key, promise] : promises) {
            wrappers.add(collectorFor(std::move(key), std::move(promise)));
        }

        return kj::joinPromisesFailFast(wrappers.releaseAsArray()).fork();
    }

public:
    AsyncCollect(kj::Array<std::pair<K, kj::Promise<V>>> && promises)
        : allPromises(waitForAll(promises))
        , remaining(promises.size())
        , signal{nullptr}
    {
    }

    // oneDone promises capture `this`
    KJ_DISALLOW_COPY_AND_MOVE(AsyncCollect);

    kj::Promise<std::optional<Item>> next()
    {
        if (remaining == 0 && results.empty()) {
            return {std::nullopt};
        }

        if (!results.empty()) {
            auto result = std::move(results.front());
            results.pop_front();
            return {{std::move(result)}};
        }

        if (notify == nullptr) {
            auto pair = kj::newPromiseAndFulfiller<void>();
            notify = std::move(pair.fulfiller);
            signal = pair.promise.fork();
        }

        return signal.addBranch().exclusiveJoin(allPromises.addBranch()).then([this] {
            return next();
        });
    }
};

/**
  * Collect the results of a list of promises, in order of completion.
  * Once any input promise is rejected all promises that have not been
  * resolved or rejected will be cancelled and the exception rethrown.
  */
template<typename K, typename V>
AsyncCollect<K, V> asyncCollect(kj::Array<std::pair<K, kj::Promise<V>>> promises)
{
    return AsyncCollect<K, V>(std::move(promises));
}

/**
 * Run `fn` for every item in the `input` range asynchronously, using
 * the same fail-fast semantics as `asyncCollect`. `asyncSpread` is a
 * shorthand for calling `asyncCollect` with `std::tuple()` values as
 * keys, awaiting that to completion, and propagating all exceptions.
 */
template<typename Input, typename Fn>
kj::Promise<Result<void>> asyncSpread(Input && input, Fn fn)
    requires requires {
        {
            fn(*begin(input))
        } -> std::same_as<kj::Promise<Result<void>>>;
    }
{
    kj::Vector<std::pair<std::tuple<>, kj::Promise<Result<void>>>> children;
    if constexpr (requires { input.size(); }) {
        children.reserve(input.size());
    }

    for (auto & i : input) {
        children.add(std::tuple(), fn(i));
    }

    auto collect = asyncCollect(children.releaseAsArray());
    while (auto r = co_await collect.next()) {
        if (!r->second.has_value()) {
            co_return std::move(r->second);
        }
    }

    co_return result::success();
}

/**
 * Run `promises` concurrently until they've all succeeded, or pass on
 * the error returned by the first failing promise. This is similar to
 * `kj::joinPromisesFailFast()`, but not limited to thrown exceptions.
 */
template<typename... Promises>
kj::Promise<Result<void>> asyncJoin(Promises &&... promises)
    requires(std::same_as<Promises, kj::Promise<Result<void>>> && ...)
{
    auto collect =
        asyncCollect(kj::arr(std::pair{std::tuple{}, std::forward<Promises>(promises)}...));
    while (auto r = co_await collect.next()) {
        if (!r->second.has_value()) {
            co_return std::move(r->second);
        }
    }

    co_return result::success();
}
}
