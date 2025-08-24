#include "lix/libcmd/command.hh"
#include "lix/libcmd/cmd-profiles.hh"
#include "lix/libcmd/installable-flake.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libutil/archive.hh"
#include "lix/libstore/builtins/buildenv.hh"
#include "lix/libexpr/flake/flakeref.hh"
#include "lix/libutil/regex.hh"
#include "user-env.hh"
#include "lix/libstore/profiles.hh"
#include "lix/libstore/names.hh"
#include "profile.hh"

#include <regex>
#include <iomanip>

namespace nix {


static std::map<Installable *, std::pair<BuiltPaths, ref<ExtraPathInfo>>>
builtPathsPerInstallable(
    const std::vector<std::pair<ref<Installable>, BuiltPathWithResult>> & builtPaths)
{
    std::map<Installable *, std::pair<BuiltPaths, ref<ExtraPathInfo>>> res;
    for (auto & [installable, builtPath] : builtPaths) {
        auto & r = res.insert({
            &*installable,
            {
                {},
                make_ref<ExtraPathInfo>(),
            }
        }).first->second;
        /* Note that there could be conflicting info
           (e.g. meta.priority fields) if the installable returned
           multiple derivations. So pick one arbitrarily. FIXME:
           print a warning? */
        r.first.push_back(builtPath.path);
        r.second = builtPath.info;
    }
    return res;
}

struct CmdProfileInstall : InstallablesCommand, MixDefaultProfile
{
    std::optional<int64_t> priority;

    CmdProfileInstall() {
        addFlag({
            .longName = "priority",
            .description = "The priority of the package to install.",
            .labels = {"priority"},
            .handler = {&priority},
        });
    };

    std::string description() override
    {
        return "install a package into a profile";
    }

    std::string doc() override
    {
        return
          #include "profile-install.md"
          ;
    }

    void run(ref<Store> store, Installables && installables) override
    {
        auto state = getEvaluator()->begin(aio());
        ProfileManifest manifest(*state, *profile);

        auto builtPaths = builtPathsPerInstallable(
            Installable::build2(
                *state, getEvalStore(), store, Realise::Outputs, installables, bmNormal));

        for (auto & installable : installables) {
            ProfileElement element;

            auto iter = builtPaths.find(&*installable);
            if (iter == builtPaths.end()) continue;
            auto & [res, info] = iter->second;

            if (auto * info2 = dynamic_cast<ExtraPathInfoFlake *>(&*info)) {
                element.source = ProfileElementSource {
                    .originalRef = info2->flake.originalRef,
                    .lockedRef = info2->flake.lockedRef,
                    .attrPath = info2->value.attrPath,
                    .outputs = info2->value.extendedOutputsSpec,
                };
            }

            // If --priority was specified we want to override the
            // priority of the installable.
            element.priority =
                priority
                ? *priority
                : ({
                    auto * info2 = dynamic_cast<ExtraPathInfoValue *>(&*info);
                    info2
                        ? info2->value.priority.value_or(DEFAULT_PRIORITY)
                        : DEFAULT_PRIORITY;
                });

            element.updateStorePaths(getEvalStore(), store, res);

            manifest.addElement(std::move(element));
        }

        try {
            updateProfile(aio().blockOn(manifest.build(store)));
        } catch (BuildEnvFileConflictError & conflictError) {
            // FIXME use C++20 std::ranges once macOS has it
            //       See https://github.com/NixOS/nix/compare/3efa476c5439f8f6c1968a6ba20a31d1239c2f04..1fe5d172ece51a619e879c4b86f603d9495cc102
            auto findRefByFilePath = [&]<typename Iterator>(Iterator begin, Iterator end) {
                for (auto it = begin; it != end; it++) {
                    auto & profileElement = it->second;
                    for (auto & storePath : profileElement.storePaths) {
                        if (conflictError.fileA.starts_with(store->printStorePath(storePath))) {
                            return std::pair(conflictError.fileA, profileElement.toInstallables(*store));
                        }
                        if (conflictError.fileB.starts_with(store->printStorePath(storePath))) {
                            return std::pair(conflictError.fileB, profileElement.toInstallables(*store));
                        }
                    }
                }
                throw conflictError;
            };
            // There are 2 conflicting files. We need to find out which one is from the already installed package and
            // which one is the package that is the new package that is being installed.
            // The first matching package is the one that was already installed (original).
            auto [originalConflictingFilePath, originalConflictingRefs] = findRefByFilePath(manifest.elements.begin(), manifest.elements.end());
            // The last matching package is the one that was going to be installed (new).
            auto [newConflictingFilePath, newConflictingRefs] = findRefByFilePath(manifest.elements.rbegin(), manifest.elements.rend());

            throw Error(
                "An existing package already provides the following file:\n"
                "\n"
                "  %1%\n"
                "\n"
                "This is the conflicting file from the new package:\n"
                "\n"
                "  %2%\n"
                "\n"
                "To remove the existing package:\n"
                "\n"
                "  nix profile remove %3%\n"
                "\n"
                "The new package can also be installed next to the existing one by assigning a different priority.\n"
                "The conflicting packages have a priority of %5%.\n"
                "To prioritise the new package:\n"
                "\n"
                "  nix profile install %4% --priority %6%\n"
                "\n"
                "To prioritise the existing package:\n"
                "\n"
                "  nix profile install %4% --priority %7%\n",
                originalConflictingFilePath,
                newConflictingFilePath,
                concatStringsSep(" ", originalConflictingRefs),
                concatStringsSep(" ", newConflictingRefs),
                conflictError.priority,
                conflictError.priority - 1,
                conflictError.priority + 1
            );
        }
    }
};

class MixProfileElementMatchers : virtual Args
{
    std::vector<std::string> _matchers;

public:

