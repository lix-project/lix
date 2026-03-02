#include <sstream>
#include <sys/utsname.h>

#include "lix/libcmd/command.hh"
#include "lix/libexpr/eval-settings.hh"
#include "lix/libexpr/search-path.hh"
#include "lix/libfetchers/registry.hh"
#include "lix/libutil/logging.hh"
#include "lix/libstore/serve-protocol.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/local-fs-store.hh"
#include "lix/libstore/worker-protocol.hh"
#include "lix/libutil/exit.hh"
#include "lix/libutil/systemd.hh"
#include "lix/libutil/compile-time-features.hh"
#include "lix/libutil/users.hh"
#include "lix/libstore/profiles.hh"
#include "doctor.hh"

namespace nix {

namespace {

enum SearchPathBackingType {
    Channel,
    FlakeRegistry,
    FlakeRef,
    FilesystemPath,
    Other,
};

struct FlakeRegistryEntryFacts
{
    fetchers::Registry::Entry entry;
};

struct SearchPathFacts
{
    std::string originReference;
};

std::string formatProtocol(unsigned int proto)
{
    if (proto) {
        auto major = GET_PROTOCOL_MAJOR(proto) >> 8;
        auto minor = GET_PROTOCOL_MINOR(proto);
        return fmt("%1%.%2%", major, minor);
    }
    return "unknown";
}

bool checkPass(const std::string & msg) {
    notice(ANSI_GREEN "[PASS] " ANSI_NORMAL "%1%", Uncolored(msg));
    return true;
}

bool checkFail(const std::string & msg) {
    notice(ANSI_RED "[FAIL] " ANSI_NORMAL "%1%", Uncolored(msg));
    return false;
}

void checkInfo(const std::string & msg) {
    notice(ANSI_BLUE "[INFO] " ANSI_NORMAL "%1%", Uncolored(msg));
}

}

struct CmdDoctor : StoreCommand
{
    bool success = true;
    std::map<std::string, SearchPathFacts> searchPathFacts;
    std::map<std::string, FlakeRegistryEntryFacts> flakeRegistryFacts;

    /**
     * This command is stable before the others
     */
    std::optional<ExperimentalFeature> experimentalFeature() override
    {
        return std::nullopt;
    }

    std::string description() override
    {
        return "check your system for potential problems and print a PASS or FAIL for each check";
    }

    Category category() override { return catNixInstallation; }

    void run(ref<Store> store) override
    {
        printInfo("Collecting general system information");
        printGeneralSystemInfo();
        printInfo("Collecting general Nix configuration information");
        printGeneralNixInfo();
        printInfo("Running checks against store uri %1%", store->getUri());

        if (store.try_cast_shared<LocalFSStore>()) {
            success &= checkNixInPath();
            success &= checkProfileRoots(store);
        }
        success &= checkStoreProtocol(aio().blockOn(store->getProtocol()));
        checkTrustedUser(store);
        {
            Path profile = getEnv("NIX_PROFILE").value_or(getDefaultProfile());
            checkValidCurrentProfileGeneration(profile);
        }
        {
            printInfo("Collecting information about the Flake registry");
            success &= checkFlakeRegistry(store);
        }
        {
            printInfo("Collecting information about the ambient Nix search paths");
            success &= checkAmbientNixSearchPaths();
        }
        {
            printInfo("Collecting information about Nixpkgs provenance");
            success &= checkNixpkgsProvenance();
        }

        if (!success)
            throw Exit(2);
    }

