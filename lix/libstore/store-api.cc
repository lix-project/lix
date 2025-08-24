#include "lix/libstore/fs-accessor.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/nar-info-disk-cache.hh"
#include "lix/libutil/async-collect.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/hash.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/sync.hh"
#include "lix/libutil/thread-pool.hh"
#include "lix/libutil/url.hh"
#include "lix/libutil/archive.hh"
#include "lix/libstore/uds-remote-store.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/strings.hh"
// FIXME this should not be here, see TODO below on
// `addMultipleToStore`.
#include "lix/libstore/worker-protocol.hh"
#include "lix/libutil/users.hh"

#include <functional>
#include <kj/async.h>
#include <memory>
#include <mutex>
#include <regex>

namespace nix {

BuildMode buildModeFromInteger(int raw) {
    switch (raw) {
    case bmNormal: return bmNormal;
    case bmRepair: return bmRepair;
    case bmCheck: return bmCheck;
    default: throw Error("Invalid BuildMode");
    }
}

bool Store::isInStore(PathView path) const
{
    return isInDir(path, config().storeDir);
}


std::pair<StorePath, Path> Store::toStorePath(PathView path) const
{
    if (!isInStore(path))
        throw Error("path '%1%' is not in the Nix store", path);
    auto slash = path.find('/', config().storeDir.size() + 1);
    if (slash == Path::npos)
        return {parseStorePath(path), ""};
    else
        return {parseStorePath(path.substr(0, slash)), (Path) path.substr(slash)};
}


Path Store::followLinksToStore(std::string_view _path) const
{
    Path path = absPath(std::string(_path));
    while (!isInStore(path)) {
        if (!isLink(path)) break;
        auto target = readLink(path);
        path = absPath(target, dirOf(path));
    }
    if (!isInStore(path))
        throw BadStorePath("path '%1%' is not in the Nix store", path);
    return path;
}


StorePath Store::followLinksToStorePath(std::string_view path) const
{
    return toStorePath(followLinksToStore(path)).first;
}


/* Store paths have the following form:

   <realized-path> = <store>/<h>-<name>

   where

   <store> = the location of the Nix store, usually /nix/store

   <name> = a human readable name for the path, typically obtained
     from the name attribute of the derivation, or the name of the
     source file from which the store path is created.  For derivation
     outputs other than the default "out" output, the string "-<id>"
     is suffixed to <name>.

   <h> = base-32 representation of the first 160 bits of a SHA-256
     hash of <s>; the hash part of the store name

   <s> = the string "<type>:sha256:<h2>:<store>:<name>";
     note that it includes the location of the store as well as the
     name to make sure that changes to either of those are reflected
     in the hash (e.g. you won't get /nix/store/<h>-name1 and
     /nix/store/<h>-name2 with equal hash parts).

   <type> = one of:
     "text:<r1>:<r2>:...<rN>"
       for plain text files written to the store using
       addTextToStore(); <r1> ... <rN> are the store paths referenced
       by this path, in the form described by <realized-path>
     "source:<r1>:<r2>:...:<rN>:self"
       for paths copied to the store using addToStore() when recursive
       = true and hashAlgo = "sha256". Just like in the text case, we
       can have the store paths referenced by the path.
       Additionally, we can have an optional :self label to denote self
       reference.
     "output:<id>"
       for either the outputs created by derivations, OR paths copied
       to the store using addToStore() with recursive != true or
       hashAlgo != "sha256" (in that case "source" is used; it's
       silly, but it's done that way for compatibility).  <id> is the
       name of the output (usually, "out").

   <h2> = base-16 representation of a SHA-256 hash of <s2>

   <s2> =
     if <type> = "text:...":
       the string written to the resulting store path
     if <type> = "source:...":
       the serialisation of the path from which this store path is
       copied, as returned by hashPath()
     if <type> = "output:<id>":
       for non-fixed derivation outputs:
         the derivation (see hashDerivationModulo() in
         primops.cc)
       for paths copied by addToStore() or produced by fixed-output
       derivations:
         the string "fixed:out:<rec><algo>:<hash>:", where
           <rec> = "r:" for recursive (path) hashes, or "" for flat
             (file) hashes
           <algo> = "md5", "sha1" or "sha256"
           <hash> = base-16 representation of the path or flat hash of
             the contents of the path (or expected contents of the
             path for fixed-output derivations)

   Note that since an output derivation has always type output, while
   something added by addToStore can have type output or source depending
   on the hash, this means that the same input can be hashed differently
   if added to the store via addToStore or via a derivation, in the sha256
   recursive case.

   It would have been nicer to handle fixed-output derivations under
   "source", e.g. have something like "source:<rec><algo>", but we're
   stuck with this for now...

   The main reason for this way of computing names is to prevent name
   collisions (for security).  For instance, it shouldn't be feasible
   to come up with a derivation whose output path collides with the
   path for a copied source.  The former would have a <s> starting with
   "output:out:", while the latter would have a <s> starting with
   "source:".
*/


StorePath Store::makeStorePath(std::string_view type,
    std::string_view hash, std::string_view name) const
{
    /* e.g., "source:sha256:1abc...:/nix/store:foo.tar.gz" */
    auto s = std::string(type) + ":" + std::string(hash)
        + ":" + config().storeDir + ":" + std::string(name);
    auto h = compressHash(hashString(HashType::SHA256, s), 20);
    return StorePath(h, name);
}


StorePath Store::makeStorePath(std::string_view type,
    const Hash & hash, std::string_view name) const
{
    return makeStorePath(type, hash.to_string(Base::Base16, true), name);
}


StorePath Store::makeOutputPath(std::string_view id,
    const Hash & hash, std::string_view name) const
{
    return makeStorePath("output:" + std::string { id }, hash, outputPathName(name, id));
}


/* Stuff the references (if any) into the type.  This is a bit
   hacky, but we can't put them in, say, <s2> (per the grammar above)
   since that would be ambiguous. */
static std::string makeType(
    const Store & store,
    std::string && type,
    const StoreReferences & references)
{
    for (auto & i : references.others) {
        type += ":";
        type += store.printStorePath(i);
    }
    if (references.self) type += ":self";
    return std::move(type);
}


StorePath Store::makeFixedOutputPath(std::string_view name, const FixedOutputInfo & info) const
{
    if (info.hash.type == HashType::SHA256 && info.method == FileIngestionMethod::Recursive) {
        return makeStorePath(makeType(*this, "source", info.references), info.hash, name);
    } else {
        if (!info.references.empty()) {
            throw Error("fixed output derivation '%s' is not allowed to refer to other store paths.\nYou may need to use the 'unsafeDiscardReferences' derivation attribute, see the manual for more details.",
                name);
        }
        return makeStorePath("output:out",
            hashString(HashType::SHA256,
                "fixed:out:"
                + makeFileIngestionPrefix(info.method)
                + info.hash.to_string(Base::Base16, true) + ":"),
            name);
    }
}


StorePath Store::makeTextPath(std::string_view name, const TextInfo & info) const
{
    assert(info.hash.type == HashType::SHA256);
    return makeStorePath(
        makeType(*this, "text", StoreReferences {
            .others = info.references,
            .self = false,
        }),
        info.hash,
        name);
}


StorePath Store::makeFixedOutputPathFromCA(std::string_view name, const ContentAddressWithReferences & ca) const
{
    // New template
    return std::visit(overloaded {
        [&](const TextInfo & ti) {
            return makeTextPath(name, ti);
        },
        [&](const FixedOutputInfo & foi) {
            return makeFixedOutputPath(name, foi);
        }
    }, ca.raw);
}


StorePath Store::computeStorePathForPathRecursive(std::string_view name,
    const PreparedDump & source) const
{
    FixedOutputInfo caInfo {
        .method = FileIngestionMethod::Recursive,
        .hash = hashPath(HashType::SHA256, source).first,
        .references = {},
    };
    return makeFixedOutputPath(name, caInfo);
}

StorePath Store::computeStorePathForPathFlat(std::string_view name, const Path & srcPath) const
{
    FixedOutputInfo caInfo {
        .method = FileIngestionMethod::Flat,
        .hash = hashFile(HashType::SHA256, srcPath),
        .references = {},
    };
    return makeFixedOutputPath(name, caInfo);
}


StorePath Store::computeStorePathForText(
    std::string_view name,
    std::string_view s,
    const StorePathSet & references) const
{
    return makeTextPath(name, TextInfo {
        .hash = hashString(HashType::SHA256, s),
        .references = references,
    });
}


kj::Promise<Result<StorePath>> Store::addToStoreRecursive(
    std::string_view name,
    const PreparedDump & _source,
    HashType hashAlgo,
    RepairFlag repair)
try {
    auto source = AsyncGeneratorInputStream{_source.dump()};
    co_return TRY_AWAIT(
        addToStoreFromDump(source, name, FileIngestionMethod::Recursive, hashAlgo, repair, {})
    );
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<StorePath>> Store::addToStoreFlat(
    std::string_view name,
    const Path & _srcPath,
    HashType hashAlgo,
    RepairFlag repair)
try {
    Path srcPath(absPath(_srcPath));
    auto source = AsyncGeneratorInputStream{readFileSource(srcPath)};
    co_return TRY_AWAIT(
        addToStoreFromDump(source, name, FileIngestionMethod::Flat, hashAlgo, repair, {})
    );
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> Store::addMultipleToStore(
    PathsSource & pathsToCopy,
    Activity & act,
    RepairFlag repair,
    CheckSigsFlag checkSigs)
try {
    std::atomic<size_t> nrDone{0};
    std::atomic<size_t> nrFailed{0};
    std::atomic<uint64_t> bytesExpected{0};
    std::atomic<uint64_t> nrRunning{0};

    std::map<StorePath, PathsSource::value_type *> infosMap;
    StorePathSet storePathsToAdd;
    for (auto & thingToAdd : pathsToCopy) {
        infosMap.insert_or_assign(thingToAdd.first.path, &thingToAdd);
        storePathsToAdd.insert(thingToAdd.first.path);
    }

    auto showProgress = [&]() {
        act.progress(nrDone, pathsToCopy.size(), nrRunning, nrFailed);
    };

    TRY_AWAIT(processGraphAsync<StorePath>(
        storePathsToAdd,

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        [&](const StorePath & path) -> kj::Promise<Result<StorePathSet>> {
            try {
                auto & [info, _] = *infosMap.at(path);

                if (TRY_AWAIT(isValidPath(info.path))) {
                    nrDone++;
                    showProgress();
                    co_return StorePathSet();
                }

                bytesExpected += info.narSize;
                act.setExpected(actCopyPath, bytesExpected);

                co_return info.references;
            } catch (...) {
                co_return result::current_exception();
            }
        },

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        [&](const StorePath & path) -> kj::Promise<Result<void>> {
            try {
                checkInterrupt();

                auto & [info_, source_] = *infosMap.at(path);
                auto info = info_;
                info.ultimate = false;

                /* Make sure that the Source object is destroyed when
                we're done. In particular, a coroutine object must
                be destroyed to ensure that the destructors in its
                state are run; this includes
                LegacySSHStore::narFromPath()'s connection lock. */
                auto source = std::move(source_);

                if (!TRY_AWAIT(isValidPath(info.path))) {
                    MaintainCount<decltype(nrRunning)> mc(nrRunning);
                    showProgress();
                    try {
                        TRY_AWAIT(addToStore(info, *TRY_AWAIT(source()), repair, checkSigs));
                    } catch (Error & e) {
                        nrFailed++;
                        if (!settings.keepGoing)
                            throw e;
                        printMsg(lvlError, "could not copy %s: %s", printStorePath(path), e.what());
                        showProgress();
                        co_return result::success();
                    }
                }

                nrDone++;
                showProgress();
                co_return result::success();
            } catch (...) {
                co_return result::current_exception();
            }
        }));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

/*
The aim of this function is to compute in one pass the correct ValidPathInfo for
the files that we are trying to add to the store. To accomplish that in one
pass, given the different kind of inputs that we can take (normal nar archives,
nar archives with non SHA-256 hashes, and flat files), we use a passthru generator
to always pass data to narHashSink (to compute the NAR hash) and have our handlers
for various ingestion types and hash algorithms pass data to hash sinks as needed.
*/
kj::Promise<Result<ValidPathInfo>> Store::addToStoreSlow(std::string_view name, const Path & srcPath,
    FileIngestionMethod method, HashType hashAlgo,
    std::optional<Hash> expectedCAHash)
try {
    HashSink narHashSink { HashType::SHA256 };
    HashSink caHashSink { hashAlgo };

    GeneratorSource nar{[](auto nar, auto & narHashSink) -> WireFormatGenerator {
        while (auto block = nar.next()) {
            narHashSink({block->data(), block->size()});
            co_yield *block;
        }
    }(dumpPath(srcPath), narHashSink)};

    // information always flows from nar to hashSinks. we only check that the
    // nar is correct, and during flat ingestion contains only a single file.
    if (method == FileIngestionMethod::Flat) {
        auto parsed = nar::parse(nar);
        auto entry = parsed.next();
        // if the path was inaccessible we'd get an error from dumpPath
        assert(entry.has_value());
        std::visit(
            overloaded{
                [&](nar::File & f) {
                    while (auto block = f.contents.next()) {
                        caHashSink({block->data(), block->size()});
                    }
                },
                [](nar::Symlink &) { throw Error("cannot import symlink using flat ingestion"); },
                [](nar::Directory &) {
                    throw Error("cannot import directory using flat ingestion");
                },
            },
            *entry
        );
        // drain internal state through the tee as well
        while (parsed.next()) {}
    } else if (hashAlgo != HashType::SHA256) {
        nar.drainInto(caHashSink);
    } else {
        NullSink null;
        nar.drainInto(null);
    }

    /* We extract the result of the computation from the sink by calling
       finish. */
    auto [narHash, narSize] = narHashSink.finish();

    auto hash = method == FileIngestionMethod::Recursive && hashAlgo == HashType::SHA256
        ? narHash
        : caHashSink.finish().first;

    if (expectedCAHash && expectedCAHash != hash)
        throw Error("hash mismatch for '%s'", srcPath);

    ValidPathInfo info {
        *this,
        name,
        FixedOutputInfo {
            .method = method,
            .hash = hash,
            .references = {},
        },
        narHash,
    };
    info.narSize = narSize;

    if (!TRY_AWAIT(isValidPath(info.path))) {
        auto source = AsyncGeneratorInputStream{dumpPath(srcPath)};
        TRY_AWAIT(addToStore(info, source));
    }

    co_return info;
} catch (...) {
    co_return result::current_exception();
}

StringSet StoreConfig::getDefaultSystemFeatures()
{
    return settings.systemFeatures.get();
}

Store::Store(const StoreConfig & config) : state({(size_t) config.pathInfoCacheSize})
{
    assertLibStoreInitialized();
}


std::string Store::getUri()
{
    return "";
}

bool Store::PathInfoCacheValue::isKnownNow()
{
    std::chrono::duration ttl = didExist()
        ? std::chrono::seconds(settings.ttlPositiveNarInfoCache)
        : std::chrono::seconds(settings.ttlNegativeNarInfoCache);

    return std::chrono::steady_clock::now() < time_point + ttl;
}

kj::Promise<Result<std::map<std::string, StorePath>>>
Store::queryStaticDerivationOutputMap(const StorePath & path)
try {
    std::map<std::string, StorePath> outputs;
    auto drv = TRY_AWAIT(readInvalidDerivation(path));
    for (auto & [outputName, output] : drv.outputsAndPaths(*this)) {
        outputs.emplace(outputName, output.second);
    }
    co_return outputs;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::map<std::string, StorePath>>>
Store::queryDerivationOutputMap(const StorePath & path, Store * evalStore_)
try {
    auto & evalStore = evalStore_ ? *evalStore_ : *this;

    co_return TRY_AWAIT(evalStore.queryStaticDerivationOutputMap(path));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<StorePathSet>> Store::queryDerivationOutputs(const StorePath & path)
try {
    auto outputMap = TRY_AWAIT(this->queryDerivationOutputMap(path));
    StorePathSet outputPaths;
    for (auto & i: outputMap) {
        outputPaths.emplace(std::move(i.second));
    }
    co_return outputPaths;
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> Store::querySubstitutablePathInfos(const StorePathCAMap & paths, SubstitutablePathInfos & infos)
try {
    if (!settings.useSubstitutes) co_return result::success();
    for (auto & sub : TRY_AWAIT(getDefaultSubstituters())) {
        for (auto & path : paths) {
            if (infos.count(path.first))
                // Choose first succeeding substituter.
                continue;

            auto subPath(path.first);

            // Recompute store path so that we can use a different store root.
            if (path.second) {
                subPath = makeFixedOutputPathFromCA(
                    path.first.name(),
                    ContentAddressWithReferences::withoutRefs(*path.second));
                if (sub->config().storeDir == config().storeDir)
                    assert(subPath == path.first);
                if (subPath != path.first)
                    debug("replaced path '%s' with '%s' for substituter '%s'", printStorePath(path.first), sub->printStorePath(subPath), sub->getUri());
            } else if (sub->config().storeDir != config().storeDir) continue;

            debug("checking substituter '%s' for path '%s'", sub->getUri(), sub->printStorePath(subPath));
            try {
                auto info = TRY_AWAIT(sub->queryPathInfo(subPath));

                if (sub->config().storeDir != config().storeDir
                    && !(info->isContentAddressed(*sub) && info->references.empty()))
                {
                    continue;
                }

                auto narInfo = std::dynamic_pointer_cast<const NarInfo>(
                    std::shared_ptr<const ValidPathInfo>(info));
                infos.insert_or_assign(path.first, SubstitutablePathInfo{
                    .deriver = info->deriver,
                    .references = info->references,
                    .downloadSize = narInfo ? narInfo->fileSize : 0,
                    .narSize = info->narSize,
                });
            } catch (InvalidPath &) {
            } catch (SubstituterDisabled &) {
            } catch (Error & e) {
                if (settings.tryFallback)
                    logError(e.info());
                else
                    throw;
            }
        }
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<bool>> Store::isValidPath(const StorePath & storePath, const Activity * context)
try {
    {
        auto state_(co_await state.lock());
        auto res = state_->pathInfoCache.get(std::string(storePath.to_string()));
        if (res && res->isKnownNow()) {
            stats.narInfoReadAverted++;
            co_return res->didExist();
        }
    }

    if (diskCache) {
        auto res = diskCache->lookupNarInfo(getUri(), std::string(storePath.hashPart()));
        if (res.first != NarInfoDiskCache::oUnknown) {
            stats.narInfoReadAverted++;
            auto state_(co_await state.lock());
            state_->pathInfoCache.upsert(std::string(storePath.to_string()),
                res.first == NarInfoDiskCache::oInvalid ? PathInfoCacheValue{} : PathInfoCacheValue { .value = res.second });
            co_return res.first == NarInfoDiskCache::oValid;
        }
    }

    bool valid = TRY_AWAIT(isValidPathUncached(storePath, context));

    if (diskCache && !valid)
        // FIXME: handle valid = true case.
        diskCache->upsertNarInfo(getUri(), std::string(storePath.hashPart()), 0);

    co_return valid;
} catch (...) {
    co_return result::current_exception();
}

/* Default implementation for stores that only implement
   queryPathInfoUncached(). */
kj::Promise<Result<bool>>
Store::isValidPathUncached(const StorePath & path, const Activity * context)
try {
    TRY_AWAIT(queryPathInfo(path, context));
    co_return true;
} catch (InvalidPath &) {
    co_return false;
} catch (...) {
    co_return result::current_exception();
}


static void ensureGoodStorePath(Store * store, const StorePath & expected, const StorePath & actual)
{
    if (expected.hashPart() != actual.hashPart()) {
        throw Error(
            "the queried store path hash '%s' did not match expected '%s' while querying the store path '%s'",
            expected.hashPart(), actual.hashPart(), store->printStorePath(expected)
        );
    } else if (expected.name() != Store::MissingName && expected.name() != actual.name()) {
        throw Error(
            "the queried store path name '%s' did not match expected '%s' while querying the store path '%s'",
            expected.name(), actual.name(), store->printStorePath(expected)
        );
    }
}

kj::Promise<Result<ref<const ValidPathInfo>>>
Store::queryPathInfo(const StorePath & storePath, const Activity * context)
try {
    auto hashPart = std::string(storePath.hashPart());

    {
        auto res = (co_await state.lock())->pathInfoCache.get(std::string(storePath.to_string()));
        if (res && res->isKnownNow()) {
            stats.narInfoReadAverted++;
            if (!res->didExist())
                throw InvalidPath(
                    "path '%s' does not exist in the store", toRealPath(printStorePath(storePath))
                );
            co_return ref<const ValidPathInfo>::unsafeFromPtr(res->value);
        }
    }

    if (diskCache) {
        auto res = diskCache->lookupNarInfo(getUri(), hashPart);
        if (res.first != NarInfoDiskCache::oUnknown) {
            stats.narInfoReadAverted++;
            {
                auto state_(co_await state.lock());
                state_->pathInfoCache.upsert(std::string(storePath.to_string()),
                    res.first == NarInfoDiskCache::oInvalid ? PathInfoCacheValue{} : PathInfoCacheValue{ .value = res.second });
                if (res.first == NarInfoDiskCache::oInvalid)
                    throw InvalidPath(
                        "path '%s' does not exist in the store",
                        toRealPath(printStorePath(storePath))
                    );
            }
            co_return ref<const ValidPathInfo>::unsafeFromPtr(res.second);
        }
    }

    auto info = TRY_AWAIT(queryPathInfoUncached(storePath, context));
    if (info) {
        // first, before we cache anything, check that the store gave us valid data.
        ensureGoodStorePath(this, storePath, info->path);
    }

    if (diskCache) {
        diskCache->upsertNarInfo(getUri(), hashPart, info);
    }

    {
        auto state_(co_await state.lock());
        state_->pathInfoCache.upsert(std::string(storePath.to_string()), PathInfoCacheValue { .value = info });
    }

    if (!info) {
        stats.narInfoMissing++;
        throw InvalidPath(
            "path '%s' does not exist in the store", toRealPath(printStorePath(storePath))
        );
    }

    co_return ref<const ValidPathInfo>::unsafeFromPtr(info);
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> Store::substitutePaths(const StorePathSet & paths)
try {
    std::vector<DerivedPath> paths2;
    for (auto & path : paths)
        if (!path.isDerivation())
            paths2.emplace_back(DerivedPath::Opaque{path});
    uint64_t downloadSize, narSize;
    StorePathSet willBuild, willSubstitute, unknown;
    TRY_AWAIT(queryMissing(paths2,
        willBuild, willSubstitute, unknown, downloadSize, narSize));

    if (!willSubstitute.empty())
        try {
            std::vector<DerivedPath> subs;
            for (auto & p : willSubstitute) subs.emplace_back(DerivedPath::Opaque{p});
            TRY_AWAIT(buildPaths(subs));
        } catch (Error & e) {
            logWarning(e.info());
        }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<StorePathSet>>
Store::queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute)
try {
    StorePathSet valid;

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    auto doQuery = [&](const StorePath & path) -> kj::Promise<Result<void>> {
        try {
            TRY_AWAIT(queryPathInfo(path));
            valid.insert(path);
        } catch (InvalidPath &) {
        } catch (...) {
            co_return result::current_exception();
        }
        co_return result::success();
    };

    TRY_AWAIT(asyncSpread(paths, doQuery));

    co_return valid;
} catch (...) {
    co_return result::current_exception();
}


/* Return a string accepted by decodeValidPathInfo() that
   registers the specified paths as valid.  Note: it's the
   responsibility of the caller to provide a closure. */
kj::Promise<Result<std::string>> Store::makeValidityRegistration(const StorePathSet & paths,
    bool showDerivers, bool showHash)
try {
    std::string s = "";

    for (auto & i : paths) {
        s += printStorePath(i) + "\n";

        auto info = TRY_AWAIT(queryPathInfo(i));

        if (showHash) {
            s += info->narHash.to_string(Base::Base16, false) + "\n";
            s += fmt("%1%\n", info->narSize);
        }

        auto deriver = showDerivers && info->deriver ? printStorePath(*info->deriver) : "";
        s += deriver + "\n";

        s += fmt("%1%\n", info->references.size());

        for (auto & j : info->references)
            s += printStorePath(j) + "\n";
    }

    co_return s;
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<StorePathSet>>
Store::exportReferences(const StorePathSet & storePaths, const StorePathSet & inputPaths)
try {
    StorePathSet paths;

    for (auto & storePath : storePaths) {
        if (!inputPaths.count(storePath))
            throw BuildError("cannot export references of path '%s' because it is not in the input closure of the derivation", printStorePath(storePath));

        TRY_AWAIT(computeFSClosure({storePath}, paths));
    }

    /* If there are derivations in the graph, then include their
       outputs as well.  This is useful if you want to do things
       like passing all build-time dependencies of some path to a
       derivation that builds a NixOS DVD image. */
    auto paths2 = paths;

    for (auto & j : paths2) {
        if (j.isDerivation()) {
            Derivation drv = TRY_AWAIT(derivationFromPath(j));
            for (auto & k : drv.outputsAndPaths(*this)) {
                TRY_AWAIT(computeFSClosure(k.second.second, paths));
            }
        }
    }

    co_return paths;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<JSON>> Store::pathInfoToJSON(const StorePathSet & storePaths,
    bool includeImpureInfo, bool showClosureSize,
    Base hashBase,
    AllowInvalidFlag allowInvalid)
try {
    JSON::array_t jsonList = JSON::array();

    for (auto & storePath : storePaths) {
        auto& jsonPath = jsonList.emplace_back(JSON::object());

        try {
            auto info = TRY_AWAIT(queryPathInfo(storePath));

            jsonPath["path"] = printStorePath(info->path);
            jsonPath["valid"] = true;
            jsonPath["narHash"] = info->narHash.to_string(hashBase, true);
            jsonPath["narSize"] = info->narSize;

            {
                auto& jsonRefs = (jsonPath["references"] = JSON::array());
                for (auto & ref : info->references)
                    jsonRefs.emplace_back(printStorePath(ref));
            }

            if (info->ca)
                jsonPath["ca"] = renderContentAddress(info->ca);

            std::pair<uint64_t, uint64_t> closureSizes;

            if (showClosureSize) {
                closureSizes = TRY_AWAIT(getClosureSize(info->path));
                jsonPath["closureSize"] = closureSizes.first;
            }

            if (includeImpureInfo) {

                if (info->deriver)
                    jsonPath["deriver"] = printStorePath(*info->deriver);

                if (info->registrationTime)
                    jsonPath["registrationTime"] = info->registrationTime;

                if (info->ultimate)
                    jsonPath["ultimate"] = info->ultimate;

                if (!info->sigs.empty()) {
                    for (auto & sig : info->sigs)
                        jsonPath["signatures"].push_back(sig);
                }

                auto narInfo = std::dynamic_pointer_cast<const NarInfo>(
                    std::shared_ptr<const ValidPathInfo>(info));

                if (narInfo) {
                    if (!narInfo->url.empty())
                        jsonPath["url"] = narInfo->url;
                    if (narInfo->fileHash)
                        jsonPath["downloadHash"] = narInfo->fileHash->to_string(hashBase, true);
                    if (narInfo->fileSize)
                        jsonPath["downloadSize"] = narInfo->fileSize;
                    if (showClosureSize)
                        jsonPath["closureDownloadSize"] = closureSizes.second;
                }
            }

        } catch (InvalidPath &) {
            jsonPath["path"] = printStorePath(storePath);
            jsonPath["valid"] = false;
        }
    }
    co_return jsonList;
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<std::pair<uint64_t, uint64_t>>>
Store::getClosureSize(const StorePath & storePath)
try {
    uint64_t totalNarSize = 0, totalDownloadSize = 0;
    StorePathSet closure;
    TRY_AWAIT(computeFSClosure(storePath, closure, false, false));
    for (auto & p : closure) {
        auto info = TRY_AWAIT(queryPathInfo(p));
        totalNarSize += info->narSize;
        auto narInfo = std::dynamic_pointer_cast<const NarInfo>(
            std::shared_ptr<const ValidPathInfo>(info));
        if (narInfo)
            totalDownloadSize += narInfo->fileSize;
    }
    co_return {totalNarSize, totalDownloadSize};
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<Store::Stats<>>> Store::getStats()
try {
    {
        auto state_(co_await state.lock());
        stats.pathInfoCacheSize = state_->pathInfoCache.size();
    }
    co_return {
        stats.narInfoRead,
        stats.narInfoReadAverted,
        stats.narInfoMissing,
        stats.narInfoWrite,
        stats.pathInfoCacheSize,
        stats.narRead,
        stats.narReadBytes,
        stats.narReadCompressedBytes,
        stats.narWrite,
        stats.narWriteAverted,
        stats.narWriteBytes,
        stats.narWriteCompressedBytes,
        stats.narWriteCompressionTimeMs,
    };
} catch (...) {
    co_return result::current_exception();
}


static std::string makeCopyPathMessage(
    std::string_view srcUri,
    std::string_view dstUri,
    std::string_view storePath)
{
    return srcUri == "local" || srcUri == "daemon"
        ? fmt("copying path '%s' to '%s'", storePath, dstUri)
        : dstUri == "local" || dstUri == "daemon"
        ? fmt("copying path '%s' from '%s'", storePath, srcUri)
        : fmt("copying path '%s' from '%s' to '%s'", storePath, srcUri, dstUri);
}


namespace {
struct CopyPathStream : AsyncInputStream
{
    Activity & act;
    uint64_t copied = 0, expected;
    box_ptr<AsyncInputStream> inner;

    CopyPathStream(Activity & act, uint64_t expected, box_ptr<AsyncInputStream> inner)
        : act(act)
        , expected(expected)
        , inner(std::move(inner))
    {
    }

    kj::Promise<Result<std::optional<size_t>>> read(void * data, size_t len) override
    try {
        auto result = TRY_AWAIT(inner->read(data, len));

        // do not log progress on every call. nar copies cause a lot of small
        // reads, letting each read report the current copy progress causes a
        // huge amount of overhead (20x or more) in log traffic. reporting at
        // 64 kiB intervals is probably enough, being about 1000 dir entries.
        constexpr size_t CHUNK = 65536;
        const auto doLog = !result || copied / CHUNK < (copied + *result) / CHUNK || *result < len;
        if (result) {
            copied += *result;
        }
        if (doLog) {
            act.progress(copied, expected);
        }
        co_return result;
    } catch (...) {
        co_return result::current_exception();
    }
};
}

kj::Promise<Result<void>> copyStorePath(
    Store & srcStore,
    Store & dstStore,
    const StorePath & storePath,
    RepairFlag repair,
    CheckSigsFlag checkSigs,
    const Activity * context
)
try {
    /* Bail out early (before starting a download from srcStore) if
       dstStore already has this path. */
    if (!repair && TRY_AWAIT(dstStore.isValidPath(storePath, context))) {
        co_return result::success();
    }

    auto srcUri = srcStore.getUri();
    auto dstUri = dstStore.getUri();
    auto storePathS = srcStore.printStorePath(storePath);
    Activity act(
        *logger,
        lvlInfo,
        actCopyPath,
        makeCopyPathMessage(srcUri, dstUri, storePathS),
        {storePathS, srcUri, dstUri},
        context ? context->id : 0
    );

    auto info = TRY_AWAIT(srcStore.queryPathInfo(storePath, &act));

    // recompute store path on the chance dstStore does it differently
    if (info->ca && info->references.empty()) {
        auto info2 = make_ref<ValidPathInfo>(*info);
        info2->path = dstStore.makeFixedOutputPathFromCA(
            info->path.name(),
            info->contentAddressWithReferences().value());
        if (dstStore.config().storeDir == srcStore.config().storeDir)
            assert(info->path == info2->path);
        info = info2;
    }

    if (info->ultimate) {
        auto info2 = make_ref<ValidPathInfo>(*info);
        info2->ultimate = false;
        info = info2;
    }

    CopyPathStream source{act, info->narSize, TRY_AWAIT(srcStore.narFromPath(storePath, &act))};
    TRY_AWAIT(dstStore.addToStore(*info, source, repair, checkSigs, &act));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<std::map<StorePath, StorePath>>> copyPaths(
    Store & srcStore,
    Store & dstStore,
    const RealisedPath::Set & paths,
    RepairFlag repair,
    CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
try {
    StorePathSet storePaths;
    for (auto & path : paths) {
        storePaths.insert(path.path());
        if (auto _ = std::get_if<Realisation>(&path.raw)) {
            throw UnimplementedError("ca derivations are not supported");
        }
    }
    co_return TRY_AWAIT(copyPaths(srcStore, dstStore, storePaths, repair, checkSigs, substitute));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::map<StorePath, StorePath>>> copyPaths(
    Store & srcStore,
    Store & dstStore,
    const StorePathSet & storePaths,
    RepairFlag repair,
    CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
try {
    auto valid = TRY_AWAIT(dstStore.queryValidPaths(storePaths, substitute));

    StorePathSet missing;
    for (auto & path : storePaths)
        if (!valid.count(path)) missing.insert(path);

    Activity act(*logger, lvlInfo, actCopyPaths, fmt("copying %d paths", missing.size()));

    // In the general case, `addMultipleToStore` requires a sorted list of
    // store paths to add, so sort them right now
    auto sortedMissing = TRY_AWAIT(srcStore.topoSortPaths(missing));
    std::reverse(sortedMissing.begin(), sortedMissing.end());

    std::map<StorePath, StorePath> pathsMap;
    for (auto & path : storePaths)
        pathsMap.insert_or_assign(path, path);

    Store::PathsSource pathsToCopy;

    auto computeStorePathForDst = [&](const ValidPathInfo & currentPathInfo) -> StorePath {
        auto storePathForSrc = currentPathInfo.path;
        auto storePathForDst = storePathForSrc;
        if (currentPathInfo.ca && currentPathInfo.references.empty()) {
            storePathForDst = dstStore.makeFixedOutputPathFromCA(
                currentPathInfo.path.name(),
                currentPathInfo.contentAddressWithReferences().value());
            if (dstStore.config().storeDir == srcStore.config().storeDir)
                assert(storePathForDst == storePathForSrc);
            if (storePathForDst != storePathForSrc)
                debug("replaced path '%s' to '%s' for substituter '%s'",
                        srcStore.printStorePath(storePathForSrc),
                        dstStore.printStorePath(storePathForDst),
                        dstStore.getUri());
        }
        return storePathForDst;
    };

    for (auto & missingPath : sortedMissing) {
        auto info = TRY_AWAIT(srcStore.queryPathInfo(missingPath));

        auto storePathForDst = computeStorePathForDst(*info);
        pathsMap.insert_or_assign(missingPath, storePathForDst);

        ValidPathInfo infoForDst = *info;
        infoForDst.path = storePathForDst;

        struct SinglePathStream : CopyPathStream
        {
            std::shared_ptr<Activity> act;

            SinglePathStream(
                const std::shared_ptr<Activity> & act,
                size_t expected,
                box_ptr<AsyncInputStream> inner
            )
                : CopyPathStream(*act, expected, std::move(inner))
                , act(act)
            {
            }
        };

        auto source = [](auto & srcStore, auto & dstStore, auto missingPath, auto info
                      ) -> kj::Promise<Result<box_ptr<AsyncInputStream>>> {
            try {
                // We can reasonably assume that the copy will happen whenever we
                // read the path, so log something about that at that point
                auto srcUri = srcStore.getUri();
                auto dstUri = dstStore.getUri();
                auto storePathS = srcStore.printStorePath(missingPath);
                auto act = std::make_shared<Activity>(
                    *logger,
                    lvlInfo,
                    actCopyPath,
                    makeCopyPathMessage(srcUri, dstUri, storePathS),
                    Logger::Fields{storePathS, srcUri, dstUri}
                );

                co_return make_box_ptr<SinglePathStream>(
                    act, info->narSize, TRY_AWAIT(srcStore.narFromPath(missingPath, act.get()))
                );
            } catch (...) {
                co_return result::current_exception();
            }
        };
        pathsToCopy.push_back(std::pair{
            infoForDst, std::bind(source, std::ref(srcStore), std::ref(dstStore), missingPath, info)
        });
    }

    TRY_AWAIT(dstStore.addMultipleToStore(pathsToCopy, act, repair, checkSigs));

    co_return pathsMap;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> copyClosure(
    Store & srcStore,
    Store & dstStore,
    const RealisedPath::Set & paths,
    RepairFlag repair,
    CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
try {
    if (&srcStore == &dstStore) co_return result::success();

    RealisedPath::Set closure;
    TRY_AWAIT(RealisedPath::closure(srcStore, paths, closure));

    TRY_AWAIT(copyPaths(srcStore, dstStore, closure, repair, checkSigs, substitute));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> copyClosure(
    Store & srcStore,
    Store & dstStore,
    const StorePathSet & storePaths,
    RepairFlag repair,
    CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
try {
    if (&srcStore == &dstStore) co_return result::success();

    StorePathSet closure;
    TRY_AWAIT(srcStore.computeFSClosure(storePaths, closure));
    TRY_AWAIT(copyPaths(srcStore, dstStore, closure, repair, checkSigs, substitute));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

std::optional<ValidPathInfo> decodeValidPathInfo(const Store & store, std::istream & str, std::optional<HashResult> hashGiven)
{
    std::string path;
    getline(str, path);
    if (str.eof()) { return {}; }
    if (!hashGiven) {
        std::string s;
        getline(str, s);
        auto narHash = Hash::parseAny(s, HashType::SHA256);
        getline(str, s);
        auto narSize = string2Int<uint64_t>(s);
        if (!narSize) throw Error("number expected");
        hashGiven = { narHash, *narSize };
    }
    ValidPathInfo info(store.parseStorePath(path), hashGiven->first);
    info.narSize = hashGiven->second;
    std::string deriver;
    getline(str, deriver);
    if (deriver != "") info.deriver = store.parseStorePath(deriver);
    std::string s;
    getline(str, s);
    auto n = string2Int<int>(s);
    if (!n) throw Error("number expected");
    while ((*n)--) {
        getline(str, s);
        info.references.insert(store.parseStorePath(s));
    }
    if (!str || str.eof()) throw Error("missing input");
    return std::optional<ValidPathInfo>(std::move(info));
}


std::string Store::showPaths(const StorePathSet & paths)
{
    std::string s;
    for (auto & i : paths) {
        if (s.size() != 0) s += ", ";
        s += "'" + printStorePath(i) + "'";
    }
    return s;
}


std::string showPaths(const PathSet & paths)
{
    return concatStringsSep(", ", quoteStrings(paths));
}


kj::Promise<Result<Derivation>> Store::derivationFromPath(const StorePath & drvPath)
try {
    TRY_AWAIT(ensurePath(drvPath));
    co_return TRY_AWAIT(readDerivation(drvPath));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<Derivation>>
readDerivationCommon(Store& store, const StorePath& drvPath, bool requireValidPath)
try {
    auto accessor = store.getFSAccessor();
    try {
        co_return parseDerivation(
            store,
            TRY_AWAIT(accessor->readFile(store.printStorePath(drvPath), requireValidPath)),
            Derivation::nameFromPath(drvPath)
        );
    } catch (FormatError & e) {
        throw Error("error parsing derivation '%s': %s", store.printStorePath(drvPath), e.msg());
    }
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<StorePath>>> Store::getBuildDerivationPath(const StorePath & path)
try {

    if (!path.isDerivation()) {
        try {
            auto info = TRY_AWAIT(queryPathInfo(path));
            co_return info->deriver;
        } catch (InvalidPath &) {
            co_return std::nullopt;
        }
    }

    co_return path;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<Derivation>> Store::readDerivation(const StorePath & drvPath)
{ return readDerivationCommon(*this, drvPath, true); }

kj::Promise<Result<Derivation>> Store::readInvalidDerivation(const StorePath & drvPath)
{ return readDerivationCommon(*this, drvPath, false); }

}


#include "lix/libstore/local-store.hh"
#include "lix/libstore/uds-remote-store.hh"


namespace nix {

/* Split URI into protocol+hierarchy part and its parameter set. */
std::pair<std::string, StoreConfig::Params> splitUriAndParams(const std::string & uri_)
{
    auto uri(uri_);
    StoreConfig::Params params;
    auto q = uri.find('?');
    if (q != std::string::npos) {
        params = decodeQuery(uri.substr(q + 1));
        uri = uri_.substr(0, q);
    }
    return {uri, params};
}

static bool isNonUriPath(const std::string & spec)
{
    return
        // is not a URL
        spec.find("://") == std::string::npos
        // Has at least one path separator, and so isn't a single word that
        // might be special like "auto"
        && spec.find("/") != std::string::npos;
}

static std::optional<ref<Store>>
openFromNonUri(const std::string & uri, const StoreConfig::Params & params, AllowDaemon allowDaemon)
{
    if (uri == "" || uri == "auto") {
        auto stateDir = getOr(params, "state", settings.nixStateDir);
        if (allowDaemon == AllowDaemon::Allow && pathExists(settings.nixDaemonSocketFile)) {
            return make_ref<UDSRemoteStore>(params);
        } else if (access(stateDir.c_str(), R_OK | W_OK) == 0) {
            return LocalStore::makeLocalStore(params);
        }
#if __linux__
        else if (!pathExists(stateDir) && params.empty() && getuid() != 0
                 && !getEnv("NIX_STORE_DIR").has_value() && !getEnv("NIX_STATE_DIR").has_value())
        {
            /* If /nix doesn't exist, there is no daemon socket, and
               we're not root, then automatically set up a chroot
               store in ~/.local/share/nix/root. */
            auto chrootStore = getDataDir() + "/nix/root";
            if (!pathExists(chrootStore)) {
                try {
                    createDirs(chrootStore);
                } catch (Error & e) {
                    return LocalStore::makeLocalStore(params);
                }
                printTaggedWarning(
                    "'%s' does not exist, so Lix will use '%s' as a chroot store",
                    stateDir,
                    chrootStore
                );
            } else
                debug("'%s' does not exist, so Lix will use '%s' as a chroot store", stateDir, chrootStore);
            StoreConfig::Params chrootStoreParams;
            chrootStoreParams["root"] = chrootStore;
            // FIXME? this ignores *all* store parameters passed to this function?
            return LocalStore::makeLocalStore(chrootStoreParams);
        }
        #endif
        else
            return LocalStore::makeLocalStore(params);
    } else if (uri == "daemon") {
        if (allowDaemon == AllowDaemon::Disallow) {
            throw Error("tried to open a daemon store in a context that doesn't support this");
        }
        return make_ref<UDSRemoteStore>(params);
    } else if (uri == "local") {
        return LocalStore::makeLocalStore(params);
    } else if (isNonUriPath(uri)) {
        StoreConfig::Params params2 = params;
        params2["root"] = absPath(uri);
        return LocalStore::makeLocalStore(params2);
    } else {
        return std::nullopt;
    }
}

// The `parseURL` function supports both IPv6 URIs as defined in
// RFC2732, but also pure addresses. The latter one is needed here to
// connect to a remote store via SSH (it's possible to do e.g. `ssh root@::1`).
//
// This function now ensures that a usable connection string is available:
// * If the store to be opened is not an SSH store, nothing will be done.
// * If the URL looks like `root@[::1]` (which is allowed by the URL parser and probably
//   needed to pass further flags), it
//   will be transformed into `root@::1` for SSH (same for `[::1]` -> `::1`).
// * If the URL looks like `root@::1` it will be left as-is.
// * In any other case, the string will be left as-is.
static std::string extractConnStr(const std::string &proto, const std::string &connStr)
{
    if (proto.rfind("ssh") != std::string::npos) {
        std::smatch result;
        std::regex v6AddrRegex = regex::parse("^((.*)@)?\\[(.*)\\]$");

        if (std::regex_match(connStr, result, v6AddrRegex)) {
            if (result[1].matched) {
                return result.str(1) + result.str(3);
            }
            return result.str(3);
        }
    }

    return connStr;
}

kj::Promise<Result<ref<Store>>>
openStore(const std::string & uri_, const StoreConfig::Params & extraParams, AllowDaemon allowDaemon)
try {
    auto params = extraParams;
    try {
        auto parsedUri = parseURL(uri_);
        params.insert(parsedUri.query.begin(), parsedUri.query.end());

        auto baseURI = extractConnStr(
            parsedUri.scheme,
            parsedUri.authority.value_or("") + parsedUri.path
        );

        for (auto implem : *StoreImplementations::registered) {
            if (implem.uriSchemes.count(parsedUri.scheme)) {
                auto store = implem.create(parsedUri.scheme, baseURI, params);
                if (store) {
                    experimentalFeatureSettings.require((*store)->config().experimentalFeature());
                    TRY_AWAIT((*store)->init());
                    (*store)->config().warnUnknownSettings();
                    co_return *store;
                }
            }
        }
    }
    catch (BadURL &) {
        auto [uri, uriParams] = splitUriAndParams(uri_);
        params.insert(uriParams.begin(), uriParams.end());

        if (auto store = openFromNonUri(uri, params, allowDaemon)) {
            (*store)->config().warnUnknownSettings();
            co_return *store;
        }
    }

    throw Error("don't know how to open Nix store '%s'", uri_);
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::list<ref<Store>>>> getDefaultSubstituters()
try {
    static Sync<std::optional<std::list<ref<Store>>>, AsyncMutex> stores;

    auto lk = co_await stores.lock();

    if (!lk->has_value()) {
        StringSet done;

        lk->emplace();
        for (auto uri : settings.substituters.get()) {
            if (!done.insert(uri).second) continue;
            try {
                (*lk)->push_back(TRY_AWAIT(openStore(uri)));
            } catch (Error & e) {
                logWarning(e.info());
            }
        }

        (*lk)->sort([](ref<Store> & a, ref<Store> & b) {
            return a->config().priority < b->config().priority;
        });
    }

    co_return **lk;
} catch (...) {
    co_return result::current_exception();
}

std::vector<StoreFactory> * StoreImplementations::registered = 0;

}
