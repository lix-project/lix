#include "lix/libstore/fs-accessor.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/nar-info-disk-cache.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/thread-pool.hh"
#include "lix/libutil/url.hh"
#include "lix/libutil/archive.hh"
#include "lix/libstore/uds-remote-store.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/strings.hh"
// FIXME this should not be here, see TODO below on
// `addMultipleToStore`.
#include "lix/libstore/worker-protocol.hh"
#include "lix/libutil/users.hh"

#include <mutex>
#include <nlohmann/json.hpp>
#include <regex>
#include <shared_mutex>

using json = nlohmann::json;

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


std::pair<StorePath, Hash> Store::computeStorePathForPath(std::string_view name,
    const Path & srcPath, FileIngestionMethod method, HashType hashAlgo, PathFilter & filter) const
{
    Hash h = method == FileIngestionMethod::Recursive
        ? hashPath(hashAlgo, srcPath, filter).first
        : hashFile(hashAlgo, srcPath);
    FixedOutputInfo caInfo {
        .method = method,
        .hash = h,
        .references = {},
    };
    return std::make_pair(makeFixedOutputPath(name, caInfo), h);
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


StorePath Store::addToStore(
    std::string_view name,
    const Path & _srcPath,
    FileIngestionMethod method,
    HashType hashAlgo,
    PathFilter & filter,
    RepairFlag repair,
    const StorePathSet & references)
{
    Path srcPath(absPath(_srcPath));
    auto source = GeneratorSource{
        method == FileIngestionMethod::Recursive ? dumpPath(srcPath, filter).decay()
                                                 : readFileSource(srcPath)
    };
    return addToStoreFromDump(source, name, method, hashAlgo, repair, references);
}

void Store::addMultipleToStore(
    PathsSource & pathsToCopy,
    Activity & act,
    RepairFlag repair,
    CheckSigsFlag checkSigs)
{
    std::atomic<size_t> nrDone{0};
    std::atomic<size_t> nrFailed{0};
    std::atomic<uint64_t> bytesExpected{0};
    std::atomic<uint64_t> nrRunning{0};

    using PathWithInfo = std::pair<ValidPathInfo, std::unique_ptr<Source>>;

    std::map<StorePath, PathWithInfo *> infosMap;
    StorePathSet storePathsToAdd;
    for (auto & thingToAdd : pathsToCopy) {
        infosMap.insert_or_assign(thingToAdd.first.path, &thingToAdd);
        storePathsToAdd.insert(thingToAdd.first.path);
    }

    auto showProgress = [&]() {
        act.progress(nrDone, pathsToCopy.size(), nrRunning, nrFailed);
    };

    processGraph<StorePath>("addMultipleToStore pool",
        storePathsToAdd,

        [&](const StorePath & path) {

            auto & [info, _] = *infosMap.at(path);

            if (isValidPath(info.path)) {
                nrDone++;
                showProgress();
                return StorePathSet();
            }

            bytesExpected += info.narSize;
            act.setExpected(actCopyPath, bytesExpected);

            return info.references;
        },

        [&](const StorePath & path) {
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

            if (!isValidPath(info.path)) {
                MaintainCount<decltype(nrRunning)> mc(nrRunning);
                showProgress();
                try {
                    addToStore(info, *source, repair, checkSigs);
                } catch (Error & e) {
                    nrFailed++;
                    if (!settings.keepGoing)
                        throw e;
                    printMsg(lvlError, "could not copy %s: %s", printStorePath(path), e.what());
                    showProgress();
                    return;
                }
            }

            nrDone++;
            showProgress();
        });
}

void Store::addMultipleToStore(
    Source & source,
    RepairFlag repair,
    CheckSigsFlag checkSigs)
{
    auto remoteVersion = getProtocol();

    auto expected = readNum<uint64_t>(source);
    for (uint64_t i = 0; i < expected; ++i) {
        // FIXME we should not be using the worker protocol here at all!
        auto info = WorkerProto::Serialise<ValidPathInfo>::read(*this,
            WorkerProto::ReadConn {source, remoteVersion}
        );
        info.ultimate = false;
        addToStore(info, source, repair, checkSigs);
    }
}

namespace {
/**
 * If the NAR archive contains a single file at top-level, then save
 * the contents of the file to `s`.  Otherwise assert.
 */
struct RetrieveRegularNARVisitor : NARParseVisitor
{
    struct MyFileHandle : public FileHandle
    {
        Sink & sink;

        void receiveContents(std::string_view data) override
        {
            sink(data);
        }

    private:
        MyFileHandle(Sink & sink) : sink(sink) {}

        friend struct RetrieveRegularNARVisitor;
    };

    Sink & sink;

    RetrieveRegularNARVisitor(Sink & sink) : sink(sink) { }

    std::unique_ptr<FileHandle> createRegularFile(const Path & path, uint64_t size, bool executable) override
    {
        return std::unique_ptr<MyFileHandle>(new MyFileHandle{sink});
    }

    void createDirectory(const Path & path) override
    {
        assert(false && "RetrieveRegularNARVisitor::createDirectory must not be called");
    }

    void createSymlink(const Path & path, const std::string & target) override
    {
        assert(false && "RetrieveRegularNARVisitor::createSymlink must not be called");
    }
};
}

/*
The aim of this function is to compute in one pass the correct ValidPathInfo for
the files that we are trying to add to the store. To accomplish that in one
pass, given the different kind of inputs that we can take (normal nar archives,
nar archives with non SHA-256 hashes, and flat files), we set up a net of sinks
and aliases. Also, since the dataflow is obfuscated by this, we include here a
graphviz diagram:

digraph graphname {
    node [shape=box]
    fileSource -> narSink
    narSink [style=dashed]
    narSink -> unsualHashTee [style = dashed, label = "Recursive && !SHA-256"]
    narSink -> narHashSink [style = dashed, label = "else"]
    unsualHashTee -> narHashSink
    unsualHashTee -> caHashSink
    fileSource -> parseSink
    parseSink [style=dashed]
    parseSink-> fileSink [style = dashed, label = "Flat"]
    parseSink -> blank [style = dashed, label = "Recursive"]
    fileSink -> caHashSink
}
*/
ValidPathInfo Store::addToStoreSlow(std::string_view name, const Path & srcPath,
    FileIngestionMethod method, HashType hashAlgo,
    std::optional<Hash> expectedCAHash)
{
    HashSink narHashSink { HashType::SHA256 };
    HashSink caHashSink { hashAlgo };

    /* Note that fileSink and unusualHashTee must be mutually exclusive, since
       they both write to caHashSink. Note that that requisite is currently true
       because the former is only used in the flat case. */
    RetrieveRegularNARVisitor fileSink { caHashSink };
    TeeSink unusualHashTee { narHashSink, caHashSink };

    auto & narSink = method == FileIngestionMethod::Recursive && hashAlgo != HashType::SHA256
        ? static_cast<Sink &>(unusualHashTee)
        : narHashSink;

    /* Functionally, this means that fileSource will yield the content of
       srcPath. The fact that we use scratchpadSink as a temporary buffer here
       is an implementation detail. */
    auto fileSource = GeneratorSource{dumpPath(srcPath)};

    /* tapped provides the same data as fileSource, but we also write all the
       information to narSink. */
    TeeSource tapped { fileSource, narSink };

    NARParseVisitor blank;
    auto & parseSink = method == FileIngestionMethod::Flat
        ? fileSink
        : blank;

    /* The information that flows from tapped (besides being replicated in
       narSink), is now put in parseSink. */
    parseDump(parseSink, tapped);

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

    if (!isValidPath(info.path)) {
        auto source = GeneratorSource{dumpPath(srcPath)};
        addToStore(info, source);
    }

    return info;
}

StringSet StoreConfig::getDefaultSystemFeatures()
{
    auto res = settings.systemFeatures.get();

    if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations))
        res.insert("ca-derivations");

    if (experimentalFeatureSettings.isEnabled(Xp::RecursiveNix))
        res.insert("recursive-nix");

    return res;
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

