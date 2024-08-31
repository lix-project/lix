#include "goal.hh"
#include "worker.hh"
#include <kj/time.h>

namespace nix {


bool CompareGoalPtrs::operator() (const GoalPtr & a, const GoalPtr & b) const {
    std::string s1 = a->key();
    std::string s2 = b->key();
    return s1 < s2;
}


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
    co_return ContinueImmediately{};
} catch (...) {
    co_return std::current_exception();
}

}
