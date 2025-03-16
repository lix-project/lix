#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/async-io.hh"
#include "add-to-store.hh"

namespace nix {

struct CmdAddToStore : MixDryRun, StoreCommand
{
    Path path;
    std::optional<std::string> namePart;
    FileIngestionMethod ingestionMethod;

    CmdAddToStore()
    {
        // FIXME: completion
        expectArg("path", &path);

        addFlag({
            .longName = "name",
            .shortName = 'n',
            .description = "Override the name component of the store path. It defaults to the base name of *path*.",
            .labels = {"name"},
            .handler = {&namePart},
        });
    }

    void run(ref<Store> store) override
    {
        if (!namePart) namePart = baseNameOf(path);

        StringSink sink;
        sink << dumpPath(path);

        auto narHash = hashString(HashType::SHA256, sink.s);

        Hash hash = narHash;
        if (ingestionMethod == FileIngestionMethod::Flat) {
            HashSink hsink(HashType::SHA256);
            hsink << readFileSource(path);
            hash = hsink.finish().first;
        }

        ValidPathInfo info {
            *store,
            std::move(*namePart),
            FixedOutputInfo {
                .method = std::move(ingestionMethod),
                .hash = std::move(hash),
                .references = {},
            },
            narHash,
        };
        info.narSize = sink.s.size();

        if (!dryRun) {
            auto source = AsyncStringInputStream(sink.s);
            aio().blockOn(store->addToStore(info, source));
        }

        logger->cout("%s", store->printStorePath(info.path));
    }
};

struct CmdAddFile : CmdAddToStore
{
    CmdAddFile()
    {
        ingestionMethod = FileIngestionMethod::Flat;
    }

    std::string description() override
    {
        return "add a regular file to the Nix store";
    }

    std::string doc() override
    {
        return
          #include "add-file.md"
          ;
    }
};

struct CmdAddPath : CmdAddToStore
{
    CmdAddPath()
    {
        ingestionMethod = FileIngestionMethod::Recursive;
    }

    std::string description() override
    {
        return "add a path to the Nix store";
    }

    std::string doc() override
    {
        return
          #include "add-path.md"
          ;
    }
};

void registerNixStoreAdd()
{
    registerCommand2<CmdAddFile>({"store", "add-file"});
    registerCommand2<CmdAddPath>({"store", "add-path"});
}

}
