#include "lix/libcmd/command.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/make-content-addressed.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libutil/json.hh"
#include "make-content-addressed.hh"

namespace nix {

struct CmdMakeContentAddressed : virtual CopyCommand, virtual StorePathsCommand, MixJSON
{
    CmdMakeContentAddressed()
    {
        realiseMode = Realise::Outputs;
    }

    std::string description() override
    {
        return "rewrite a path or closure to content-addressed form";
    }

    std::string doc() override
    {
        return
          #include "make-content-addressed.md"
          ;
    }

    void run(ref<Store> srcStore, StorePaths && storePaths) override
    {
        auto dstStore = aio().blockOn(dstUri.empty() ? openStore() : openStore(dstUri));

        auto remappings = aio().blockOn(makeContentAddressed(*srcStore, *dstStore,
            StorePathSet(storePaths.begin(), storePaths.end())));

        if (json) {
            auto jsonRewrites = JSON::object();
            for (auto & path : storePaths) {
                auto i = remappings.find(path);
                assert(i != remappings.end());
                jsonRewrites[srcStore->printStorePath(path)] = srcStore->printStorePath(i->second);
            }
            auto json = JSON::object();
            json["rewrites"] = jsonRewrites;
            logger->cout("%s", json);
        } else {
            for (auto & path : storePaths) {
                auto i = remappings.find(path);
                assert(i != remappings.end());
                notice("rewrote '%s' to '%s'",
                    srcStore->printStorePath(path),
                    srcStore->printStorePath(i->second));
            }
        }
    }
};

void registerNixMakeContentAddressed()
{
    registerCommand2<CmdMakeContentAddressed>({"store", "make-content-addressed"});
}

}
