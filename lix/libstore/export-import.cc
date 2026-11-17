#include "lix/libutil/result.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/archive.hh"
#include "lix/libstore/common-protocol.hh"
#include "lix/libstore/common-protocol-impl.hh"

#include <algorithm>

namespace nix {

static kj::Promise<Result<void>> exportPath(Store & source, const StorePath & path, Sink & sink);

kj::Promise<Result<void>> exportPaths(Store & source, const StorePathSet & paths, Sink & sink)
try {
    auto sorted = TRY_AWAIT(source.topoSortPaths(paths));
    std::reverse(sorted.begin(), sorted.end());

    for (auto & path : sorted) {
        sink << 1;
        TRY_AWAIT(exportPath(source, path, sink));
    }

    sink << 0;
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> exportPath(Store & source, const StorePath & path, Sink & sink)
try {
    auto info = TRY_AWAIT(source.queryPathInfo(path));

    HashSink hashSink(HashType::SHA256);
    TeeSink teeSink(sink, hashSink);

    TRY_AWAIT(TRY_AWAIT(source.narFromPath(path))->drainInto(teeSink));

    /* Refuse to export paths that have changed.  This prevents
       filesystem corruption from spreading to other machines.
       Don't complain if the stored hash is zero (unknown). */
    Hash hash = hashSink.currentHash().first;
    if (hash != info->narHash && info->narHash != Hash(info->narHash.type))
        throw Error(
            "hash of path '%s' has changed from '%s' to '%s'!",
            source.printStorePath(path),
            info->narHash.to_sri(),
            hash.to_sri()
        );

    teeSink << exportMagic << source.printStorePath(path);
    teeSink << CommonProto::write(CommonProto::WriteConn{source}, info->references);
    teeSink << (info->deriver ? source.printStorePath(*info->deriver) : "") << 0;
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<StorePaths>> importPaths(Store & target, Source & source, CheckSigsFlag checkSigs)
try {
    StorePaths res;
    while (true) {
        auto n = readNum<uint64_t>(source);
        if (n == 0) break;
        if (n != 1) throw Error("input doesn't look like something created by 'nix-store --export'");

        /* Extract the NAR from the source. */
        StringSink saved;
        saved << copyNAR(source);

        uint32_t magic = readNum<unsigned>(source);
        if (magic != exportMagic)
            throw Error("Nix archive cannot be imported; wrong format");

        auto path = target.parseStorePath(readString(source));

        //Activity act(*logger, lvlInfo, "importing path '%s'", info.path);

        auto references = CommonProto::Serialise<StorePathSet>::read(
            CommonProto::ReadConn{.from = source, .store = target}
        );
        auto deriver = readString(source);
        auto narHash = hashString(HashType::SHA256, saved.s);

        ValidPathInfo info { path, narHash };
        if (deriver != "")
            info.deriver = target.parseStorePath(deriver);
        info.references = references;
        info.narSize = saved.s.size();

        // Ignore optional legacy signature.
        if (readNum<unsigned>(source) == 1) {
            readString(source);
        }

        // Can't use underlying source, which would have been exhausted
        auto source = AsyncStringInputStream(saved.s);
        TRY_AWAIT(target.addToStore(info, source, NoRepair, checkSigs));

        res.push_back(info.path);
    }

    co_return res;
} catch (...) {
    co_return result::current_exception();
}

}
