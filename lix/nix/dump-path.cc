#include "lix/libcmd/command.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/archive.hh"
#include "dump-path.hh"

namespace nix {

struct CmdDumpPath : StorePathCommand
{
    std::string description() override
    {
        return "serialise a store path to stdout in NAR format";
    }

    std::string doc() override
    {
        return
          #include "store-dump-path.md"
          ;
    }

    void run(ref<Store> store, const StorePath & storePath) override
    {
        logger->pause();
        FdSink sink(STDOUT_FILENO);
        aio().blockOn(aio().blockOn(store->narFromPath(storePath))->drainInto(sink));
        sink.flush();
    }
};

struct CmdDumpPath2 : Command
{
    Path path;

    CmdDumpPath2()
    {
        expectArgs({
            .label = "path",
            .handler = {&path},
            .completer = completePath
        });
    }

    std::string description() override
    {
        return "serialise a path to stdout in NAR format";
    }

    std::string doc() override
    {
        return
          #include "nar-dump-path.md"
          ;
    }

    void run() override
    {
        logger->pause();
        FdSink sink(STDOUT_FILENO);
        sink << dumpPath(path);
        sink.flush();
    }
};

void registerNixStoreDumpPath()
{
    registerCommand2<CmdDumpPath>({"store", "dump-path"});
    registerCommand2<CmdDumpPath2>({"nar", "dump-path"});
}

}
