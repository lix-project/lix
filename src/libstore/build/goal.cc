#include "goal.hh"
#include "async-collect.hh"
#include "worker.hh"
#include <boost/outcome/try.hpp>
#include <kj/time.h>

namespace nix {


void Goal::trace(std::string_view s)
{
    debug("%1%: %2%", name, s);
}

kj::Promise<void> Goal::waitForAWhile()
{
    trace("wait for a while");
    /* If we are polling goals that are waiting for a lock, then wake
       up after a few seconds at most. */
    return worker.aio.provider->getTimer().afterDelay(settings.pollInterval.get() * kj::SECONDS);
}

kj::Promise<Result<Goal::WorkResult>> Goal::work() noexcept
try {
    BOOST_OUTCOME_CO_TRY(auto result, co_await workImpl());

    trace("done");
    assert(!exitCode.has_value());
    exitCode = result.exitCode;
    ex = result.ex;

    notify->fulfill(result);
    cleanup();

    co_return std::move(result);
} catch (...) {
    co_return result::failure(std::current_exception());
}

kj::Promise<Result<void>>
Goal::waitForGoals(kj::Array<std::pair<GoalPtr, kj::Promise<Result<WorkResult>>>> dependencies) noexcept
try {
    auto left = dependencies.size();
    for (auto & [dep, p] : dependencies) {
        p = p.then([this, dep, &left](auto _result) {
            left--;
            trace(fmt("waitee '%s' done; %d left", dep->name, left));

            if (dep->exitCode != Goal::ecSuccess) ++nrFailed;
            if (dep->exitCode == Goal::ecNoSubstituters) ++nrNoSubstituters;
            if (dep->exitCode == Goal::ecIncompleteClosure) ++nrIncompleteClosure;
            return _result;
        }).eagerlyEvaluate(nullptr);
    }

    auto collectDeps = asyncCollect(std::move(dependencies));

    while (auto item = co_await collectDeps.next()) {
        auto & [dep, _result] = *item;

        waiteeDone(dep);

        if (dep->exitCode == ecFailed && !settings.keepGoing) {
            co_return result::success();
        }
    }

    co_return result::success();
} catch (...) {
    co_return result::failure(std::current_exception());
}

}
