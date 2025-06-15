#include "lix/libstore/make-content-addressed.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/references.hh"
#include "lix/libutil/strings.hh"

namespace nix {

kj::Promise<Result<std::map<StorePath, StorePath>>> makeContentAddressed(
    Store & srcStore,
    Store & dstStore,
    const StorePathSet & storePaths)
try {
    StorePathSet closure;
    TRY_AWAIT(srcStore.computeFSClosure(storePaths, closure));

    auto paths = TRY_AWAIT(srcStore.topoSortPaths(closure));

    std::reverse(paths.begin(), paths.end());

    std::map<StorePath, StorePath> remappings;

    for (auto & path : paths) {
        auto pathS = srcStore.printStorePath(path);
        auto oldInfo = TRY_AWAIT(srcStore.queryPathInfo(path));
        std::string oldHashPart(path.hashPart());

        StringSink sink;
        TRY_AWAIT(TRY_AWAIT(srcStore.narFromPath(path))->drainInto(sink));

        StringMap rewrites;

        StoreReferences refs;
        for (auto & ref : oldInfo->references) {
            if (ref == path)
                refs.self = true;
            else {
                auto i = remappings.find(ref);
                auto replacement = i != remappings.end() ? i->second : ref;
                // FIXME: warn about unremapped paths?
                if (replacement != ref)
                    rewrites.insert_or_assign(srcStore.printStorePath(ref), srcStore.printStorePath(replacement));
                refs.others.insert(std::move(replacement));
            }
        }

        sink.s = rewriteStrings(sink.s, rewrites);

        auto narModuloHash = [&] {
            StringSource source{sink.s};
            return computeHashModulo(HashType::SHA256, oldHashPart, source).first;
        }();

        ValidPathInfo info {
            dstStore,
            path.name(),
            FixedOutputInfo {
                .method = FileIngestionMethod::Recursive,
                .hash = narModuloHash,
                .references = std::move(refs),
            },
            Hash::dummy,
        };

        printInfo("rewriting '%s' to '%s'", pathS, dstStore.printStorePath(info.path));

        const auto rewritten = rewriteStrings(sink.s, {{oldHashPart, std::string(info.path.hashPart())}});

        info.narHash = hashString(HashType::SHA256, rewritten);
        info.narSize = sink.s.size();

        AsyncStringInputStream source(rewritten);
        TRY_AWAIT(dstStore.addToStore(info, source));

        remappings.insert_or_assign(std::move(path), std::move(info.path));
    }

    co_return remappings;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<StorePath>> makeContentAddressed(
    Store & srcStore,
    Store & dstStore,
    const StorePath & fromPath)
try {
    auto remappings = TRY_AWAIT(makeContentAddressed(srcStore, dstStore, StorePathSet{fromPath}));
    auto i = remappings.find(fromPath);
    assert(i != remappings.end());
    co_return i->second;
} catch (...) {
    co_return result::current_exception();
}

}
