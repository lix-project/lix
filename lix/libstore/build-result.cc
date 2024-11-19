#include "lix/libstore/build-result.hh"

namespace nix {

GENERATE_CMP_EXT(
    ,
    BuildResult,
    me->status,
    me->errorMsg,
    me->timesBuilt,
    me->isNonDeterministic,
    me->builtOutputs,
    me->startTime,
    me->stopTime,
    me->cpuUser,
    me->cpuSystem);

KeyedBuildResult BuildResult::restrictTo(DerivedPath path) const
{
    KeyedBuildResult res{*this, std::move(path)};

    if (auto pbp = std::get_if<DerivedPath::Built>(&res.path)) {
        auto & bp = *pbp;

        /* Because goals are in general shared between derived paths
           that share the same derivation, we need to filter their
           results to get back just the results we care about.
         */

        for (auto it = res.builtOutputs.begin(); it != res.builtOutputs.end();) {
            if (bp.outputs.contains(it->first))
                ++it;
            else
                it = res.builtOutputs.erase(it);
        }
    }

    return res;
}

}
