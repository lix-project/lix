#include "lix/libcmd/command.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/fs-accessor.hh"
#include "lix/libstore/nar-accessor.hh"
#include "cat.hh"

namespace nix {

struct MixCat : virtual Args
{
    std::string path;

    void cat(ref<FSAccessor> accessor)
    {
        auto st = aio().blockOn(accessor->stat(path));
        if (st.type == FSAccessor::Type::tMissing)
            throw Error("path '%1%' does not exist", path);
        if (st.type != FSAccessor::Type::tRegular)
            throw Error("path '%1%' is not a regular file", path);

        auto file = aio().blockOn(accessor->readFile(path));

        logger->pause();
        writeFull(STDOUT_FILENO, file);
    }
};

struct CmdCatStore : StoreCommand, MixCat
{
    CmdCatStore()
    {
        expectArgs({
            .label = "path",
            .handler = {&path},
            .completer = completePath
        });
    }

    std::string description() override
    {
        return "print the contents of a file in the Nix store on stdout";
    }

    std::string doc() override
    {
        return
          #include "store-cat.md"
          ;
    }

    void run(ref<Store> store) override
    {
        cat(store->getFSAccessor());
    }
};

struct CmdCatNar : StoreCommand, MixCat
{
    Path narPath;

    CmdCatNar()
    {
        expectArgs({
            .label = "nar",
            .handler = {&narPath},
            .completer = completePath
        });
        expectArg("path", &path);
    }

    std::string description() override
    {
        return "print the contents of a file inside a NAR file on stdout";
    }

    std::string doc() override
    {
        return
          #include "nar-cat.md"
          ;
    }

    void run(ref<Store> store) override
    {
        cat(makeNarAccessor(readFile(narPath)));
    }
};

void registerNixCat()
{
    registerCommand2<CmdCatStore>({"store", "cat"});
    registerCommand2<CmdCatNar>({"nar", "cat"});
}

}