std::map<std::string, std::optional<StorePath>> Store::queryStaticPartialDerivationOutputMap(const StorePath & path)
{
    std::map<std::string, std::optional<StorePath>> outputs;
    auto drv = readInvalidDerivation(path);
    for (auto & [outputName, output] : drv.outputsAndOptPaths(*this)) {
        outputs.emplace(outputName, output.second);
    }
    return outputs;
}

std::map<std::string, std::optional<StorePath>> Store::queryPartialDerivationOutputMap(
    const StorePath & path,
    Store * evalStore_)
{
    auto & evalStore = evalStore_ ? *evalStore_ : *this;

    auto outputs = evalStore.queryStaticPartialDerivationOutputMap(path);

    if (!experimentalFeatureSettings.isEnabled(Xp::CaDerivations))
        return outputs;

    auto drv = evalStore.readInvalidDerivation(path);
    auto drvHashes = staticOutputHashes(*this, drv);
    for (auto & [outputName, hash] : drvHashes) {
        auto realisation = queryRealisation(DrvOutput{hash, outputName});
        if (realisation) {
            outputs.insert_or_assign(outputName, realisation->outPath);
        } else {
            // queryStaticPartialDerivationOutputMap is not guaranteed
            // to return std::nullopt for outputs which are not
            // statically known.
            outputs.insert({outputName, std::nullopt});
        }
    }

    return outputs;
}

