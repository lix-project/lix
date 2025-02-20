#include "lix/libutil/result.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/archive.hh"
#include "lix/libstore/common-protocol.hh"
#include "lix/libstore/common-protocol-impl.hh"

#include <algorithm>

namespace nix {

kj::Promise<Result<void>> Store::exportPaths(const StorePathSet & paths, Sink & sink)
try {
    auto sorted = topoSortPaths(paths);
    std::reverse(sorted.begin(), sorted.end());

    for (auto & path : sorted) {
        sink << 1;
        TRY_AWAIT(exportPath(path, sink));
    }

    sink << 0;
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> Store::exportPath(const StorePath & path, Sink & sink)
try {
    auto info = queryPathInfo(path);

    HashSink hashSink(HashType::SHA256);
    TeeSink teeSink(sink, hashSink);

    narFromPath(path)->drainInto(teeSink);

    /* Refuse to export paths that have changed.  This prevents
       filesystem corruption from spreading to other machines.
       Don't complain if the stored hash is zero (unknown). */
    Hash hash = hashSink.currentHash().first;
    if (hash != info->narHash && info->narHash != Hash(info->narHash.type))
        throw Error("hash of path '%s' has changed from '%s' to '%s'!",
            printStorePath(path), info->narHash.to_string(Base::Base32, true), hash.to_string(Base::Base32, true));

    teeSink
        << exportMagic
        << printStorePath(path);
    teeSink << CommonProto::write(*this,
        CommonProto::WriteConn {},
        info->references);
    teeSink
        << (info->deriver ? printStorePath(*info->deriver) : "")
        << 0;
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<StorePaths>> Store::importPaths(Source & source, CheckSigsFlag checkSigs)
try {
    StorePaths res;
    while (true) {
        auto n = readNum<uint64_t>(source);
        if (n == 0) break;
        if (n != 1) throw Error("input doesn't look like something created by 'nix-store --export'");

        /* Extract the NAR from the source. */
        StringSink saved;
        saved << copyNAR(source);

        uint32_t magic = readInt(source);
        if (magic != exportMagic)
            throw Error("Nix archive cannot be imported; wrong format");

        auto path = parseStorePath(readString(source));

        //Activity act(*logger, lvlInfo, "importing path '%s'", info.path);

        auto references = CommonProto::Serialise<StorePathSet>::read(*this,
            CommonProto::ReadConn { .from = source });
        auto deriver = readString(source);
        auto narHash = hashString(HashType::SHA256, saved.s);

        ValidPathInfo info { path, narHash };
        if (deriver != "")
            info.deriver = parseStorePath(deriver);
        info.references = references;
        info.narSize = saved.s.size();

        // Ignore optional legacy signature.
        if (readInt(source) == 1)
            readString(source);

        // Can't use underlying source, which would have been exhausted
        auto source = StringSource(saved.s);
        TRY_AWAIT(addToStore(info, source, NoRepair, checkSigs));

        res.push_back(info.path);
    }

    co_return res;
} catch (...) {
    co_return result::current_exception();
}

}