    MixProfileElementMatchers()
    {
        expectArgs("elements", &_matchers);
    }

    struct RegexPattern {
        std::string pattern;
        std::regex  reg;
    };
    using Matcher = std::variant<Path, RegexPattern>;

    std::vector<Matcher> getMatchers(ref<Store> store)
    {
        std::vector<Matcher> res;

        for (auto & s : _matchers) {
            if (auto n = string2Int<size_t>(s)) {
                throw Error("'nix profile' no longer supports indices ('%d')", *n);
            } else if (store->isStorePath(s)) {
                res.push_back(s);
            } else {
                res.push_back(RegexPattern{s, regex::parse(s, std::regex::extended | std::regex::icase)});
            }
        }

        return res;
    }

    bool matches(
        Store const & store,
        // regex_match doesn't take a string_view lol
        std::string const & name,
        ProfileElement const & element,
        std::vector<Matcher> const & matchers
    )
    {
        for (auto & matcher : matchers) {
            if (auto path = std::get_if<Path>(&matcher)) {
                if (element.storePaths.count(store.parseStorePath(*path))) return true;
            } else if (auto regex = std::get_if<RegexPattern>(&matcher)) {
                if (std::regex_match(name, regex->reg)) {
                    return true;
                }
            }
        }

        return false;
    }
};

struct CmdProfileRemove : virtual EvalCommand, MixDefaultProfile, MixProfileElementMatchers
{
    std::string description() override
    {
        return "remove packages from a profile";
    }

    std::string doc() override
    {
        return
          #include "profile-remove.md"
          ;
    }

    void run(ref<Store> store) override
    {
        ProfileManifest oldManifest(*getEvaluator()->begin(aio()), *profile);

        auto matchers = getMatchers(store);

        ProfileManifest newManifest;

        for (auto & [name, element] : oldManifest.elements) {
            if (!matches(*store, name, element, matchers)) {
                newManifest.elements.insert_or_assign(name, std::move(element));
            } else {
                notice("removing '%s'", element.identifier());
            }
        }

        auto removedCount = oldManifest.elements.size() - newManifest.elements.size();
        printInfo("removed %d packages, kept %d packages",
            removedCount,
            newManifest.elements.size());

        if (removedCount == 0) {
            for (auto matcher: matchers) {
                if (const Path * path = std::get_if<Path>(&matcher)) {
                    printTaggedWarning("'%s' does not match any paths", *path);
                } else if (const RegexPattern * regex = std::get_if<RegexPattern>(&matcher)){
                    printTaggedWarning("'%s' does not match any packages", regex->pattern);
                }
            }
            printTaggedWarning("Use 'nix profile list' to see the current profile.");
        }
        updateProfile(aio().blockOn(newManifest.build(store)));
    }
};

struct CmdProfileUpgrade : virtual SourceExprCommand, MixDefaultProfile, MixProfileElementMatchers
{
    std::string description() override
    {
        return "upgrade packages using their most recent flake";
    }

    std::string doc() override
    {
        return
          #include "profile-upgrade.md"
          ;
    }

