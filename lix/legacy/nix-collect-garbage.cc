#include "lix/libutil/file-system.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/store-cast.hh"
#include "lix/libstore/gc-store.hh"
#include "lix/libstore/profiles.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/globals.hh"
#include "lix/libcmd/legacy.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/types.hh"
#include "nix-collect-garbage.hh"

#include <cerrno>

namespace nix {

std::string deleteOlderThan;
bool dryRun = false;


/* If `-d' was specified, remove all old generations of all profiles.
 * Of course, this makes rollbacks to before this point in time
 * impossible. */

static void removeOldGenerations(std::string dir, NeverAsync = {})
{
    if (access(dir.c_str(), R_OK) != 0) return;

    bool canWrite = access(dir.c_str(), W_OK) == 0;

    for (auto & i : readDirectory(dir)) {
        checkInterrupt();

        auto path = dir + "/" + i.name;
        auto type = i.type == DT_UNKNOWN ? getFileType(path) : i.type;

        if (type == DT_LNK && canWrite) {
            std::string link;
            try {
                link = readLink(path);
            } catch (SysError & e) {
                if (e.errNo == ENOENT) continue;
                throw;
            }
            if (link.find("link") != std::string::npos) {
                printInfo("removing old generations of profile %s", path);
                if (deleteOlderThan != "") {
                    auto t = parseOlderThanTimeSpec(deleteOlderThan);
                    deleteGenerationsOlderThan(path, t, dryRun);
                } else
                    deleteOldGenerations(path, dryRun);
            }
        } else if (type == DT_DIR) {
            removeOldGenerations(path);
        }
    }
}

static int main_nix_collect_garbage(AsyncIoRoot & aio, std::string programName, Strings argv)
{
    {
        bool removeOld = false;

        GCOptions options;

        LegacyArgs(aio, programName, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-collect-garbage");
            else if (*arg == "--version")
                printVersion("nix-collect-garbage");
            else if (*arg == "--delete-old" || *arg == "-d") removeOld = true;
            else if (*arg == "--delete-older-than") {
                removeOld = true;
                deleteOlderThan = getArg(*arg, arg, end);
            }
            else if (*arg == "--dry-run") dryRun = true;
            else if (*arg == "--max-freed")
                options.maxFreed = std::max(getIntArg<int64_t>(*arg, arg, end, true), (int64_t) 0);
            else
                return false;
            return true;
        }).parseCmdline(argv);

        if (removeOld) {
            std::set<Path> dirsToClean = {
                profilesDir(), settings.nixStateDir + "/profiles", dirOf(getDefaultProfile())};
            for (auto & dir : dirsToClean)
                removeOldGenerations(dir);
        }

        // Run the actual garbage collector.
        if (!dryRun) {
            options.action = GCOptions::gcDeleteDead;
        } else {
            options.action = GCOptions::gcReturnDead;
        }
        auto store = aio.blockOn(openStore());
        auto & gcStore = require<GcStore>(*store);
        GCResults results;
        PrintFreed freed(true, results);
        aio.blockOn(gcStore.collectGarbage(options, results));

        if (dryRun) {
            // Only print results for dry run; when !dryRun, paths will be printed as they're deleted.
            for (auto & i : results.paths) {
                printInfo("%s", Uncolored(i));
            }
        }

        return 0;
    }
}

void registerLegacyNixCollectGarbage() {
    LegacyCommandRegistry::add("nix-collect-garbage", main_nix_collect_garbage);
}

}