    bool printGeneralSystemInfo()
    {
        checkInfo(fmt("Nix system type: '%1%'", settings.thisSystem));
        {
            std::stringstream ss;
            struct utsname uname_buf;
            if (auto err = uname(&uname_buf) != 0) {
                return checkFail(fmt("Failed to obtain information via uname: %d", err));
            }

            ss << uname_buf.sysname << " " << uname_buf.version;
#ifdef __linux__
            if (auto hostInfo = aio().blockOn(systemd::get_host_information())) {
                ss << ", " << hostInfo->os_pretty_name << ", " << hostInfo->build_id.value_or("no build id");
                checkInfo(fmt("Host OS: %s", ss.str()));

                checkInfo(fmt("Chassis: %s", hostInfo->chassis));
                checkInfo(
                    fmt("Hardware: %s (vendor: %s, version: %s)",
                        hostInfo->hardware_vendor,
                        hostInfo->hardware_model,
                        hostInfo->hardware_version)
                );
                checkInfo(
                    fmt("Firmware: %s (version: %s)", hostInfo->firmware_vendor, hostInfo->firmware_version)
                );
            }
#else
            checkInfo(fmt("Host OS: %s", ss.str()));
#endif
        }

        {
            auto targets = {"LOCALE", "LC_CTYPE", "LC_ALL", "LANG"};
            std::list<std::string> vars;

            for (auto & target : targets) {
                if (auto var = getEnvNonEmpty(target)) {
                    vars.push_back(fmt("%s=%s", target, *var));
                }
            }

            checkInfo(fmt("User locale: %s", concatStringsSep(", ", vars)));
        }

        return true;
    }

    bool printGeneralNixInfo()
    {
        checkInfo(fmt("Sandbox mode: %1%", settings.sandboxMode.to_string()));
        checkInfo(fmt("Version: %1%", nixVersion));
        checkInfo(fmt(
            "Additional system types: %1%", concatStringsSep(", ", settings.extraPlatforms.get())
        ));
        checkInfo(fmt("System configuration file: %1%", settings.nixConfDir + "/nix.conf"));
        checkInfo(
            fmt("User configuration files: %1%", concatStringsSep(":", settings.nixUserConfFiles))
        );
        checkInfo(fmt("Store directory: %1%", settings.nixStore));
        checkInfo(fmt("State directory: %1%", settings.nixStateDir));
        checkInfo(fmt("Data directory: %1%", settings.nixDataDir));
        // look into --version
        checkInfo(fmt("Features: %1%", concatStringsSep(", ", getNixFeatures())));

        return true;
    }

    bool checkFlakeRegistry(ref<Store> store)
    {
        try {
            auto registries = aio().blockOn(fetchers::getRegistries(store));
            for (auto & registry : registries) {
                for (auto & entry : registry->entries) {
                    flakeRegistryFacts[entry.from.toURL().to_string()] = {.entry = entry};
                }
            }
            return true;

        } catch (...) {
            return checkFail("Failed to obtain the Flake registry");
        }
    }

    bool checkAmbientNixSearchPaths()
    {
        bool nixPathOverridden = evalSettings.nixPath.overridden;
        bool influencedByEnvironment = getEnvNonEmpty("NIX_PATH") == std::nullopt;

        auto nixPathS = concatStringsSep(", ", evalSettings.nixPath.get());

        if (nixPathOverridden) {
            if (influencedByEnvironment) {
                checkInfo(fmt(
                    "Default (influenced by $NIX_PATH) Nix configuration search path: %s", nixPathS
                ));
            } else {
                checkInfo(fmt("Default Nix configuration search path: %s", nixPathS));
            }
        } else {
            checkInfo(fmt("Overridden Nix configuration search path: %s", nixPathS));
        }

        // Parse all entries one by one to construct the search path.
        for (auto entry : evalSettings.nixPath.get()) {
            auto elem = SearchPath::Elem::parse(entry);
            auto prefix = elem.prefix.s.empty() ? elem.path.s : elem.prefix.s;
            searchPathFacts[prefix] = {
                .originReference = entry,
            };
        }

        return true;
    }

    bool checkNixpkgsProvenance()
    {
        if (!searchPathFacts.contains("nixpkgs")) {
            return checkFail(
                "Search path does not contain nixpkgs. All evaluations using nixpkgs (including nix-shell) "
                "will fail."
            );
        }

        auto entry = searchPathFacts["nixpkgs"];
        checkInfo(fmt("Nixpkgs provenance: %s", entry.originReference));
        try {
            auto nixpkgsVersion = aio().blockOn(runProgram(
                "nix-instantiate",
                true,
                {"--eval", "--raw", "--expr", "(import <nixpkgs> { }).lib.version"},
                false
            ));
            checkInfo(fmt("Nixpkgs version: %s", nixpkgsVersion));
        } catch (...) {
            return checkFail("Failed obtaining the nixpkgs version: nixpkgs is either broken or invalid");
        }

        return true;
    }