OutputPathMap Store::queryDerivationOutputMap(const StorePath & path, Store * evalStore) {
    auto resp = queryPartialDerivationOutputMap(path, evalStore);
    OutputPathMap result;
    for (auto & [outName, optOutPath] : resp) {
        if (!optOutPath)
            throw MissingRealisation(printStorePath(path), outName);
        result.insert_or_assign(outName, *optOutPath);
    }
    return result;
}

StorePathSet Store::queryDerivationOutputs(const StorePath & path)
{
    auto outputMap = this->queryDerivationOutputMap(path);
    StorePathSet outputPaths;
    for (auto & i: outputMap) {
        outputPaths.emplace(std::move(i.second));
    }
    return outputPaths;
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
                auto info = sub->queryPathInfo(subPath);

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


bool Store::isValidPath(const StorePath & storePath)
{
    {
        auto state_(state.lock());
        auto res = state_->pathInfoCache.get(std::string(storePath.to_string()));
        if (res && res->isKnownNow()) {
            stats.narInfoReadAverted++;
            return res->didExist();
        }
    }

    if (diskCache) {
        auto res = diskCache->lookupNarInfo(getUri(), std::string(storePath.hashPart()));
        if (res.first != NarInfoDiskCache::oUnknown) {
            stats.narInfoReadAverted++;
            auto state_(state.lock());
            state_->pathInfoCache.upsert(std::string(storePath.to_string()),
                res.first == NarInfoDiskCache::oInvalid ? PathInfoCacheValue{} : PathInfoCacheValue { .value = res.second });
            return res.first == NarInfoDiskCache::oValid;
        }
    }

    bool valid = isValidPathUncached(storePath);

    if (diskCache && !valid)
        // FIXME: handle valid = true case.
        diskCache->upsertNarInfo(getUri(), std::string(storePath.hashPart()), 0);

    return valid;
}


/* Default implementation for stores that only implement
   queryPathInfoUncached(). */
bool Store::isValidPathUncached(const StorePath & path)
{
    try {
        queryPathInfo(path);
        return true;
    } catch (InvalidPath &) {
        return false;
    }
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


ref<const ValidPathInfo> Store::queryPathInfo(const StorePath & storePath)
{
    auto hashPart = std::string(storePath.hashPart());

    {
        auto res = state.lock()->pathInfoCache.get(std::string(storePath.to_string()));
        if (res && res->isKnownNow()) {
            stats.narInfoReadAverted++;
            if (!res->didExist())
                throw InvalidPath("path '%s' does not exist in the store", printStorePath(storePath));
            return ref<const ValidPathInfo>(res->value);
        }
    }

    if (diskCache) {
        auto res = diskCache->lookupNarInfo(getUri(), hashPart);
        if (res.first != NarInfoDiskCache::oUnknown) {
            stats.narInfoReadAverted++;
            {
                auto state_(state.lock());
                state_->pathInfoCache.upsert(std::string(storePath.to_string()),
                    res.first == NarInfoDiskCache::oInvalid ? PathInfoCacheValue{} : PathInfoCacheValue{ .value = res.second });
                if (res.first == NarInfoDiskCache::oInvalid)
                    throw InvalidPath("path '%s' does not exist in the store", printStorePath(storePath));
            }
            return ref<const ValidPathInfo>(res.second);
        }
    }

    auto info = queryPathInfoUncached(storePath);
    if (info) {
        // first, before we cache anything, check that the store gave us valid data.
        ensureGoodStorePath(this, storePath, info->path);
    }

    if (diskCache) {
        diskCache->upsertNarInfo(getUri(), hashPart, info);
    }

    {
        auto state_(state.lock());
        state_->pathInfoCache.upsert(std::string(storePath.to_string()), PathInfoCacheValue { .value = info });
    }

    if (!info) {
        stats.narInfoMissing++;
        throw InvalidPath("path '%s' does not exist in the store", printStorePath(storePath));
    }

    return ref<const ValidPathInfo>(info);
}

std::shared_ptr<const Realisation> Store::queryRealisation(const DrvOutput & id)
{

    if (diskCache) {
        auto [cacheOutcome, maybeCachedRealisation]
            = diskCache->lookupRealisation(getUri(), id);
        switch (cacheOutcome) {
        case NarInfoDiskCache::oValid:
            debug("Returning a cached realisation for %s", id.to_string());
            return maybeCachedRealisation;
        case NarInfoDiskCache::oInvalid:
            debug(
                "Returning a cached missing realisation for %s",
                id.to_string());
            return nullptr;
        case NarInfoDiskCache::oUnknown:
            break;
        }
    }

    auto info = queryRealisationUncached(id);

    if (diskCache) {
        if (info)
            diskCache->upsertRealisation(getUri(), *info);
        else
            diskCache->upsertAbsentRealisation(getUri(), id);
    }

    return info;
}

kj::Promise<Result<void>> Store::substitutePaths(const StorePathSet & paths)
try {
    std::vector<DerivedPath> paths2;
    for (auto & path : paths)
        if (!path.isDerivation())
            paths2.emplace_back(DerivedPath::Opaque{path});
    uint64_t downloadSize, narSize;
    StorePathSet willBuild, willSubstitute, unknown;
    queryMissing(paths2,
        willBuild, willSubstitute, unknown, downloadSize, narSize);

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


StorePathSet Store::queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute)
{
    struct State
    {
        size_t left;
        StorePathSet valid;
        std::exception_ptr exc = {};
    };

    Sync<State> state_(State{paths.size(), StorePathSet()});

    std::condition_variable wakeup;
    ThreadPool pool{"queryValidPaths pool"};

    auto doQuery = [&](const StorePath & path) {
        checkInterrupt();

        bool exists = false;
        std::exception_ptr newExc{};

        try {
            queryPathInfo(path);
            exists = true;
        } catch (InvalidPath &) {
        } catch (...) {
            newExc = std::current_exception();
        }

        {
            auto state(state_.lock());

            if (exists) {
                state->valid.insert(path);
            }
            if (newExc != nullptr) {
                state->exc = newExc;
            }
            assert(state->left);
            if (!--state->left)
                wakeup.notify_one();
        }
    };

    for (auto & path : paths)
        pool.enqueue(std::bind(doQuery, path));

    pool.process();

    while (true) {
        auto state(state_.lock());
        if (!state->left) {
            if (state->exc) std::rethrow_exception(state->exc);
            return std::move(state->valid);
        }
        state.wait(wakeup);
    }
}


/* Return a string accepted by decodeValidPathInfo() that
   registers the specified paths as valid.  Note: it's the
   responsibility of the caller to provide a closure. */
std::string Store::makeValidityRegistration(const StorePathSet & paths,
    bool showDerivers, bool showHash)
{
    std::string s = "";

    for (auto & i : paths) {
        s += printStorePath(i) + "\n";

        auto info = queryPathInfo(i);

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

    return s;
}


kj::Promise<Result<StorePathSet>>
Store::exportReferences(const StorePathSet & storePaths, const StorePathSet & inputPaths)
try {
    StorePathSet paths;

    for (auto & storePath : storePaths) {
        if (!inputPaths.count(storePath))
            throw BuildError("cannot export references of path '%s' because it is not in the input closure of the derivation", printStorePath(storePath));

        computeFSClosure({storePath}, paths);
    }

    /* If there are derivations in the graph, then include their
       outputs as well.  This is useful if you want to do things
       like passing all build-time dependencies of some path to a
       derivation that builds a NixOS DVD image. */
    auto paths2 = paths;

    for (auto & j : paths2) {
        if (j.isDerivation()) {
            Derivation drv = TRY_AWAIT(derivationFromPath(j));
            for (auto & k : drv.outputsAndOptPaths(*this)) {
                if (!k.second.second)
                    /* FIXME: I am confused why we are calling
                       `computeFSClosure` on the output path, rather than
                       derivation itself. That doesn't seem right to me, so I
                       won't try to implemented this for CA derivations. */
                    throw UnimplementedError("exportReferences on CA derivations is not yet implemented");
                computeFSClosure(*k.second.second, paths);
            }
        }
    }

    co_return paths;
} catch (...) {
    co_return result::current_exception();
}

json Store::pathInfoToJSON(const StorePathSet & storePaths,
    bool includeImpureInfo, bool showClosureSize,
    Base hashBase,
    AllowInvalidFlag allowInvalid)
{
    json::array_t jsonList = json::array();

    for (auto & storePath : storePaths) {
        auto& jsonPath = jsonList.emplace_back(json::object());

        try {
            auto info = queryPathInfo(storePath);

            jsonPath["path"] = printStorePath(info->path);
            jsonPath["valid"] = true;
            jsonPath["narHash"] = info->narHash.to_string(hashBase, true);
            jsonPath["narSize"] = info->narSize;

            {
                auto& jsonRefs = (jsonPath["references"] = json::array());
                for (auto & ref : info->references)
                    jsonRefs.emplace_back(printStorePath(ref));
            }

            if (info->ca)
                jsonPath["ca"] = renderContentAddress(info->ca);

            std::pair<uint64_t, uint64_t> closureSizes;

            if (showClosureSize) {
                closureSizes = getClosureSize(info->path);
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
    return jsonList;
}


std::pair<uint64_t, uint64_t> Store::getClosureSize(const StorePath & storePath)
{
    uint64_t totalNarSize = 0, totalDownloadSize = 0;
    StorePathSet closure;
    computeFSClosure(storePath, closure, false, false);
    for (auto & p : closure) {
        auto info = queryPathInfo(p);
        totalNarSize += info->narSize;
        auto narInfo = std::dynamic_pointer_cast<const NarInfo>(
            std::shared_ptr<const ValidPathInfo>(info));
        if (narInfo)
            totalDownloadSize += narInfo->fileSize;
    }
    return {totalNarSize, totalDownloadSize};
}


const Store::Stats & Store::getStats()
{
    {
        auto state_(state.lock());
        stats.pathInfoCacheSize = state_->pathInfoCache.size();
    }
    return stats;
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


// buffer size for path copy progress reporting. should be large enough to not cause excessive
// overhead during copies, but small enough to provide reasonably quick copy progress updates.
static constexpr unsigned PATH_COPY_BUFSIZE = 65536;

void copyStorePath(
    Store & srcStore,
    Store & dstStore,
    const StorePath & storePath,
    RepairFlag repair,
    CheckSigsFlag checkSigs)
{
    /* Bail out early (before starting a download from srcStore) if
       dstStore already has this path. */
    if (!repair && dstStore.isValidPath(storePath))
        return;

    auto srcUri = srcStore.getUri();
    auto dstUri = dstStore.getUri();
    auto storePathS = srcStore.printStorePath(storePath);
    Activity act(*logger, lvlInfo, actCopyPath,
        makeCopyPathMessage(srcUri, dstUri, storePathS),
        {storePathS, srcUri, dstUri});
    PushActivity pact(act.id);

    auto info = srcStore.queryPathInfo(storePath);

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

    GeneratorSource source{
        [](auto & act, auto & info, auto & srcStore, auto & storePath) -> WireFormatGenerator {
            auto nar = srcStore.narFromPath(storePath);
            auto buf = std::make_unique<char[]>(PATH_COPY_BUFSIZE);
            uint64_t total = 0;
            while (true) {
                try {
                    auto got = nar->read(buf.get(), PATH_COPY_BUFSIZE);
                    total += got;
                    act.progress(total, info->narSize);
                    co_yield std::span{buf.get(), got};
                } catch (EndOfFile &) {
                    break;
                }
            }
        }(act, info, srcStore, storePath)
    };

    dstStore.addToStore(*info, source, repair, checkSigs);
}


std::map<StorePath, StorePath> copyPaths(
    Store & srcStore,
    Store & dstStore,
    const RealisedPath::Set & paths,
    RepairFlag repair,
    CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
{
    StorePathSet storePaths;
    std::set<Realisation> toplevelRealisations;
    for (auto & path : paths) {
        storePaths.insert(path.path());
        if (auto realisation = std::get_if<Realisation>(&path.raw)) {
            experimentalFeatureSettings.require(Xp::CaDerivations);
            toplevelRealisations.insert(*realisation);
        }
    }
    auto pathsMap = copyPaths(srcStore, dstStore, storePaths, repair, checkSigs, substitute);

    try {
        // Copy the realisation closure
        processGraph<Realisation>(
            "copyPaths pool", Realisation::closure(srcStore, toplevelRealisations),
            [&](const Realisation & current) -> std::set<Realisation> {
                std::set<Realisation> children;
                for (const auto & [drvOutput, _] : current.dependentRealisations) {
                    auto currentChild = srcStore.queryRealisation(drvOutput);
                    if (!currentChild)
                        throw Error(
                            "incomplete realisation closure: '%s' is a "
                            "dependency of '%s' but isn't registered",
                            drvOutput.to_string(), current.id.to_string());
                    children.insert(*currentChild);
                }
                return children;
            },
            [&](const Realisation& current) -> void {
                dstStore.registerDrvOutput(current, checkSigs);
            });
    } catch (MissingExperimentalFeature & e) {
        // Don't fail if the remote doesn't support CA derivations is it might
        // not be within our control to change that, and we might still want
        // to at least copy the output paths.
        if (e.missingFeature == Xp::CaDerivations)
            ignoreExceptionExceptInterrupt();
        else
            throw;
    }

    return pathsMap;
}

std::map<StorePath, StorePath> copyPaths(
    Store & srcStore,
    Store & dstStore,
    const StorePathSet & storePaths,
    RepairFlag repair,
    CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
{
    auto valid = dstStore.queryValidPaths(storePaths, substitute);

    StorePathSet missing;
    for (auto & path : storePaths)
        if (!valid.count(path)) missing.insert(path);

    Activity act(*logger, lvlInfo, actCopyPaths, fmt("copying %d paths", missing.size()));

    // In the general case, `addMultipleToStore` requires a sorted list of
    // store paths to add, so sort them right now
    auto sortedMissing = srcStore.topoSortPaths(missing);
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
        auto info = srcStore.queryPathInfo(missingPath);

        auto storePathForDst = computeStorePathForDst(*info);
        pathsMap.insert_or_assign(missingPath, storePathForDst);

        ValidPathInfo infoForDst = *info;
        infoForDst.path = storePathForDst;

        auto source = [](auto & srcStore, auto & dstStore, auto missingPath, auto info
                      ) -> WireFormatGenerator {
            // We can reasonably assume that the copy will happen whenever we
            // read the path, so log something about that at that point
            auto srcUri = srcStore.getUri();
            auto dstUri = dstStore.getUri();
            auto storePathS = srcStore.printStorePath(missingPath);
            Activity act(
                *logger,
                lvlInfo,
                actCopyPath,
                makeCopyPathMessage(srcUri, dstUri, storePathS),
                {storePathS, srcUri, dstUri}
            );
            PushActivity pact(act.id);

            auto nar = srcStore.narFromPath(missingPath);
            auto buf = std::make_unique<char[]>(PATH_COPY_BUFSIZE);
            uint64_t total = 0;
            while (true) {
                try {
                    auto got = nar->read(buf.get(), PATH_COPY_BUFSIZE);
                    total += got;
                    act.progress(total, info->narSize);
                    co_yield std::span{buf.get(), got};
                } catch (EndOfFile &) {
                    break;
                }
            }
        };
        pathsToCopy.push_back(std::pair{
            infoForDst,
            std::make_unique<GeneratorSource>(source(srcStore, dstStore, missingPath, info))
        });
    }

    dstStore.addMultipleToStore(pathsToCopy, act, repair, checkSigs);

    return pathsMap;
}

void copyClosure(
    Store & srcStore,
    Store & dstStore,
    const RealisedPath::Set & paths,
    RepairFlag repair,
    CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
{
    if (&srcStore == &dstStore) return;

    RealisedPath::Set closure;
    RealisedPath::closure(srcStore, paths, closure);

    copyPaths(srcStore, dstStore, closure, repair, checkSigs, substitute);
}

void copyClosure(
    Store & srcStore,
    Store & dstStore,
    const StorePathSet & storePaths,
    RepairFlag repair,
    CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
{
    if (&srcStore == &dstStore) return;

    StorePathSet closure;
    srcStore.computeFSClosure(storePaths, closure);
    copyPaths(srcStore, dstStore, closure, repair, checkSigs, substitute);
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
    co_return readDerivation(drvPath);
} catch (...) {
    co_return result::current_exception();
}

Derivation readDerivationCommon(Store& store, const StorePath& drvPath, bool requireValidPath)
{
    auto accessor = store.getFSAccessor();
    try {
        return parseDerivation(store,
            accessor->readFile(store.printStorePath(drvPath), requireValidPath),
            Derivation::nameFromPath(drvPath));
    } catch (FormatError & e) {
        throw Error("error parsing derivation '%s': %s", store.printStorePath(drvPath), e.msg());
    }
}

std::optional<StorePath> Store::getBuildDerivationPath(const StorePath & path)
{

    if (!path.isDerivation()) {
        try {
            auto info = queryPathInfo(path);
            return info->deriver;
        } catch (InvalidPath &) {
            return std::nullopt;
        }
    }

    if (!experimentalFeatureSettings.isEnabled(Xp::CaDerivations) || !isValidPath(path))
        return path;

    auto drv = readDerivation(path);
    if (!drv.type().hasKnownOutputPaths()) {
        // The build log is actually attached to the corresponding
        // resolved derivation, so we need to get it first
        auto resolvedDrv = drv.tryResolve(*this);
        if (resolvedDrv)
            return writeDerivation(*this, *resolvedDrv, NoRepair, true);
    }

    return path;
}

Derivation Store::readDerivation(const StorePath & drvPath)
{ return readDerivationCommon(*this, drvPath, true); }

Derivation Store::readInvalidDerivation(const StorePath & drvPath)
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

std::shared_ptr<Store> openFromNonUri(const std::string & uri, const StoreConfig::Params & params)
{
    if (uri == "" || uri == "auto") {
        auto stateDir = getOr(params, "state", settings.nixStateDir);
        if (access(stateDir.c_str(), R_OK | W_OK) == 0)
            return LocalStore::makeLocalStore(params);
        else if (pathExists(settings.nixDaemonSocketFile))
            return std::make_shared<UDSRemoteStore>(params);
        #if __linux__
        else if (!pathExists(stateDir)
            && params.empty()
            && getuid() != 0
            && !getEnv("NIX_STORE_DIR").has_value()
            && !getEnv("NIX_STATE_DIR").has_value())
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
                warn("'%s' does not exist, so Lix will use '%s' as a chroot store", stateDir, chrootStore);
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
        return std::make_shared<UDSRemoteStore>(params);
    } else if (uri == "local") {
        return LocalStore::makeLocalStore(params);
    } else if (isNonUriPath(uri)) {
        StoreConfig::Params params2 = params;
        params2["root"] = absPath(uri);
        return LocalStore::makeLocalStore(params2);
    } else {
        return nullptr;
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
        std::regex v6AddrRegex("^((.*)@)?\\[(.*)\\]$");

        if (std::regex_match(connStr, result, v6AddrRegex)) {
            if (result[1].matched) {
                return result.str(1) + result.str(3);
            }
            return result.str(3);
        }
    }

    return connStr;
}

kj::Promise<Result<ref<Store>>> openStore(const std::string & uri_,
    const StoreConfig::Params & extraParams)
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
                    experimentalFeatureSettings.require(store->config().experimentalFeature());
                    store->init();
                    store->config().warnUnknownSettings();
                    co_return ref<Store>(store);
                }
            }
        }
    }
    catch (BadURL &) {
        auto [uri, uriParams] = splitUriAndParams(uri_);
        params.insert(uriParams.begin(), uriParams.end());

        if (auto store = openFromNonUri(uri, params)) {
            store->config().warnUnknownSettings();
            co_return ref<Store>(store);
        }
    }

    throw Error("don't know how to open Nix store '%s'", uri_);
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::list<ref<Store>>>> getDefaultSubstituters()
try {
    static std::shared_mutex mtx;
    static std::optional<std::list<ref<Store>>> stores;

    if (std::shared_lock l(mtx); stores.has_value()) {
        co_return *stores;
    }

    std::lock_guard l(mtx);

    if (!stores.has_value()) {
        StringSet done;

        stores.emplace();
        for (auto uri : settings.substituters.get()) {
            if (!done.insert(uri).second) continue;
            try {
                stores->push_back(TRY_AWAIT(openStore(uri)));
            } catch (Error & e) {
                logWarning(e.info());
            }
        }

        stores->sort([](ref<Store> & a, ref<Store> & b) {
            return a->config().priority < b->config().priority;
        });
    }

    co_return *stores;
} catch (...) {
    co_return result::current_exception();
}

std::vector<StoreFactory> * StoreImplementations::registered = 0;

}
