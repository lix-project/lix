#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/local-fs-store.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/json.hh"
#include "build.hh"

namespace nix {

static JSON derivedPathsToJSON(AsyncIoRoot & aio, const DerivedPaths & paths, Store & store)
{
    auto res = JSON::array();
    for (auto & t : paths) {
        std::visit([&](const auto & t) {
            res.push_back(aio.blockOn(t.toJSON(store)));
        }, t.raw());
    }
    return res;
}

static JSON builtPathsWithResultToJSON(
    AsyncIoRoot & aio, const std::vector<BuiltPathWithResult> & buildables, const Store & store
)
{
    auto res = JSON::array();
    for (auto & b : buildables) {
        std::visit([&](const auto & t) {
            auto j = aio.blockOn(t.toJSON(store));
            if (b.result) {
                if (b.result->startTime)
                    j["startTime"] = b.result->startTime;
                if (b.result->stopTime)
                    j["stopTime"] = b.result->stopTime;
                if (b.result->cpuUser)
                    j["cpuUser"] = ((double) b.result->cpuUser->count()) / 1000000;
                if (b.result->cpuSystem)
                    j["cpuSystem"] = ((double) b.result->cpuSystem->count()) / 1000000;
            }
            res.push_back(j);
        }, b.path.raw());
    }
    return res;
}

// TODO deduplicate with other code also setting such out links.
static void createOutLinks(
    AsyncIoRoot & aio,
    const Path & outLink,
    const std::vector<BuiltPathWithResult> & buildables,
    LocalFSStore & store2
)
{
    for (const auto & [_i, buildable] : enumerate(buildables)) {
        auto i = _i;
        std::visit(overloaded {
            [&](const BuiltPath::Opaque & bo) {
                std::string symlink = outLink;
                if (i) symlink += fmt("-%d", i);
                aio.blockOn(store2.addPermRoot(bo.path, absPath(symlink)));
            },
            [&](const BuiltPath::Built & bfd) {
                for (auto & output : bfd.outputs) {
                    std::string symlink = outLink;
                    if (i) symlink += fmt("-%d", i);
                    if (output.first != "out") symlink += fmt("-%s", output.first);
                    aio.blockOn(store2.addPermRoot(output.second, absPath(symlink)));
                }
            },
        }, buildable.path.raw());
    }
}

struct CmdBuild : InstallablesCommand, MixDryRun, MixJSON, MixProfile
{
    Path outLink = "result";
    bool printOutputPaths = false;
    BuildMode buildMode = bmNormal;

    CmdBuild()
    {
        addFlag({
            .longName = "out-link",
            .shortName = 'o',
            .description = "Use *path* as prefix for the symlinks to the build results. It defaults to `result`.",
            .labels = {"path"},
            .handler = {&outLink},
            .completer = completePath
        });

        addFlag({
            .longName = "no-link",
            .description = "Do not create symlinks to the build results.",
            .handler = {&outLink, Path("")},
        });

        addFlag({
            .longName = "print-out-paths",
            .description = "Print the resulting output paths",
            .handler = {&printOutputPaths, true},
        });

        addFlag({
            .longName = "rebuild",
            .description = "Rebuild an already built package and compare the result to the existing store paths.",
            .handler = {&buildMode, bmCheck},
        });
    }

    std::string description() override
    {
        return "build a derivation or fetch a store path";
    }

    std::string doc() override
    {
        return
          #include "build.md"
          ;
    }

    void run(ref<Store> store, Installables && installables) override
    {
        auto state = getEvaluator()->begin(aio());

        if (dryRun) {
            std::vector<DerivedPath> pathsToBuild;

            for (auto & i : installables)
                for (auto & b : i->toDerivedPaths(*state))
                    pathsToBuild.push_back(b.path);

            aio().blockOn(printMissing(store, pathsToBuild, lvlError));

            if (json)
                logger->cout("%s", derivedPathsToJSON(aio(), pathsToBuild, *store).dump());

            return;
        }

        auto buildables = Installable::build(
            *state, getEvalStore(), store,
            Realise::Outputs,
            installables,
            repair ? bmRepair : buildMode);

        if (json) logger->cout("%s", builtPathsWithResultToJSON(aio(), buildables, *store).dump());

        if (outLink != "")
            if (auto store2 = store.try_cast_shared<LocalFSStore>())
                createOutLinks(aio(), outLink, buildables, *store2);

        if (printOutputPaths) {
            logger->pause();
            for (auto & buildable : buildables) {
                std::visit(overloaded {
                    [&](const BuiltPath::Opaque & bo) {
                        logger->cout(store->printStorePath(bo.path));
                    },
                    [&](const BuiltPath::Built & bfd) {
                        for (auto & output : bfd.outputs) {
                            logger->cout(store->printStorePath(output.second));
                        }
                    },
                }, buildable.path.raw());
            }
        }

        BuiltPaths buildables2;
        for (auto & b : buildables)
            buildables2.push_back(b.path);
        updateProfile(buildables2);
    }
};

void registerNixBuild()
{
    registerCommand<CmdBuild>("build");
}

}