    bool checkNixInPath()
    {
        PathSet dirs;

        for (auto & dir : tokenizeString<Strings>(getEnv("PATH").value_or(""), ":"))
            // Inaccessible-but-extant PATH elements can't be executed anyway.
            if (pathAccessible(dir + "/nix-env"))
                dirs.insert(dirOf(canonPath(dir + "/nix-env", true)));

        if (dirs.size() != 1) {
            std::stringstream ss;
            ss << "Multiple versions of nix found in PATH:\n";
            for (auto & dir : dirs)
                ss << "  " << dir << "\n";
            return checkFail(ss.str());
        }

        return checkPass("PATH contains only one nix version.");
    }

    bool checkProfileRoots(ref<Store> store)
    {
        PathSet dirs;

        for (auto & dir : tokenizeString<Strings>(getEnv("PATH").value_or(""), ":")) {
            Path profileDir = dirOf(dir);
            try {
                Path userEnv = canonPath(profileDir, true);

                if (store->isStorePath(userEnv) && userEnv.ends_with("user-environment")) {
                    while (profileDir.find("/profiles/") == std::string::npos && isLink(profileDir))
                        profileDir = absPath(readLink(profileDir), dirOf(profileDir));

                    if (profileDir.find("/profiles/") == std::string::npos)
                        dirs.insert(dir);
                }
            } catch (SysError &) {}
        }

        if (!dirs.empty()) {
            std::stringstream ss;
            ss << "Found profiles outside of " << settings.nixStateDir << "/profiles.\n"
               << "The generation this profile points to might not have a gcroot and could be\n"
               << "garbage collected, resulting in broken symlinks.\n\n";
            for (auto & dir : dirs)
                ss << "  " << dir << "\n";
            ss << "\n";
            return checkFail(ss.str());
        }

        return checkPass("All profiles are gcroots.");
    }

    bool checkStoreProtocol(unsigned int storeProto)
    {
        unsigned int clientProto = GET_PROTOCOL_MAJOR(SERVE_PROTOCOL_VERSION) == GET_PROTOCOL_MAJOR(storeProto)
            ? SERVE_PROTOCOL_VERSION
            : PROTOCOL_VERSION;

        if (clientProto != storeProto) {
            std::stringstream ss;
            ss << "Warning: protocol version of this client does not match the store.\n"
               << "While this is not necessarily a problem it's recommended to keep the client in\n"
               << "sync with the daemon.\n\n"
               << "Client protocol: " << formatProtocol(clientProto) << "\n"
               << "Store protocol: " << formatProtocol(storeProto) << "\n\n";
            return checkFail(ss.str());
        }

        return checkPass("Client protocol matches store protocol.");
    }

    void checkTrustedUser(ref<Store> store)
    {
        auto trustedMay = aio().blockOn(store->isTrustedClient());
        std::string_view trustedness = trustedMay ? (*trustedMay ? "trusted" : "not trusted") : "unknown trust";
        checkInfo(fmt("You are %s by store uri: %s", trustedness, store->getUri()));
    }

    void checkValidCurrentProfileGeneration(const Path & profile)
    {
        Generations generations;
        std::optional<GenerationNumber> currentGeneration;
        std::string errStr;

        try {
            std::tie(generations, currentGeneration) = findGenerations(profile);
        } catch (SysError const & e) {
            errStr = fmt(": %s", e.msg());
        }

        if (!currentGeneration) {
            std::stringstream ss;
            ss << "Error: current generation cannot be discovered for profile: '" << profile << "'";
            ss << errStr << "\n";
            checkFail(ss.str());
        } else {
            std::stringstream ss;
            ss << "You have " << generations.size() << " generations for profile '" << profile << "'\n";
            ss << "The current generation number is '" << *currentGeneration << "'\n";
            checkPass(ss.str());
        }
    }
};

void registerNixDoctor()
{
    registerCommand<CmdDoctor>("doctor");
}

}
