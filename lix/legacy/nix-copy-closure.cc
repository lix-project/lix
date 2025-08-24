#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libcmd/legacy.hh"
#include "nix-copy-closure.hh"

namespace nix {

static int main_nix_copy_closure(AsyncIoRoot & aio, std::string programName, Strings argv)
{
    {
        auto gzip = false;
        auto toMode = true;
        auto includeOutputs = false;
        auto dryRun = false;
        auto useSubstitutes = NoSubstitute;
        std::string sshHost;
        PathSet storePaths;

        LegacyArgs(aio, programName, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-copy-closure");
            else if (*arg == "--version")
                printVersion("nix-copy-closure");
            else if (*arg == "--gzip" || *arg == "--bzip2" || *arg == "--xz") {
                if (*arg != "--gzip")
                    printTaggedWarning("'%1%' is not implemented, falling back to gzip", *arg);
                gzip = true;
            } else if (*arg == "--from")
                toMode = false;
            else if (*arg == "--to")
                toMode = true;
            else if (*arg == "--include-outputs")
                includeOutputs = true;
            else if (*arg == "--show-progress")
                printMsg(lvlError, "Warning: '--show-progress' is not implemented");
            else if (*arg == "--dry-run")
                dryRun = true;
            else if (*arg == "--use-substitutes" || *arg == "-s")
                useSubstitutes = Substitute;
            else if (sshHost.empty())
                sshHost = *arg;
            else
                storePaths.insert(*arg);
            return true;
        }).parseCmdline(argv);

        if (sshHost.empty())
            throw UsageError("no host name specified");

        auto remoteUri = "ssh://" + sshHost + (gzip ? "?compress=true" : "");
        auto to = aio.blockOn(toMode ? openStore(remoteUri) : openStore());
        auto from = aio.blockOn(toMode ? openStore() : openStore(remoteUri));

        RealisedPath::Set storePaths2;
        for (auto & path : storePaths)
            storePaths2.insert(from->followLinksToStorePath(path));

        aio.blockOn(copyClosure(*from, *to, storePaths2, NoRepair, NoCheckSigs, useSubstitutes));

        return 0;
    }
}

void registerLegacyNixCopyClosure() {
    LegacyCommandRegistry::add("nix-copy-closure", main_nix_copy_closure);
}

}
