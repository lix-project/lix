#include "goal.hh"
#include "worker.hh"

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

}
