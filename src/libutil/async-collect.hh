#pragma once
/// @file

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

}
