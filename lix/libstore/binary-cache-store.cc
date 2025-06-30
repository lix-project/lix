#include "lix/libutil/archive.hh"
#include "lix/libstore/binary-cache-store.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/compression.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/fs-accessor.hh"
#include "lix/libstore/nar-info.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/sync.hh"
#include "lix/libstore/remote-fs-accessor.hh"
#include "lix/libstore/nar-info-disk-cache.hh" // IWYU pragma: keep
#include "lix/libstore/nar-accessor.hh"
#include "lix/libstore/temporary-dir.hh"
#include "lix/libutil/thread-pool.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/strings.hh"

#include <chrono>
#include <functional>
#include <regex>
#include <fstream>
#include <sstream>

namespace nix {

BinaryCacheStore::BinaryCacheStore(const BinaryCacheStoreConfig & config)
{
    if (config.secretKeyFile != "")
        secretKey = std::unique_ptr<SecretKey>(new SecretKey(readFile(config.secretKeyFile)));

    StringSink sink;
    sink << narVersionMagic1;
    narMagic = sink.s;
}

kj::Promise<Result<void>> BinaryCacheStore::init()
try {
    std::string cacheInfoFile = "nix-cache-info";

    auto cacheInfo = TRY_AWAIT(getFileContents(cacheInfoFile));
    if (!cacheInfo) {
        TRY_AWAIT(upsertFile(
            cacheInfoFile, "StoreDir: " + config().storeDir + "\n", "text/x-nix-cache-info"
        ));
    } else {
        for (auto & line : tokenizeString<Strings>(*cacheInfo, "\n")) {
            size_t colon= line.find(':');
            if (colon ==std::string::npos) continue;
            auto name = line.substr(0, colon);
            auto value = trim(line.substr(colon + 1, std::string::npos));
            if (name == "StoreDir") {
                if (value != config().storeDir)
                    throw Error("binary cache '%s' is for Nix stores with prefix '%s', not '%s'",
                        getUri(), value, config().storeDir);
            } else if (name == "WantMassQuery") {
                config().wantMassQuery.setDefault(value == "1");
            } else if (name == "Priority") {
                config().priority.setDefault(std::stoi(value));
            }
        }
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> BinaryCacheStore::upsertFile(
    const std::string & path, std::string && data, const std::string & mimeType
)
try {
    TRY_AWAIT(upsertFile(path, std::make_shared<std::stringstream>(std::move(data)), mimeType));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<std::string>>>
BinaryCacheStore::getFileContents(const std::string & path)
try {
    try {
        co_return TRY_AWAIT(TRY_AWAIT(getFile(path))->drain());
    } catch (NoSuchBinaryCacheFile &) {
        co_return std::nullopt;
    }
} catch (...) {
    co_return result::current_exception();
}

std::string BinaryCacheStore::narInfoFileFor(const StorePath & storePath)
{
    return std::string(storePath.hashPart()) + ".narinfo";
}

kj::Promise<Result<void>> BinaryCacheStore::writeNarInfo(ref<NarInfo> narInfo)
try {
    auto narInfoFile = narInfoFileFor(narInfo->path);

    TRY_AWAIT(upsertFile(narInfoFile, narInfo->to_string(*this), "text/x-nix-narinfo"));

    {
        auto state_(co_await state.lock());
        state_->pathInfoCache.upsert(
            std::string(narInfo->path.to_string()),
            PathInfoCacheValue { .value = std::shared_ptr<NarInfo>(narInfo) });
    }

    if (diskCache)
        diskCache->upsertNarInfo(getUri(), std::string(narInfo->path.hashPart()), std::shared_ptr<NarInfo>(narInfo));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<ref<const ValidPathInfo>>> BinaryCacheStore::addToStoreCommon(
    AsyncInputStream & narSource, RepairFlag repair, CheckSigsFlag checkSigs,
    std::function<ValidPathInfo(HashResult)> mkInfo)
try {
    auto [fdTemp, fnTemp] = createTempFile();

    AutoDelete autoDelete(fnTemp);

    auto now1 = std::chrono::steady_clock::now();

    /* Read the NAR simultaneously into a CompressionSink+FileSink (to
       write the compressed NAR to disk), into a HashSink (to get the
       NAR hash), and into a NarAccessor (to get the NAR listing). */
    HashSink fileHashSink { HashType::SHA256 };
    nar_index::Entry narIndex;
    HashSink narHashSink { HashType::SHA256 };
    {
        FdSink fileSink(fdTemp.get());
        TeeSink teeSinkCompressed { fileSink, fileHashSink };
        auto compressionSink = makeCompressionSink(
            config().compression,
            teeSinkCompressed,
            config().parallelCompression,
            config().compressionLevel
        );
        TeeSink teeSinkUncompressed { *compressionSink, narHashSink };
        AsyncTeeInputStream teeSource { narSource, teeSinkUncompressed };
        narIndex = TRY_AWAIT(nar_index::create(teeSource));
        compressionSink->finish();
        fileSink.flush();
    }

    auto now2 = std::chrono::steady_clock::now();

    auto info = mkInfo(narHashSink.finish());
    auto narInfo = make_ref<NarInfo>(info);
    narInfo->compression = config().compression;
    auto [fileHash, fileSize] = fileHashSink.finish();
    narInfo->fileHash = fileHash;
    narInfo->fileSize = fileSize;
    narInfo->url = "nar/" + narInfo->fileHash->to_string(Base::Base32, false) + ".nar"
        + (config().compression == "xz" ? ".xz" :
           config().compression == "bzip2" ? ".bz2" :
           config().compression == "zstd" ? ".zst" :
           config().compression == "lzip" ? ".lzip" :
           config().compression == "lz4" ? ".lz4" :
           config().compression == "br" ? ".br" :
           "");

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();
    printMsg(lvlTalkative, "copying path '%1%' (%2% bytes, compressed %3$.1f%% in %4% ms) to binary cache",
        printStorePath(narInfo->path), info.narSize,
        ((1.0 - (double) fileSize / info.narSize) * 100.0),
        duration);

    /* Verify that all references are valid. This may do some .narinfo
       reads, but typically they'll already be cached. */
    for (auto & ref : info.references)
        try {
            if (ref != info.path)
                TRY_AWAIT(queryPathInfo(ref));
        } catch (InvalidPath &) {
            throw Error("cannot add '%s' to the binary cache because the reference '%s' does not exist",
                printStorePath(info.path), printStorePath(ref));
        }

    /* Optionally write a JSON file containing a listing of the
       contents of the NAR. */
    if (config().writeNARListing) {
        JSON j = {
            {"version", 1},
            {"root", listNar(narIndex)},
        };

        try {
            TRY_AWAIT(
                upsertFile(std::string(info.path.hashPart()) + ".ls", j.dump(), "application/json")
            );
        } catch (ForeignException & exc) {
            if (exc.is<JSON::exception>()) {
                warn(
                    "Skipping NAR listing for path '%1%' due to serialization failure: %2%",
                    printStorePath(narInfo->path),
                    exc.what()
                );
            } else {
                throw exc;
            }
        }
    }

    /* Optionally maintain an index of DWARF debug info files
       consisting of JSON files named 'debuginfo/<build-id>' that
       specify the NAR file and member containing the debug info. */
    if (config().writeDebugInfo) {

        const std::string buildIdPath = "/lib/debug/.build-id";

        auto * buildIdDir = std::get_if<nar_index::Directory>(&narIndex);
        for (auto subdir : { "lib", "debug", ".build-id" }) {
            // get returns nullptr subdir does not exist, and std::get_if propagates it.
            buildIdDir = std::get_if<nar_index::Directory>(get(buildIdDir->contents, subdir));
        }

        if (buildIdDir) {

            ThreadPool threadPool("write debuginfo pool", 25);

            auto doFile = [&](AsyncIoRoot & aio,
                              std::string member,
                              std::string key,
                              std::string target) {
                JSON json;
                json["archive"] = target;
                json["member"] = member;

                // FIXME: or should we overwrite? The previous link may point
                // to a GC'ed file, so overwriting might be useful...
                if (aio.blockOn(fileExists(key))) {
                    return;
                }

                printMsg(lvlTalkative, "creating debuginfo link from '%s' to '%s'", key, target);

                aio.blockOn(upsertFile(key, json.dump(), "application/json"));
            };

            std::regex regex1 = regex::parse("^[0-9a-f]{2}$");
            std::regex regex2 = regex::parse("^[0-9a-f]{38}\\.debug$");

            for (auto & [s1, s1Inode] : buildIdDir->contents) {
                auto dir = std::get_if<nar_index::Directory>(&s1Inode);

                if (!dir || !std::regex_match(s1, regex1))
                    continue;

                for (auto & [s2, s2Inode] : dir->contents) {
                    auto debugPath = fmt("%s/%s/%s", buildIdPath, s1, s2);

                    auto file = std::get_if<nar_index::File>(&s2Inode);
                    if (!file || !std::regex_match(s2, regex2))
                        continue;

                    auto buildId = s1 + s2;

                    std::string key = "debuginfo/" + buildId;
                    std::string target = "../" + narInfo->url;

                    threadPool.enqueueWithAio(std::bind(
                        doFile, std::placeholders::_1, std::string(debugPath, 1), key, target
                    ));
                }
            }

            threadPool.process();
        }
    }

    /* Atomically write the NAR file. */
    if (repair || !TRY_AWAIT(fileExists(narInfo->url))) {
        stats.narWrite++;
        TRY_AWAIT(upsertFile(
            narInfo->url,
            std::make_shared<std::fstream>(fnTemp, std::ios_base::in | std::ios_base::binary),
            "application/x-nix-nar"
        ));
    } else {
        stats.narWriteAverted++;
    }

    stats.narWriteBytes += info.narSize;
    stats.narWriteCompressedBytes += fileSize;
    stats.narWriteCompressionTimeMs += duration;

    /* Atomically write the NAR info file.*/
    if (secretKey) narInfo->sign(*this, *secretKey);

    TRY_AWAIT(writeNarInfo(narInfo));

    stats.narInfoWrite++;

    co_return narInfo;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> BinaryCacheStore::addToStore(
    const ValidPathInfo & info,
    AsyncInputStream & narSource,
    RepairFlag repair,
    CheckSigsFlag checkSigs
)
try {
    if (!repair && TRY_AWAIT(isValidPath(info.path))) {
        // FIXME: copyNAR -> null sink
        TRY_AWAIT(narSource.drain());
        co_return result::success();
    }

    TRY_AWAIT(addToStoreCommon(narSource, repair, checkSigs, {[&](HashResult nar) {
        /* FIXME reinstate these, once we can correctly do hash modulo sink as
           needed. We need to throw here in case we uploaded a corrupted store path. */
        // assert(info.narHash == nar.first);
        // assert(info.narSize == nar.second);
        return info;
    }}));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<StorePath>> BinaryCacheStore::addToStoreFromDump(
    AsyncInputStream & dump,
    std::string_view name,
    FileIngestionMethod method,
    HashType hashAlgo,
    RepairFlag repair,
    const StorePathSet & references
)
try {
    if (method != FileIngestionMethod::Recursive || hashAlgo != HashType::SHA256)
        unsupported("addToStoreFromDump");
    co_return TRY_AWAIT(addToStoreCommon(dump, repair, CheckSigs, [&](HashResult nar) {
        ValidPathInfo info {
            *this,
            name,
            FixedOutputInfo {
                .method = method,
                .hash = nar.first,
                .references = {
                    .others = references,
                    // caller is not capable of creating a self-reference, because this is content-addressed without modulus
                    .self = false,
                },
            },
            nar.first,
        };
        info.narSize = nar.second;
        return info;
    }))->path;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<bool>> BinaryCacheStore::isValidPathUncached(const StorePath & storePath)
try {
    // FIXME: this only checks whether a .narinfo with a matching hash
    // part exists. So ‘f4kb...-foo’ matches ‘f4kb...-bar’, even
    // though they shouldn't. Not easily fixed.
    co_return TRY_AWAIT(fileExists(narInfoFileFor(storePath)));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<StorePath>>>
BinaryCacheStore::queryPathFromHashPart(const std::string & hashPart)
try {
    auto pseudoPath = StorePath(hashPart + "-" + MissingName);
    try {
        auto info = TRY_AWAIT(queryPathInfo(pseudoPath));
        co_return info->path;
    } catch (InvalidPath &) {
        co_return std::nullopt;
    }
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<box_ptr<AsyncInputStream>>>
BinaryCacheStore::narFromPath(const StorePath & storePath)
try {
    struct NarFromPath : AsyncInputStream
    {
        Stats<std::atomic> & stats;
        box_ptr<AsyncInputStream> decompressed;
        uint64_t total;

        NarFromPath(
            Stats<std::atomic> & stats, const std::string & method, box_ptr<AsyncInputStream> file
        )
            : stats(stats)
            , decompressed(makeDecompressionStream(method, std::move(file)))
        {
        }

        kj::Promise<Result<size_t>> read(void * buffer, size_t size) override
        {
            return decompressed->read(buffer, size).then([&](auto r) {
                if (r.has_value()) {
                    if (r.value() > 0) {
                        total += r.value();
                    } else {
                        stats.narRead++;
                        // stats.narReadCompressedBytes += nar->size(); // FIXME
                        stats.narReadBytes += total;
                    }
                }
                return r;
            });
        }
    };

    auto info_ = TRY_AWAIT(queryPathInfo(storePath)).try_cast<const NarInfo>();
    assert(info_ && "binary cache queryPathInfo didn't return a NarInfo");
    auto & info = *info_;

    try {
        auto file = TRY_AWAIT(getFile(info->url));
        co_return make_box_ptr<NarFromPath>(stats, info->compression, std::move(file));
    } catch (NoSuchBinaryCacheFile & e) {
        throw SubstituteGone(std::move(e.info()));
    }
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::shared_ptr<const ValidPathInfo>>>
BinaryCacheStore::queryPathInfoUncached(const StorePath & storePath)
try {
    auto uri = getUri();
    auto storePathS = printStorePath(storePath);
    auto act = std::make_shared<Activity>(*logger, lvlTalkative, actQueryPathInfo,
        fmt("querying info about '%s' on '%s'", storePathS, uri), Logger::Fields{storePathS, uri});
    PushActivity pact(act->id);

    auto narInfoFile = narInfoFileFor(storePath);

    auto data = TRY_AWAIT(getFileContents(narInfoFile));

    if (!data) co_return result::success(nullptr);

    stats.narInfoRead++;

    co_return std::make_shared<NarInfo>(*this, *data, narInfoFile);
} catch (...) {
    co_return result::current_exception();
}

static ValidPathInfo makeAddToStoreInfo(
    HashResult nar, Store & store, FileIngestionMethod method, std::string_view name, Hash h
)
{
    ValidPathInfo info{
        store,
        name,
        FixedOutputInfo {
            .method = method,
            .hash = h,
            .references = {
                .others = {},
                // caller is not capable of creating a self-reference, because this is content-addressed without modulus
                .self = false,
            },
        },
        nar.first,
    };
    info.narSize = nar.second;
    return info;
}

kj::Promise<Result<StorePath>> BinaryCacheStore::addToStoreRecursive(
    std::string_view name,
    const PreparedDump & _source,
    HashType hashAlgo,
    RepairFlag repair)
try {
    /* FIXME: Make BinaryCacheStore::addToStoreCommon support
       non-recursive+sha256 so we can just use the default
       implementation of this method in terms of addToStoreFromDump. */

    HashSink sink { hashAlgo };
    sink << _source.dump();
    auto h = sink.finish().first;

    auto source = AsyncGeneratorInputStream{_source.dump()};
    co_return TRY_AWAIT(addToStoreCommon(source, repair, CheckSigs, [&](HashResult nar) {
        return makeAddToStoreInfo(nar, *this, FileIngestionMethod::Recursive, name, h);
    }))->path;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<StorePath>> BinaryCacheStore::addToStoreFlat(
    std::string_view name,
    const Path & srcPath,
    HashType hashAlgo,
    RepairFlag repair)
try {
    /* FIXME: Make BinaryCacheStore::addToStoreCommon support
       non-recursive+sha256 so we can just use the default
       implementation of this method in terms of addToStoreFromDump. */

    HashSink sink { hashAlgo };
    sink << readFileSource(srcPath);
    auto h = sink.finish().first;

    auto source = AsyncGeneratorInputStream{dumpPath(srcPath)};
    co_return TRY_AWAIT(addToStoreCommon(source, repair, CheckSigs, [&](HashResult nar) {
        return makeAddToStoreInfo(nar, *this, FileIngestionMethod::Flat, name, h);
    }))->path;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<StorePath>> BinaryCacheStore::addTextToStore(
    std::string_view name,
    std::string_view s,
    const StorePathSet & references,
    RepairFlag repair)
try {
    auto textHash = hashString(HashType::SHA256, s);
    auto path = makeTextPath(name, TextInfo { { textHash }, references });

    if (!repair && TRY_AWAIT(isValidPath(path)))
        co_return path;

    StringSink sink;
    sink << dumpString(s);
    AsyncStringInputStream source(sink.s);
    co_return TRY_AWAIT(addToStoreCommon(source, repair, CheckSigs, [&](HashResult nar) {
        ValidPathInfo info {
            *this,
            std::string { name },
            TextInfo {
                .hash = textHash,
                .references = references,
            },
            nar.first,
        };
        info.narSize = nar.second;
        return info;
    }))->path;
} catch (...) {
    co_return result::current_exception();
}

ref<FSAccessor> BinaryCacheStore::getFSAccessor()
{
    return make_ref<RemoteFSAccessor>(ref<Store>(*this), config().localNarCache);
}

kj::Promise<Result<void>>
BinaryCacheStore::addSignatures(const StorePath & storePath, const StringSet & sigs)
try {
    /* Note: this is inherently racy since there is no locking on
       binary caches. In particular, with S3 this unreliable, even
       when addSignatures() is called sequentially on a path, because
       S3 might return an outdated cached version. */

    // downcast: BinaryCacheStore always returns NarInfo from queryPathInfoUncached, making it sound
    auto narInfo =
        make_ref<NarInfo>(dynamic_cast<NarInfo const &>(*TRY_AWAIT(queryPathInfo(storePath))));

    narInfo->sigs.insert(sigs.begin(), sigs.end());

    TRY_AWAIT(writeNarInfo(narInfo));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<std::string>>>
BinaryCacheStore::getBuildLogExact(const StorePath & path)
try {
    auto logPath = "log/" + std::string(baseNameOf(printStorePath(path)));

    debug("fetching build log from binary cache '%s/%s'", getUri(), logPath);

    co_return TRY_AWAIT(getFileContents(logPath));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>>
BinaryCacheStore::addBuildLog(const StorePath & drvPath, std::string_view log)
try {
    assert(drvPath.isDerivation());

    TRY_AWAIT(upsertFile(
        "log/" + std::string(drvPath.to_string()),
        (std::string) log, // FIXME: don't copy
        "text/plain; charset=utf-8"
    ));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

}
