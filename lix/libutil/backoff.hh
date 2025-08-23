#pragma once
/// @file

#include "generator.hh"
#include "lix/libutil/error.hh"
#include <cassert>
#include <chrono>
#include <cmath>
#include <random>

namespace nix {
struct BackoffTiming
{
    std::chrono::milliseconds downloadTimeout;
    std::chrono::milliseconds waitTime;
    unsigned int attempt;
};

/**
 * Generator which computes for each attempt of a retriable action (e.g. a download)
 * the action's timeout and the time to wait in between using exponential backoff.
 *
 * The formula to compute the timeout of the ith attempt is
 *
 *     timeout := min(max_connect_timeout, initial_connect_timeout * 2^i)
 *
 * The increase factor 2^i is capped at 2^48, the initial backoff value is capped at 30s
 * (30000ms) to prevent overflows.
 */
inline Generator<BackoffTiming> backoffTimeouts(
    unsigned int maxAttempts,
    std::chrono::milliseconds maxBackoff,
    std::chrono::milliseconds initialBackoff,
    std::chrono::milliseconds retryTime
)
{
    thread_local std::default_random_engine generator(std::random_device{}());
    std::uniform_real_distribution<> waitDist(-0.5, 0.5);

    auto initialBackoffCapped = std::min(initialBackoff, std::chrono::milliseconds(30000));

    for (unsigned int attempt = 1; attempt < maxAttempts; attempt++) {
        int64_t increaseFactor = std::pow(2, std::min(attempt, 48u));
        auto next = std::min(maxBackoff, initialBackoffCapped * increaseFactor);
        auto wait = std::chrono::round<std::chrono::milliseconds>(
            retryTime * std::pow(2, attempt) + retryTime * waitDist(generator)
        );

        co_yield BackoffTiming{next, wait, attempt};
    }
}
}