    void run(ref<Store> store) override
    {
        auto state = getEvaluator()->begin(aio());
        ProfileManifest manifest(*state, *profile);

        auto matchers = getMatchers(store);

        Installables installables;
        std::vector<ProfileElement *> elems;

        auto matchedCount = 0;
        auto upgradedCount = 0;

        for (auto & [name, element] : manifest.elements) {
            if (!matches(*store, name, element, matchers)) {
                continue;
            }

            matchedCount += 1;

            if (!element.source) {
                printTaggedWarning(
                    "Found package '%s', but it was not installed from a flake, so it can't be "
                    "checked for upgrades",
                    element.identifier()
                );
                continue;
            }

            if (element.source->originalRef.input.isLocked()) {
                printTaggedWarning(
                    "Found package '%s', but it was installed from a locked flake reference so it "
                    "can't be upgraded",
                    element.identifier()
                );
                continue;
            }

            upgradedCount++;

             Activity act(
                *logger,
                lvlChatty,
                actUnknown,
                fmt("checking '%s' for updates", element.source->attrPath),
                Logger::Fields{element.source->attrPath}
            );

            auto installable = make_ref<InstallableFlake>(
                this,
                getEvaluator(),
                FlakeRef(element.source->originalRef),
                "",
                element.source->outputs,
                Strings{element.source->attrPath},
                Strings{},
                lockFlags
            );

            auto derivedPaths = installable->toDerivedPaths(*state);
            if (derivedPaths.empty()) {
                continue;
            }

            auto * infop = dynamic_cast<ExtraPathInfoFlake *>(&*derivedPaths[0].info);
            // `InstallableFlake` should use `ExtraPathInfoFlake`.
            assert(infop);
            auto & info = *infop;

            if (element.source->lockedRef == info.flake.lockedRef) {
                continue;
            }

            printInfo(
                "upgrading '%s' from flake '%s' to '%s'",
                element.source->attrPath,
                element.source->lockedRef,
                info.flake.lockedRef
            );

            element.source = ProfileElementSource {
                .originalRef = installable->flakeRef,
                .lockedRef = info.flake.lockedRef,
                .attrPath = info.value.attrPath,
                .outputs = installable->extendedOutputsSpec,
            };

            installables.push_back(installable);
            elems.push_back(&element);

        }

        if (upgradedCount == 0) {
            if (matchedCount == 0) {
                for (auto & matcher : matchers) {
                    if (const Path * path = std::get_if<Path>(&matcher)){
                        printTaggedWarning("'%s' does not match any paths", *path);
                    } else if (const RegexPattern * regex = std::get_if<RegexPattern>(&matcher)) {
                        printTaggedWarning("'%s' does not match any packages", regex->pattern);
                    }
                }
            } else {
                printTaggedWarning("Found some packages but none of them could be upgraded");
            }
            printTaggedWarning("Use 'nix profile list' to see the current profile.");
        }

        auto builtPaths = builtPathsPerInstallable(
            Installable::build2(
                *state, getEvalStore(), store, Realise::Outputs, installables, bmNormal));

        for (size_t i = 0; i < installables.size(); ++i) {
            auto & installable = installables.at(i);
            auto & element = *elems.at(i);
            element.updateStorePaths(
                getEvalStore(),
                store,
                builtPaths.find(&*installable)->second.first);
        }

        updateProfile(aio().blockOn(manifest.build(store)));
    }
};

struct CmdProfileList : virtual EvalCommand, virtual StoreCommand, MixDefaultProfile, MixJSON
{
    std::string description() override
    {
        return "list installed packages";
    }

    std::string doc() override
    {
        return
          #include "profile-list.md"
          ;
    }

    void run(ref<Store> store) override
    {
        ProfileManifest manifest(*getEvaluator()->begin(aio()), *profile);

        if (json) {
            std::cout << manifest.toJSON(*store).dump() << "\n";
        } else {
            for (auto const & [i, nameElemPair] : enumerate(manifest.elements)) {
                auto & [name, element] = nameElemPair;
                if (i) {
                    logger->cout("");
                }
                logger->cout(
                    "Name:               " ANSI_BOLD "%s" ANSI_NORMAL "%s",
                    name,
                    element.active ? "" : " " ANSI_RED "(inactive)" ANSI_NORMAL
                );
                if (element.source) {
                    logger->cout("Flake attribute:    %s%s", element.source->attrPath, element.source->outputs.to_string());
                    logger->cout("Original flake URL: %s", element.source->originalRef.to_string());
                    logger->cout("Locked flake URL:   %s", element.source->lockedRef.to_string());
                }
                logger->cout("Store paths:        %s", concatStringsSep(" ", store->printStorePathSet(element.storePaths)));
            }
        }
    }
};

struct CmdProfileDiffClosures : virtual StoreCommand, MixDefaultProfile
{
    std::string description() override
    {
        return "show the closure difference between each version of a profile";
    }

    std::string doc() override
    {
        return
          #include "profile-diff-closures.md"
          ;
    }

