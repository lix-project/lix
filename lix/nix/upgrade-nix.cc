#include <algorithm>

#include "lix/libcmd/cmd-profiles.hh"
#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libstore/local-fs-store.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/processes.hh"
#include "lix/libstore/profiles.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-settings.hh"
#include "lix/libexpr/attr-path.hh"
#include "lix/libstore/names.hh"
#include "upgrade-nix.hh"

namespace nix {

struct CmdUpgradeNix : MixDryRun, EvalCommand
{
    Path profileDir;
    std::string storePathsUrl = "https://releases.lix.systems/manifest.nix";

    std::optional<Path> overrideStorePath;

    CmdUpgradeNix()
    {
        addFlag({
            .longName = "profile",
            .shortName = 'p',
            .description = "The path to the Nix profile to upgrade.",
            .labels = {"profile-dir"},
            .handler = {&profileDir}
        });

        addFlag({
            .longName = "store-path",
            .description = "A specific store path to upgrade Nix to",
            .labels = {"store-path"},
            .handler = {&overrideStorePath},
        });

        addFlag({
            .longName = "nix-store-paths-url",
            .description = "The URL of the file that contains the store paths of the latest Nix release.",
            .labels = {"url"},
            .handler = {&storePathsUrl}
        });
    }

    /**
     * This command is stable before the others
     */
    std::optional<ExperimentalFeature> experimentalFeature() override
    {
        return std::nullopt;
    }

    std::string description() override
    {
        return "upgrade Nix to the stable version declared in Nixpkgs";
    }

    std::string doc() override
    {
        return
          #include "upgrade-nix.md"
          ;
    }

    Category category() override { return catNixInstallation; }

    void run(ref<Store> store) override
    {
        evalSettings.pureEval.override(true);

        if (profileDir == "") {
            profileDir = getProfileDir(store);
        }

        auto canonProfileDir = canonPath(profileDir, true);

        printInfo("upgrading Nix in profile '%s'", profileDir);

        StorePath storePath = getLatestNix(store);

        auto version = DrvName(storePath.name()).version;

        if (dryRun) {
            logger->pause();
            printTaggedWarning("would upgrade to version %s", version);
            return;
        }

        {
            Activity act(*logger, lvlInfo, actUnknown, fmt("downloading '%s'...", store->printStorePath(storePath)));
            aio().blockOn(store->ensurePath(storePath));
        }

        // {profileDir}/bin/nix-env is a symlink to {profileDir}/bin/nix, which *then*
        // is a symlink to /nix/store/meow-nix/bin/nix.
        // We want /nix/store/meow-nix/bin/nix-env.
        Path const oldNixInStore = realPath(canonProfileDir + "/bin/nix");
        Path const oldNixEnv = dirOf(oldNixInStore) + "/nix-env";

        Path const newNixEnv = store->printStorePath(storePath) + "/bin/nix-env";

        {
            Activity act(*logger, lvlInfo, actUnknown, fmt("verifying that '%s' works...", store->printStorePath(storePath)));
            auto s = aio().blockOn(runProgram(newNixEnv, false, {"--version"}));
            if (s.find("Nix") == std::string::npos)
                throw Error("could not verify that '%s' works", newNixEnv);
        }

        logger->pause();

        auto const fullStorePath = store->printStorePath(storePath);

        if (pathExists(canonProfileDir + "/manifest.nix")) {
            // First remove the existing Nix, then use the *new* Nix by absolute path to
            // install the new one, in case the new and old versions aren't considered
            // to be "the same package" by nix-env's logic (e.g., if their pnames differ).
            Strings removeArgs = {
                "--uninstall",
                oldNixEnv,
                "--profile",
                this->profileDir,
            };
            printTalkative("running %s %s", newNixEnv, concatStringsSep(" ", removeArgs));
            aio().blockOn(runProgram(newNixEnv, false, removeArgs));

            Strings upgradeArgs = {
                "--profile",
                this->profileDir,
                "--install",
                fullStorePath,
                "--no-sandbox",
            };

            printTalkative("running %s %s", newNixEnv, concatStringsSep(" ", upgradeArgs));
            aio().blockOn(runProgram(newNixEnv, false, upgradeArgs));
        } else if (pathExists(canonProfileDir + "/manifest.json")) {
            this->upgradeNewStyleProfile(store, storePath);
        } else {
            // No I will not use std::unreachable.
            // That is undefined behavior if you're wrong.
            // This will have a better error message and coredump.
            assert(
                false && "tried to upgrade unexpected kind of profile, "
                "we can only handle `user-environment` and `profile`"
            );
        }

        printInfo(ANSI_GREEN "upgrade to version %s done" ANSI_NORMAL, version);
    }

