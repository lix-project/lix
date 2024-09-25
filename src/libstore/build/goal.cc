#include "goal.hh"
#include "async-collect.hh"
#include "worker.hh"
#include <kj/time.h>

namespace nix {


void Goal::trace(std::string_view s)
{
    debug("%1%: %2%", name, s);
}

kj::Promise<Result<Goal::WorkResult>> Goal::waitForAWhile()
try {
    trace("wait for a while");
    /* If we are polling goals that are waiting for a lock, then wake
       up after a few seconds at most. */
    co_await worker.aio.provider->getTimer().afterDelay(settings.pollInterval.get() * kj::SECONDS);
    co_return StillAlive{};
} catch (...) {
    co_return std::current_exception();
}

kj::Promise<Result<Goal::WorkResult>>
Goal::waitForGoals(kj::Array<std::pair<GoalPtr, kj::Promise<void>>> dependencies) noexcept
try {
    auto left = dependencies.size();
    for (auto & [dep, p] : dependencies) {
        p = p.then([this, dep, &left] {
            left--;
            trace(fmt("waitee '%s' done; %d left", dep->name, left));

            if (dep->exitCode != Goal::ecSuccess) ++nrFailed;
            if (dep->exitCode == Goal::ecNoSubstituters) ++nrNoSubstituters;
            if (dep->exitCode == Goal::ecIncompleteClosure) ++nrIncompleteClosure;
        }).eagerlyEvaluate(nullptr);
    }

    auto collectDeps = asyncCollect(std::move(dependencies));

    while (auto item = co_await collectDeps.next()) {
        auto & dep = *item;

        waiteeDone(dep);

        if (dep->exitCode == ecFailed && !settings.keepGoing) {
            co_return result::success(StillAlive{});
        }
    }

    co_return result::success(StillAlive{});
} catch (...) {
    co_return result::failure(std::current_exception());
}

}
