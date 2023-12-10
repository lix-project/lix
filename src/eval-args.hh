#pragma once

#include <string>
#include <nix/flake/flake.hh>
#include <nix/args/root.hh>
#include <nix/common-eval-args.hh>

class MyArgs : virtual public nix::MixEvalArgs,
               virtual public nix::MixCommonArgs,
               virtual nix::RootArgs {
  public:
    std::string releaseExpr;
    nix::Path gcRootsDir;
    bool flake = false;
    bool fromArgs = false;
    bool meta = false;
    bool showTrace = false;
    bool impure = false;
    bool forceRecurse = false;
    bool checkCacheStatus = false;
    size_t nrWorkers = 1;
    size_t maxMemorySize = 4096;

    // usually in MixFlakeOptions
    nix::flake::LockFlags lockFlags = {.updateLockFile = false,
                                       .writeLockFile = false,
                                       .useRegistries = false,
                                       .allowUnlocked = false};
    MyArgs();
    MyArgs(const MyArgs&) = delete;

    void parseArgs(char** argv, int argc);
};
