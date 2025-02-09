#include "lix/libcmd/command.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/store-cast.hh"
#include "lix/libstore/log-store.hh"
#include "lix/libutil/sync.hh"
#include "lix/libutil/thread-pool.hh"

#include <atomic>

using namespace nix;

struct CmdCopyLog : virtual CopyCommand, virtual InstallablesCommand
{
    std::string description() override
    {
        return "copy build logs between Nix stores";
    }

    std::string doc() override
    {
        return
          #include "store-copy-log.md"
          ;
    }

    void run(ref<Store> srcStore, Installables && installables) override
    {
        auto & srcLogStore = require<LogStore>(*srcStore);

        auto dstStore = getDstStore();
        auto & dstLogStore = require<LogStore>(*dstStore);

        for (auto & drvPath : Installable::toDerivations(
                 *getEvaluator()->begin(aio()), getEvalStore(), installables, true
             ))
        {
            if (auto log = aio().blockOn(srcLogStore.getBuildLog(drvPath)))
                dstLogStore.addBuildLog(drvPath, *log);
            else
                throw Error("build log for '%s' is not available", srcStore->printStorePath(drvPath));
        }
    }
};

static auto rCmdCopyLog = registerCommand2<CmdCopyLog>({"store", "copy-log"});