    /* Return the profile in which Nix is installed. */
    Path getProfileDir(ref<Store> store)
    {
        Path where;

        for (auto & dir : tokenizeString<Strings>(getEnv("PATH").value_or(""), ":"))
            if (pathExists(dir + "/nix-env")) {
                where = dir;
                break;
            }

        if (where == "")
            throw Error("couldn't figure out how Nix is installed, so I can't upgrade it");

        printInfo("found Nix in '%s'", where);

        if (where.starts_with("/run/current-system"))
            throw Error("Nix on NixOS must be upgraded via 'nixos-rebuild'");

        Path profileDir = dirOf(where);

        // Resolve profile to /nix/var/nix/profiles/<name> link.
        while (canonPath(profileDir).find("/profiles/") == std::string::npos && isLink(profileDir)) {
            profileDir = readLink(profileDir);
        }

        printInfo("found profile '%s'", profileDir);

        Path userEnv = canonPath(profileDir, true);

        if (baseNameOf(where) != "bin") {
            throw Error("directory '%s' does not appear to be part of a Nix profile (no /bin dir?)", where);
        }

        if (!pathExists(userEnv + "/manifest.nix") && !pathExists(userEnv + "/manifest.json")) {
            throw Error(
                "directory '%s' does not have a compatible profile manifest; was it created by Nix?",
                where
            );
        }

        if (!aio().blockOn(store->isValidPath(store->parseStorePath(userEnv)))) {
            throw Error("directory '%s' is not in the Nix store", userEnv);
        }

        return profileDir;
    }

    // TODO: Is there like, any good naming scheme that distinguishes
    // "profiles which nix-env can use" and "profiles which nix profile can use"?
    // You can't just say the manifest version since v2 and v3 are both the latter.
    void upgradeNewStyleProfile(ref<Store> & store, StorePath const & newNix)
    {
        auto fsStore = store.try_cast_shared<LocalFSStore>();
        // TODO(Qyriad): this check is here because we need to cast to a LocalFSStore,
        // to pass to createGeneration(), ...but like, there's no way a remote store
        // would work with the nix-env based upgrade either right?
        if (!fsStore) {
            throw Error("nix upgrade-nix cannot be used on a remote store");
        }

        // nb: nothing actually gets evaluated here.
        // The ProfileManifest constructor only evaluates anything for manifest.nix
        // profiles, which this is not.
        auto evalState = this->getEvaluator();

        ProfileManifest manifest(*evalState->begin(aio()), profileDir);

        // Find which profile element has Nix in it.
        // It should be impossible to *not* have Nix, since we grabbed this
        // store path by looking for things with bin/nix-env in them anyway.
        auto findNix = [&](std::pair<std::string, ProfileElement> const & nameElemPair) -> bool {
            auto const & [name, elem] = nameElemPair;
            for (auto const & ePath : elem.storePaths) {
                auto const nixEnv = store->printStorePath(ePath) + "/bin/nix-env";
                if (pathExists(nixEnv)) {
                    return true;
                }
            }
            // We checked each store path in this element. No nixes here boss!
            return false;
        };
        auto elemWithNix = std::find_if(
            manifest.elements.begin(),
            manifest.elements.end(),
            findNix
        );
        // *Should* be impossible...
        assert(elemWithNix != std::end(manifest.elements));

        auto const nixElemName = elemWithNix->first;

        // Now create a new profile element for the new Nix version...
        ProfileElement elemForNewNix = {
            .storePaths = {newNix},
        };

        // ...and splork it into the manifest where the old profile element was.
        manifest.elements.at(nixElemName) = elemForNewNix;

        // Build the new profile, and switch to it.
        StorePath const newProfile = aio().blockOn(manifest.build(store));
        printTalkative("built new profile '%s'", store->printStorePath(newProfile));
        auto const newGeneration =
            aio().blockOn(createGeneration(*fsStore, this->profileDir, newProfile));
        printTalkative(
            "switching '%s' to newly created generation '%s'",
            this->profileDir,
            newGeneration
        );
        // TODO(Qyriad): use switchGeneration?
        // switchLink's docstring seems to indicate that's preferred, but it's
        // not used for any other `nix profile`-style profile code except for
        // rollback, and it assumes you already have a generation number, which
        // we don't.
        switchLink(profileDir, newGeneration);
    }

    /* Return the store path of the latest stable Nix. */
    StorePath getLatestNix(ref<Store> store)
    {
        if (this->overrideStorePath) {
            printTalkative(
                "skipping Nix version query and using '%s' as latest Nix",
                *this->overrideStorePath
            );
            return store->parseStorePath(*this->overrideStorePath);
        }

        Activity act(*logger, lvlInfo, actUnknown, "querying latest Nix version");

        // FIXME: use nixos.org?
        auto [res, content] = aio().blockOn(getFileTransfer()->download(storePathsUrl));
        auto data = aio().blockOn(content->drain());

        auto evaluator = std::make_unique<Evaluator>(aio(), SearchPath{}, store);
        auto state = evaluator->begin(aio());
        Value v;
        state->eval(evaluator->parseExprFromString(data, CanonPath("/no-such-path")), v);
        Bindings & bindings(*evaluator->mem.allocBindings(0));
        auto v2 = findAlongAttrPath(*state, settings.thisSystem, bindings, v).first;

        return store->parseStorePath(
            state->forceString(v2, noPos, "while evaluating the path tho latest nix version")
        );
    }
};

void registerNixUpgradeNix()
{
    registerCommand<CmdUpgradeNix>("upgrade-nix");
}

}
