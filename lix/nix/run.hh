#pragma once
///@file

#include "lix/libstore/store-api.hh"

namespace nix {

static constexpr std::string_view chrootHelperName = "__run_in_chroot";

void chrootHelper(int argc, char ** argv);

enum struct UseSearchPath { Use, DontUse };

void runProgramInStore(
    ref<Store> store,
    UseSearchPath useSearchPath,
    const std::string & program,
    const Strings & args,
    std::optional<std::string_view> system = std::nullopt
);

void registerNixRun();

}
