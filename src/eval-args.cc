#include <stdio.h>
#include <stdlib.h>
#include <nix/args.hh>
#include <nix/file-system.hh>
#include <nix/flake/flake.hh>
#include <nix/flake/lockfile.hh>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "eval-args.hh"

MyArgs::MyArgs() : MixCommonArgs("nix-eval-jobs") {
    addFlag({
        .longName = "help",
        .description = "show usage information",
        .handler = {[&]() {
            printf("USAGE: nix-eval-jobs [options] expr\n\n");
            for (const auto &[name, flag] : longFlags) {
                if (hiddenCategories.count(flag->category)) {
                    continue;
                }
                printf("  --%-20s %s\n", name.c_str(),
                       flag->description.c_str());
            }
            ::exit(0);
        }},
    });

    addFlag({.longName = "impure",
             .description = "allow impure expressions",
             .handler = {&impure, true}});

    addFlag({.longName = "force-recurse",
             .description = "force recursion (don't respect recurseIntoAttrs)",
             .handler = {&forceRecurse, true}});

    addFlag({.longName = "gc-roots-dir",
             .description = "garbage collector roots directory",
             .labels = {"path"},
             .handler = {&gcRootsDir}});

    addFlag(
        {.longName = "workers",
         .description = "number of evaluate workers",
         .labels = {"workers"},
         .handler = {[=, this](std::string s) { nrWorkers = std::stoi(s); }}});

    addFlag({.longName = "max-memory-size",
             .description = "maximum evaluation memory size in megabyte "
                            "(4GiB per worker by default)",
             .labels = {"size"},
             .handler = {
                 [=, this](std::string s) { maxMemorySize = std::stoi(s); }}});

    addFlag({.longName = "flake",
             .description = "build a flake",
             .handler = {&flake, true}});

    addFlag({.longName = "meta",
             .description = "include derivation meta field in output",
             .handler = {&meta, true}});

    addFlag({.longName = "check-cache-status",
             .description =
                 "Check if the derivations are present locally or in "
                 "any configured substituters (i.e. binary cache). The "
                 "information "
                 "will be exposed in the `isCached` field of the JSON output.",
             .handler = {&checkCacheStatus, true}});

    addFlag(
        {.longName = "show-trace",
         .description = "print out a stack trace in case of evaluation errors",
         .handler = {&showTrace, true}});

    addFlag({.longName = "expr",
             .shortName = 'E',
             .description = "treat the argument as a Nix expression",
             .handler = {&fromArgs, true}});

    // usually in MixFlakeOptions
    addFlag({
        .longName = "override-input",
        .description =
            "Override a specific flake input (e.g. `dwarffs/nixpkgs`).",
        .category = category,
        .labels = {"input-path", "flake-url"},
        .handler = {[&](std::string inputPath, std::string flakeRef) {
            // overriden inputs are unlocked
            lockFlags.allowUnlocked = true;
            lockFlags.inputOverrides.insert_or_assign(
                nix::flake::parseInputPath(inputPath),
                nix::parseFlakeRef(flakeRef, nix::absPath("."), true));
        }},
    });

    expectArg("expr", &releaseExpr);
}

void MyArgs::parseArgs(char **argv, int argc) {
    parseCmdline(nix::argvToStrings(argc, argv), 0);
}