    void run(ref<Store> store) override
    {
        auto [gens, curGen] = findGenerations(*profile);

        std::optional<Generation> prevGen;
        bool first = true;

        for (auto & gen : gens) {
            if (prevGen) {
                if (!first) logger->cout("");
                first = false;
                logger->cout("Version %d -> %d:", prevGen->number, gen.number);
                aio().blockOn(printClosureDiff(store,
                    store->followLinksToStorePath(prevGen->path),
                    store->followLinksToStorePath(gen.path),
                    false,
                    "  "));
            }

            prevGen = gen;
        }
    }
};

struct CmdProfileHistory : virtual StoreCommand, EvalCommand, MixDefaultProfile
{
    std::string description() override
    {
        return "show all versions of a profile";
    }

    std::string doc() override
    {
        return
          #include "profile-history.md"
          ;
    }

    void run(ref<Store> store) override
    {
        auto [gens, curGen] = findGenerations(*profile);

        std::optional<std::pair<Generation, ProfileManifest>> prevGen;
        bool first = true;

        for (auto & gen : gens) {
            ProfileManifest manifest(*getEvaluator()->begin(aio()), gen.path);

            if (!first) logger->cout("");
            first = false;

            logger->cout("Version %s%d" ANSI_NORMAL " (%s)%s:",
                gen.number == curGen ? ANSI_GREEN : ANSI_BOLD,
                gen.number,
                std::put_time(std::gmtime(&gen.creationTime), "%Y-%m-%d"),
                prevGen ? fmt(" <- %d", prevGen->first.number) : "");

            ProfileManifest::printDiff(
                prevGen ? prevGen->second : ProfileManifest(),
                manifest,
                "  ");

            prevGen = {gen, std::move(manifest)};
        }
    }
};

struct CmdProfileRollback : virtual StoreCommand, MixDefaultProfile, MixDryRun
{
    std::optional<GenerationNumber> version;

    CmdProfileRollback()
    {
        addFlag({
            .longName = "to",
            .description = "The profile version to roll back to.",
            .labels = {"version"},
            .handler = {&version},
        });
    }

    std::string description() override
    {
        return "roll back to the previous version or a specified version of a profile";
    }

    std::string doc() override
    {
        return
          #include "profile-rollback.md"
          ;
    }

    void run(ref<Store> store) override
    {
        switchGeneration(*profile, version, dryRun);
    }
};

struct CmdProfileWipeHistory : virtual StoreCommand, MixDefaultProfile, MixDryRun
{
    std::optional<std::string> minAge;

    CmdProfileWipeHistory()
    {
        addFlag({
            .longName = "older-than",
            .description =
                "Delete versions older than the specified age. *age* "
                "must be in the format *N*`d`, where *N* denotes a number "
                "of days.",
            .labels = {"age"},
            .handler = {&minAge},
        });
    }

    std::string description() override
    {
        return "delete non-current versions of a profile";
    }

    std::string doc() override
    {
        return
          #include "profile-wipe-history.md"
          ;
    }

    void run(ref<Store> store) override
    {
        if (minAge) {
            auto t = parseOlderThanTimeSpec(*minAge);
            deleteGenerationsOlderThan(*profile, t, dryRun);
        } else
            deleteOldGenerations(*profile, dryRun);
    }
};

struct CmdProfile : MultiCommand
{
    CmdProfile()
        : MultiCommand({
              {"install", [](auto & aio) { return make_ref<MixAio<CmdProfileInstall>>(aio); }},
              {"remove", [](auto & aio) { return make_ref<MixAio<CmdProfileRemove>>(aio); }},
              {"upgrade", [](auto & aio) { return make_ref<MixAio<CmdProfileUpgrade>>(aio); }},
              {"list", [](auto & aio) { return make_ref<MixAio<CmdProfileList>>(aio); }},
              {"diff-closures", [](auto & aio) { return make_ref<MixAio<CmdProfileDiffClosures>>(aio); }},
              {"history", [](auto & aio) { return make_ref<MixAio<CmdProfileHistory>>(aio); }},
              {"rollback", [](auto & aio) { return make_ref<MixAio<CmdProfileRollback>>(aio); }},
              {"wipe-history", [](auto & aio) { return make_ref<MixAio<CmdProfileWipeHistory>>(aio); }},
          })
    { }

    std::string description() override
    {
        return "manage Nix profiles";
    }

    std::string doc() override
    {
        return
          #include "profile.md"
          ;
    }

    void run() override
    {
        if (!command)
            throw UsageError("'nix profile' requires a sub-command.");
        command->second->run();
    }
};

void registerNixProfile()
{
    registerCommand<CmdProfile>("profile");
}

}
