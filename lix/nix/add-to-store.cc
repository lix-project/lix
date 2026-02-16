#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libstore/path.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/json.hh"
#include "add-to-store.hh"

namespace nix {

struct CmdAddToStore : MixDryRun, StoreCommand
{
    Path path;
    std::optional<std::string> namePart;
    std::optional<Path> referencesListFile;
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
        StorePathSet references{};
        if (!namePart) namePart = baseNameOf(path);
        if (referencesListFile.has_value()) {
            auto parsed = json::parse(readFile(*referencesListFile), "references list file");
            for (auto it : parsed.get<std::vector<std::string_view>>()) {
                references.insert(store->parseStorePath(it));
            }
        }

        StringSink sink;
        sink << dumpPath(path);

        auto narHash = hashString(HashType::SHA256, sink.s);

        Hash hash = narHash;
        if (ingestionMethod == FileIngestionMethod::Flat) {
            HashSink hsink(HashType::SHA256);
            hsink << readFileSource(path);
            hash = hsink.finish().first;
        }

        ValidPathInfo info{
            *store,
            std::move(*namePart),
            FixedOutputInfo{
                .method = std::move(ingestionMethod),
                .hash = std::move(hash),
                .references = {references},
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

        // References are only available for the recursive ingest method; the
        // store will tell us "fixed output derivation is not allowed to refer
        // to other store paths" for the flat ingest method.
        addFlag({
            .longName = "references-list-json",
            .description = "File containing a JSON list of references of the to-be-added store path",
            .labels = {"file"},
            .handler = {&referencesListFile},
            .completer = completePath,
        });
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
