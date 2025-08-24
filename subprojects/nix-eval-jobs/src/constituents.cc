#include <fnmatch.h>
#include <lix/config.h>
#include <lix/libstore/derivations.hh>
#include <lix/libstore/local-fs-store.hh>
#include <lix/libutil/json.hh>

#include "constituents.hh"
#include "lix/libutil/async.hh"
#include "drv.hh"

#include <sstream>

namespace {
// This is copied from `libutil/topo-sort.hh` in CppNix and slightly modified.
// However, I needed a way to use strings as identifiers to sort, but still be
// able to put AggregateJob objects into this function since I'd rather not have
// to transform back and forth between a list of strings and AggregateJobs in
// resolveNamedConstituents.
auto topoSort(const std::set<AggregateJob> &items)
    -> std::vector<AggregateJob> {
    std::vector<AggregateJob> sorted;
    std::set<std::string> visited;
    std::set<std::string> parents;

    std::map<std::string, AggregateJob> dictIdentToObject;
    for (const auto &it : items) {
        dictIdentToObject.insert({it.name, it});
    }

    std::function<void(const std::string &path, const std::string *parent)>
        dfsVisit;

    dfsVisit = [&](const std::string &path, const std::string *parent) {
        if (parents.contains(path)) {
            dictIdentToObject.erase(path);
            dictIdentToObject.erase(*parent);
            std::set<std::string> remaining;
            for (auto &[k, _] : dictIdentToObject) {
                remaining.insert(k);
            }
            throw DependencyCycle(path, *parent, remaining);
        }

        if (!visited.insert(path).second) {
            return;
        }
        parents.insert(path);

        std::set<std::string> references = dictIdentToObject[path].dependencies;

        for (const auto &i : references) {
            /* Don't traverse into items that don't exist in our starting set.
             */
            if (i != path &&
                dictIdentToObject.find(i) != dictIdentToObject.end()) {
                dfsVisit(i, &path);
            }
        }

        sorted.push_back(dictIdentToObject[path]);
        parents.erase(path);
    };

    for (auto &[i, _] : dictIdentToObject) {
        dfsVisit(i, nullptr);
    }

    return sorted;
}
} // namespace

auto resolveNamedConstituents(const std::map<std::string, nix::JSON> &jobs)
    -> std::variant<std::vector<AggregateJob>, DependencyCycle> {
    std::set<AggregateJob> aggregateJobs;
    for (auto const &[jobName, job] : jobs) {
        auto named = job.find("namedConstituents");
        if (named != job.end() && !named->empty()) {
            std::unordered_map<std::string, std::string> brokenJobs;
            std::set<std::string> results;

            auto isBroken = [&brokenJobs,
                             &jobName](const std::string &childJobName,
                                       const nix::JSON &job) -> bool {
                if (job.find("error") != job.end()) {
                    std::string error = job["error"];
                    printError(
                        "aggregate job '%s' references broken job '%s': %s",
                        jobName, childJobName, error);
                    brokenJobs[childJobName] = error;
                    return true;
                }
                return false;
            };

            for (const std::string childJobName : *named) {
                auto childJobIter = jobs.find(childJobName);
                if (childJobIter == jobs.end()) {
                    printError(
                        "aggregate job '%s' references non-existent job '%s'",
                        jobName, childJobName);
                    brokenJobs[childJobName] = "does not exist";
                } else if (!isBroken(childJobName, childJobIter->second)) {
                    results.insert(childJobName);
                }
            }

            aggregateJobs.insert(AggregateJob(jobName, results, brokenJobs));
        }
    }

    try {
        return topoSort(aggregateJobs);
    } catch (DependencyCycle &e) {
        return e;
    }
}

void rewriteAggregates(std::map<std::string, nix::JSON> &jobs,
                       const std::vector<AggregateJob> &aggregateJobs,
                       nix::ref<nix::Store> &store, nix::Path &gcRootsDir,
                       nix::AsyncIoRoot &aio) {
    for (const auto &aggregateJob : aggregateJobs) {
        auto &job = jobs.find(aggregateJob.name)->second;
        auto drvPath = store->parseStorePath(std::string(job["drvPath"]));
        auto drv = aio.blockOn(store->readDerivation(drvPath));

        if (aggregateJob.brokenJobs.empty()) {
            for (const auto &childJobName : aggregateJob.dependencies) {
                auto childDrvPath = store->parseStorePath(
                    std::string(jobs.find(childJobName)->second["drvPath"]));
                auto childDrv = aio.blockOn(store->readDerivation(childDrvPath));
                job["constituents"].push_back(
                    store->printStorePath(childDrvPath));
                drv.inputDrvs[childDrvPath] = {childDrv.outputs.begin()->first};
            }

            std::string drvName(drvPath.name());
            assert(drvName.ends_with(nix::drvExtension));
            drvName.resize(drvName.size() - nix::drvExtension.size());

            auto hashModulo = aio.blockOn(hashDerivationModulo(*store, drv, true));
            auto h = hashModulo.hashes.find("out");
            if (h == hashModulo.hashes.end()) {
                continue;
            }
            auto outPath = store->makeOutputPath("out", h->second, drvName);
            drv.env["out"] = store->printStorePath(outPath);
            drv.outputs.insert_or_assign(
                "out", nix::DerivationOutput::InputAddressed{.path = outPath});

            auto newDrvPath = aio.blockOn(nix::writeDerivation(*store, drv));
            auto newDrvPathS = store->printStorePath(newDrvPath);

            register_gc_root(gcRootsDir, newDrvPathS, store, aio);

            printError("rewrote aggregate derivation %s -> %s",
                       store->printStorePath(drvPath), newDrvPathS);

            job["drvPath"] = newDrvPathS;
            job["outputs"]["out"] = store->printStorePath(outPath);
        }

        job.erase("namedConstituents");

        if (!aggregateJob.brokenJobs.empty()) {
            std::stringstream ss;
            for (const auto &[jobName, error] : aggregateJob.brokenJobs) {
                ss << jobName << ": " << error << "\n";
            }
            job["error"] = ss.str();
        }

        std::cout << job.dump() << "\n" << std::flush;
    }
}
