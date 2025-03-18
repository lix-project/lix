#include "lix/libcmd/command.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/store-cast.hh"
#include "lix/libstore/log-store.hh"
#include "lix/libutil/sync.hh"
#include "lix/libutil/thread-pool.hh"
#include "store-copy-log.hh"

#include <atomic>

namespace nix {

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
                aio().blockOn(dstLogStore.addBuildLog(drvPath, *log));
            else
                throw Error("build log for '%s' is not available", srcStore->printStorePath(drvPath));
        }
    }
};

void registerNixStoreCopyLog()
{
    registerCommand2<CmdCopyLog>({"store", "copy-log"});
}

}
