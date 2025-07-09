#pragma once
///@file

#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/ref.hh"
#include "lix/libutil/result.hh"
#include <kj/async.h>
#include <set>
#include <vector>

namespace nix {

class Store;

struct Machine {

    const std::string storeUri;
    const std::set<std::string> systemTypes;
    const std::string sshKey;
    const unsigned int maxJobs;
    const float speedFactor;
    const std::set<std::string> supportedFeatures;
    const std::set<std::string> mandatoryFeatures;
    const std::string sshPublicHostKey;
    bool enabled = true;

    /**
     * @return Whether `system` is either `"builtin"` or in
     * `systemTypes`.
     */
    bool systemSupported(const std::string & system) const;

    /**
     * @return Whether `features` is a subset of the union of `supportedFeatures` and
     * `mandatoryFeatures`
     */
    bool allSupported(const std::set<std::string> & features) const;

    /**
     * @return @Whether `mandatoryFeatures` is a subset of `features`
     */
    bool mandatoryMet(const std::set<std::string> & features) const;

    Machine(decltype(storeUri) storeUri,
        decltype(systemTypes) systemTypes,
        decltype(sshKey) sshKey,
        decltype(maxJobs) maxJobs,
        decltype(speedFactor) speedFactor,
        decltype(supportedFeatures) supportedFeatures,
        decltype(mandatoryFeatures) mandatoryFeatures,
        decltype(sshPublicHostKey) sshPublicHostKey);

    kj::Promise<Result<std::pair<ref<Store>, Pipe>>> openStore() const;
};

typedef std::vector<Machine> Machines;

void parseMachines(const std::string & s, Machines & machines);

Machines getMachines();

}
