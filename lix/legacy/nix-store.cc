#include "lix/libutil/archive.hh"
#include "lix/libstore/derivations.hh"
#include "dotgraph.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/exit.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/build-result.hh" // IWYU pragma: keep
#include "lix/libstore/store-cast.hh"
#include "lix/libstore/gc-store.hh"
#include "lix/libstore/log-store.hh"
#include "lix/libstore/local-store.hh"
#include "lix/libutil/monitor-fd.hh"
#include "lix/libstore/serve-protocol.hh"
#include "lix/libstore/serve-protocol-impl.hh" // IWYU pragma: keep
#include "lix/libmain/shared.hh"
#include "graphml.hh"
#include "lix/libcmd/legacy.hh"
#include "lix/libstore/path-with-outputs.hh"
#include "lix/libutil/serialise.hh"
#include "nix-store.hh"

#include <cstdint>
#include <ctime>
#include <iostream>
#include <algorithm>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


namespace nix {


using std::cin;
using std::cout;

typedef void (*Operation)(
    std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs
);

static Path gcRoot;
static int rootNr = 0;
static bool noOutput = false;

ref<LocalStore> ensureLocalStore(std::shared_ptr<Store> store)
{
    auto store2 = std::dynamic_pointer_cast<LocalStore>(store);
    if (!store2) throw Error("you don't have sufficient rights to use this command");
    return ref<LocalStore>::unsafeFromPtr(store2);
}

static kj::Promise<Result<StorePath>>
useDeriver(std::shared_ptr<Store> store, const StorePath & path)
try {
    if (path.isDerivation()) co_return path;
    auto info = TRY_AWAIT(store->queryPathInfo(path));
    if (!info->deriver)
        throw Error("deriver of path '%s' is not known", store->printStorePath(path));
    co_return *info->deriver;
} catch (...) {
    co_return result::current_exception();
}


/* Realise the given path.  For a derivation that means build it; for
   other paths it means ensure their validity. */
static kj::Promise<Result<PathSet>>
realisePath(std::shared_ptr<Store> store, StorePathWithOutputs path, bool build = true)
try {
    auto store2 = std::dynamic_pointer_cast<LocalFSStore>(store);

    if (path.path.isDerivation()) {
        if (build) TRY_AWAIT(store->buildPaths({path.toDerivedPath()}));
        auto outputPaths = TRY_AWAIT(store->queryDerivationOutputMap(path.path));
        Derivation drv = TRY_AWAIT(store->derivationFromPath(path.path));
        rootNr++;

        /* FIXME: Encode this empty special case explicitly in the type. */
        if (path.outputs.empty())
            for (auto & i : drv.outputs) path.outputs.insert(i.first);

        PathSet outputs;
        for (auto & j : path.outputs) {
            /* Match outputs of a store path with outputs of the derivation that produces it. */
            DerivationOutputs::iterator i = drv.outputs.find(j);
            if (i == drv.outputs.end())
                throw Error("derivation '%s' does not have an output named '%s'",
                    store2->printStorePath(path.path), j);
            auto outPath = outputPaths.at(i->first);
            auto retPath = store->printStorePath(outPath);
            if (store2) {
                if (gcRoot == "")
                    printGCWarning();
                else {
                    Path rootName = gcRoot;
                    if (rootNr > 1) rootName += "-" + std::to_string(rootNr);
                    if (i->first != "out") rootName += "-" + i->first;
                    retPath = TRY_AWAIT(store2->addPermRoot(outPath, rootName));
                }
            }
            outputs.insert(retPath);
        }
        co_return outputs;
    }

    else {
        if (build) TRY_AWAIT(store->ensurePath(path.path));
        else if (!TRY_AWAIT(store->isValidPath(path.path)))
            throw Error("path '%s' does not exist and cannot be created", store->printStorePath(path.path));
        if (store2) {
            if (gcRoot == "")
                printGCWarning();
            else {
                Path rootName = gcRoot;
                rootNr++;
                if (rootNr > 1) rootName += "-" + std::to_string(rootNr);
                co_return PathSet{TRY_AWAIT(store2->addPermRoot(path.path, rootName))};
            }
        }
        co_return PathSet{store->printStorePath(path.path)};
    }
} catch (...) {
    co_return result::current_exception();
}


/* Realise the given paths. */
static void
opRealise(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    bool dryRun = false;
    BuildMode buildMode = bmNormal;
    bool ignoreUnknown = false;

    for (auto & i : opFlags)
        if (i == "--dry-run") dryRun = true;
        else if (i == "--repair") buildMode = bmRepair;
        else if (i == "--check") buildMode = bmCheck;
        else if (i == "--ignore-unknown") ignoreUnknown = true;
        else throw UsageError("unknown flag '%1%'", i);

    std::vector<StorePathWithOutputs> paths;
    for (auto & i : opArgs)
        paths.push_back(followLinksToStorePathWithOutputs(*store, i));

    uint64_t downloadSize, narSize;
    StorePathSet willBuild, willSubstitute, unknown;
    aio.blockOn(store->queryMissing(
        toDerivedPaths(paths),
        willBuild, willSubstitute, unknown, downloadSize, narSize));

    /* Filter out unknown paths from `paths`. */
    if (ignoreUnknown) {
        std::vector<StorePathWithOutputs> paths2;
        for (auto & i : paths)
            if (!unknown.count(i.path)) paths2.push_back(i);
        paths = std::move(paths2);
        unknown = StorePathSet();
    }

    if (settings.printMissing) {
        aio.blockOn(printMissing(
            ref<Store>::unsafeFromPtr(store), willBuild, willSubstitute, unknown, downloadSize, narSize
        ));
    }

    if (dryRun) return;

    /* Build all paths at the same time to exploit parallelism. */
    aio.blockOn(store->buildPaths(toDerivedPaths(paths), buildMode));

    if (!ignoreUnknown)
        for (auto & i : paths) {
            auto paths2 = aio.blockOn(realisePath(store, i, false));
            if (!noOutput)
                for (auto & j : paths2)
                    cout << fmt("%1%\n", j);
        }
}


/* Add files to the Nix store and print the resulting paths. */
static void opAdd(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (auto & i : opArgs) {
        cout << fmt(
            "%s\n",
            store->printStorePath(aio.blockOn(
                store->addToStoreRecursive(std::string(baseNameOf(i)), *prepareDump(i))
            ))
        );
    }
}


/* Preload the output of a fixed-output derivation into the Nix
   store. */
static void
opAddFixed(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    auto method = FileIngestionMethod::Flat;

    for (auto & i : opFlags)
        if (i == "--recursive") method = FileIngestionMethod::Recursive;
        else throw UsageError("unknown flag '%1%'", i);

    if (opArgs.empty())
        throw UsageError("first argument must be hash algorithm");

    HashType hashAlgo = parseHashType(opArgs.front());
    opArgs.pop_front();

    for (auto & i : opArgs) {
        std::cout << fmt(
            "%s\n",
            store->printStorePath(
                aio.blockOn(store->addToStoreSlow(baseNameOf(i), i, method, hashAlgo)).path
            )
        );
    }
}


/* Hack to support caching in `nix-prefetch-url'. */
static void
opPrintFixedPath(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    auto method = FileIngestionMethod::Flat;

    for (auto i : opFlags)
        if (i == "--recursive") method = FileIngestionMethod::Recursive;
        else throw UsageError("unknown flag '%1%'", i);

    if (opArgs.size() != 3)
        throw UsageError("'--print-fixed-path' requires three arguments");

    Strings::iterator i = opArgs.begin();
    HashType hashAlgo = parseHashType(*i++);
    std::string hash = *i++;
    std::string name = *i++;

    cout << fmt("%s\n", store->printStorePath(store->makeFixedOutputPath(name, FixedOutputInfo {
        .method = method,
        .hash = Hash::parseAny(hash, hashAlgo),
        .references = {},
    })));
}

static kj::Promise<Result<StorePathSet>> maybeUseOutputs(
    std::shared_ptr<Store> store, const StorePath & storePath, bool useOutput, bool forceRealise
)
try {
    if (forceRealise) {
        TRY_AWAIT(realisePath(store, {storePath}));
    }
    if (useOutput && storePath.isDerivation()) {
        auto drv = TRY_AWAIT(store->derivationFromPath(storePath));
        StorePathSet outputs;
        if (forceRealise)
            co_return TRY_AWAIT(store->queryDerivationOutputs(storePath));
        for (auto & i : drv.outputsAndPaths(*store)) {
            outputs.insert(i.second.second);
        }
        co_return outputs;
    }
    else co_return StorePathSet{storePath};
} catch (...) {
    co_return result::current_exception();
}


/* Some code to print a tree representation of a derivation dependency
   graph.  Topological sorting is used to keep the tree relatively
   flat. */
static void printTree(
    std::shared_ptr<Store> store,
    AsyncIoRoot & aio,
    const StorePath & path,
    const std::string & firstPad,
    const std::string & tailPad,
    StorePathSet & done
)
{
    if (!done.insert(path).second) {
        cout << fmt("%s%s [...]\n", firstPad, store->printStorePath(path));
        return;
    }

    cout << fmt("%s%s\n", firstPad, store->printStorePath(path));

    auto info = aio.blockOn(store->queryPathInfo(path));

    /* Topologically sort under the relation A < B iff A \in
       closure(B).  That is, if derivation A is an (possibly indirect)
       input of B, then A is printed first.  This has the effect of
       flattening the tree, preventing deeply nested structures.  */
    auto sorted = aio.blockOn(store->topoSortPaths(info->references));
    reverse(sorted.begin(), sorted.end());

    for (const auto &[n, i] : enumerate(sorted)) {
        bool last = n + 1 == sorted.size();
        printTree(
            store,
            aio,
            i,
            tailPad + (last ? treeLast : treeConn),
            tailPad + (last ? treeNull : treeLine),
            done
        );
    }
}


/* Perform various sorts of queries. */
static void
opQuery(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    enum QueryType
        { qOutputs, qRequisites, qReferences, qReferrers
        , qReferrersClosure, qDeriver, qValidDerivers, qBinding, qHash, qSize
        , qTree, qGraph, qGraphML, qResolve, qRoots };
    std::optional<QueryType> query;
    bool useOutput = false;
    bool includeOutputs = false;
    bool forceRealise = false;
    std::string bindingName;

    for (auto & i : opFlags) {
        std::optional<QueryType> prev = query;
        if (i == "--outputs") query = qOutputs;
        else if (i == "--requisites" || i == "-R") query = qRequisites;
        else if (i == "--references") query = qReferences;
        else if (i == "--referrers" || i == "--referers") query = qReferrers;
        else if (i == "--referrers-closure" || i == "--referers-closure") query = qReferrersClosure;
        else if (i == "--deriver" || i == "-d") query = qDeriver;
        else if (i == "--valid-derivers") query = qValidDerivers;
        else if (i == "--binding" || i == "-b") {
            if (opArgs.size() == 0)
                throw UsageError("expected binding name");
            bindingName = opArgs.front();
            opArgs.pop_front();
            query = qBinding;
        }
        else if (i == "--hash") query = qHash;
        else if (i == "--size") query = qSize;
        else if (i == "--tree") query = qTree;
        else if (i == "--graph") query = qGraph;
        else if (i == "--graphml") query = qGraphML;
        else if (i == "--resolve") query = qResolve;
        else if (i == "--roots") query = qRoots;
        else if (i == "--use-output" || i == "-u") useOutput = true;
        else if (i == "--force-realise" || i == "--force-realize" || i == "-f") forceRealise = true;
        else if (i == "--include-outputs") includeOutputs = true;
        else throw UsageError("unknown flag '%1%'", i);
        if (prev && prev != query)
            throw UsageError("query type '%1%' conflicts with earlier flag", i);
    }

    if (!query) query = qOutputs;

    RunPager pager;

    switch (*query) {

        case qOutputs: {
            for (auto & i : opArgs) {
                auto outputs = aio.blockOn(
                    maybeUseOutputs(store, store->followLinksToStorePath(i), true, forceRealise)
                );
                for (auto & outputPath : outputs)
                    cout << fmt("%1%\n", store->printStorePath(outputPath));
            }
            break;
        }

        case qRequisites:
        case qReferences:
        case qReferrers:
        case qReferrersClosure: {
            StorePathSet paths;
            for (auto & i : opArgs) {
                auto ps = aio.blockOn(maybeUseOutputs(
                    store, store->followLinksToStorePath(i), useOutput, forceRealise
                ));
                for (auto & j : ps) {
                    if (query == qRequisites) {
                        aio.blockOn(store->computeFSClosure(j, paths, false, includeOutputs));
                    }
                    else if (query == qReferences) {
                        for (auto & p : aio.blockOn(store->queryPathInfo(j))->references)
                            paths.insert(p);
                    }
                    else if (query == qReferrers) {
                        StorePathSet tmp;
                        aio.blockOn(store->queryReferrers(j, tmp));
                        for (auto & i : tmp)
                            paths.insert(i);
                    }
                    else if (query == qReferrersClosure)
                        aio.blockOn(store->computeFSClosure(j, paths, true));
                }
            }
            auto sorted = aio.blockOn(store->topoSortPaths(paths));
            for (StorePaths::reverse_iterator i = sorted.rbegin();
                 i != sorted.rend(); ++i)
                cout << fmt("%s\n", store->printStorePath(*i));
            break;
        }

        case qDeriver:
            for (auto & i : opArgs) {
                auto info = aio.blockOn(store->queryPathInfo(store->followLinksToStorePath(i)));
                cout << fmt("%s\n", info->deriver ? store->printStorePath(*info->deriver) : "unknown-deriver");
            }
            break;

        case qValidDerivers: {
            StorePathSet result;
            for (auto & i : opArgs) {
                auto derivers =
                    aio.blockOn(store->queryValidDerivers(store->followLinksToStorePath(i)));
                for (const auto & i : derivers) {
                    result.insert(i);
                }
            }
            auto sorted = aio.blockOn(store->topoSortPaths(result));
            for (StorePaths::reverse_iterator i = sorted.rbegin();
                 i != sorted.rend(); ++i)
                cout << fmt("%s\n", store->printStorePath(*i));
            break;
        }

        case qBinding:
            for (auto & i : opArgs) {
                auto path = aio.blockOn(useDeriver(store, store->followLinksToStorePath(i)));
                Derivation drv = aio.blockOn(store->derivationFromPath(path));
                StringPairs::iterator j = drv.env.find(bindingName);
                if (j == drv.env.end())
                    throw Error("derivation '%s' has no environment binding named '%s'",
                        store->printStorePath(path), bindingName);
                cout << fmt("%s\n", j->second);
            }
            break;

        case qHash:
        case qSize:
            for (auto & i : opArgs) {
                for (auto & j : aio.blockOn(maybeUseOutputs(
                         store, store->followLinksToStorePath(i), useOutput, forceRealise
                     )))
                {
                    auto info = aio.blockOn(store->queryPathInfo(j));
                    if (query == qHash) {
                        assert(info->narHash.type == HashType::SHA256);
                        cout << fmt("%s\n", info->narHash.to_string(Base::Base32, true));
                    } else if (query == qSize)
                        cout << fmt("%d\n", info->narSize);
                }
            }
            break;

        case qTree: {
            StorePathSet done;
            for (auto & i : opArgs)
                printTree(store, aio, store->followLinksToStorePath(i), "", "", done);
            break;
        }

        case qGraph: {
            StorePathSet roots;
            for (auto & i : opArgs)
                for (auto & j : aio.blockOn(maybeUseOutputs(
                         store, store->followLinksToStorePath(i), useOutput, forceRealise
                     )))
                {
                    roots.insert(j);
                }
            aio.blockOn(printDotGraph(ref<Store>::unsafeFromPtr(store), std::move(roots)));
            break;
        }

        case qGraphML: {
            StorePathSet roots;
            for (auto & i : opArgs)
                for (auto & j : aio.blockOn(maybeUseOutputs(
                         store, store->followLinksToStorePath(i), useOutput, forceRealise
                     )))
                {
                    roots.insert(j);
                }
            aio.blockOn(printGraphML(ref<Store>::unsafeFromPtr(store), std::move(roots)));
            break;
        }

        case qResolve: {
            for (auto & i : opArgs)
                cout << fmt("%s\n", store->printStorePath(store->followLinksToStorePath(i)));
            break;
        }

        case qRoots: {
            StorePathSet args;
            for (auto & i : opArgs)
                for (auto & p : aio.blockOn(maybeUseOutputs(
                         store, store->followLinksToStorePath(i), useOutput, forceRealise
                     )))
                {
                    args.insert(p);
                }

            StorePathSet referrers;
            aio.blockOn(store->computeFSClosure(
                args, referrers, true, settings.gcKeepOutputs, settings.gcKeepDerivations));

            auto & gcStore = require<GcStore>(*store);
            Roots roots = aio.blockOn(gcStore.findRoots(false));
            for (auto & [target, links] : roots)
                if (referrers.find(target) != referrers.end())
                    for (auto & link : links)
                        cout << fmt("%1% -> %2%\n", link, gcStore.printStorePath(target));
            break;
        }

        default:
            abort();
    }
}

static void
opPrintEnv(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() != 1) throw UsageError("'--print-env' requires one derivation store path");

    Path drvPath = opArgs.front();
    Derivation drv = aio.blockOn(store->derivationFromPath(store->parseStorePath(drvPath)));

    /* Print each environment variable in the derivation in a format
     * that can be sourced by the shell. */
    for (auto & i : drv.env)
        logger->cout("export %1%; %1%=%2%\n", i.first, shellEscape(i.second));

    /* Also output the arguments.  This doesn't preserve whitespace in
       arguments. */
    cout << "export _args; _args='";
    bool first = true;
    for (auto & i : drv.args) {
        if (!first) cout << ' ';
        first = false;
        cout << shellEscape(i);
    }
    cout << "'\n";
}

static void
opReadLog(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    auto & logStore = require<LogStore>(*store);

    RunPager pager;

    for (auto & i : opArgs) {
        auto path = logStore.followLinksToStorePath(i);
        auto log = aio.blockOn(logStore.getBuildLog(path));
        if (!log)
            throw Error("build log of derivation '%s' is not available", logStore.printStorePath(path));
        std::cout << *log;
    }
}

static void
opDumpDB(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (!opArgs.empty()) {
        for (auto & i : opArgs) {
            cout << aio.blockOn(
                store->makeValidityRegistration({store->followLinksToStorePath(i)}, true, true)
            );
        }
    } else {
        for (auto & i : aio.blockOn(store->queryAllValidPaths()))
            cout << aio.blockOn(store->makeValidityRegistration({i}, true, true));
    }
}

static void registerValidity(
    std::shared_ptr<Store> store,
    AsyncIoRoot & aio,
    bool reregister,
    bool hashGiven,
    bool canonicalise
)
{
    ValidPathInfos infos;

    while (1) {
        // We use a dummy value because we'll set it below. FIXME be correct by
        // construction and avoid dummy value.
        auto hashResultOpt = !hashGiven ? std::optional<HashResult> { {Hash::dummy, -1} } : std::nullopt;
        auto info = decodeValidPathInfo(*store, cin, hashResultOpt);
        if (!info) break;
        if (!aio.blockOn(store->isValidPath(info->path)) || reregister) {
            /* !!! races */
            if (canonicalise)
                canonicalisePathMetaData(store->printStorePath(info->path), {});
            if (!hashGiven) {
                HashResult hash = hashPath(HashType::SHA256, store->printStorePath(info->path));
                info->narHash = hash.first;
                info->narSize = hash.second;
            }
            infos.insert_or_assign(info->path, *info);
        }
    }

    aio.blockOn(ensureLocalStore(store)->registerValidPaths(infos));
}

static void
opLoadDB(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (!opArgs.empty())
        throw UsageError("no arguments expected");
    registerValidity(store, aio, true, true, false);
}

static void
opRegisterValidity(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    bool reregister = false; // !!! maybe this should be the default
    bool hashGiven = false;

    for (auto & i : opFlags)
        if (i == "--reregister") reregister = true;
        else if (i == "--hash-given") hashGiven = true;
        else throw UsageError("unknown flag '%1%'", i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    registerValidity(store, aio, reregister, hashGiven, true);
}

static void
opCheckValidity(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    bool printInvalid = false;

    for (auto & i : opFlags)
        if (i == "--print-invalid") printInvalid = true;
        else throw UsageError("unknown flag '%1%'", i);

    for (auto & i : opArgs) {
        auto path = store->followLinksToStorePath(i);
        if (!aio.blockOn(store->isValidPath(path))) {
            if (printInvalid)
                cout << fmt("%s\n", store->printStorePath(path));
            else
                throw Error("path '%s' is not valid", store->printStorePath(path));
        }
    }
}

static void opGC(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    bool printRoots = false;
    GCOptions options;
    options.action = GCOptions::gcDeleteDead;

    GCResults results;

    /* Do what? */
    for (auto i = opFlags.begin(); i != opFlags.end(); ++i)
        if (*i == "--print-roots") printRoots = true;
        else if (*i == "--print-live") options.action = GCOptions::gcReturnLive;
        else if (*i == "--print-dead") options.action = GCOptions::gcReturnDead;
        else if (*i == "--max-freed")
            options.maxFreed = std::max(getIntArg<int64_t>(*i, i, opFlags.end(), true), (int64_t) 0);
        else throw UsageError("bad sub-operation '%1%' in GC", *i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    auto & gcStore = require<GcStore>(*store);

    if (printRoots) {
        Roots roots = aio.blockOn(gcStore.findRoots(false));
        std::set<std::pair<Path, StorePath>> roots2;
        // Transpose and sort the roots.
        for (auto & [target, links] : roots)
            for (auto & link : links)
                roots2.emplace(link, target);
        for (auto & [link, target] : roots2)
            std::cout << link << " -> " << gcStore.printStorePath(target) << "\n";
    }

    else {
        PrintFreed freed(options.action == GCOptions::gcDeleteDead, results);
        aio.blockOn(gcStore.collectGarbage(options, results));

        if (options.action != GCOptions::gcDeleteDead)
            for (auto & i : results.paths)
                cout << i << std::endl;
    }
}


/* Remove paths from the Nix store if possible (i.e., if they do not
   have any remaining referrers and are not reachable from any GC
   roots). */
static void
opDelete(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    GCOptions options;
    options.action = GCOptions::gcDeleteSpecific;
    bool deleteClosure = false;

    for (auto & i : opFlags)
        if (i == "--ignore-liveness") options.ignoreLiveness = true;
        else if (i == "--skip-live") options.action = GCOptions::gcTryDeleteSpecific;
        else if (i == "--delete-closure") deleteClosure = true;
        else throw UsageError("unknown flag '%1%'", i);

    for (auto & arg : opArgs) {
        StorePath path = store->followLinksToStorePath(arg);
        if (deleteClosure) {
            aio.blockOn(store->computeFSClosure(path, options.pathsToDelete));
        } else {
            options.pathsToDelete.insert(path);
        }
    }

    auto & gcStore = require<GcStore>(*store);

    GCResults results;
    PrintFreed freed(true, results);
    aio.blockOn(gcStore.collectGarbage(options, results));
}


/* Dump a path as a Nix archive.  The archive is written to stdout */
static void opDump(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() != 1) throw UsageError("only one argument allowed");

    FdSink sink(STDOUT_FILENO);
    std::string path = *opArgs.begin();
    sink << dumpPath(path);
    sink.flush();
}


/* Restore a value from a Nix archive.  The archive is read from stdin. */
static void
opRestore(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() != 1) throw UsageError("only one argument allowed");

    FdSource source(STDIN_FILENO);
    restorePath(*opArgs.begin(), source);
}

static void
opExport(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    for (auto & i : opFlags)
        throw UsageError("unknown flag '%1%'", i);

    StorePathSet paths;

    for (auto & i : opArgs)
        paths.insert(store->followLinksToStorePath(i));

    FdSink sink(STDOUT_FILENO);
    aio.blockOn(store->exportPaths(paths, sink));
    sink.flush();
}

static void
opImport(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    for (auto & i : opFlags)
        throw UsageError("unknown flag '%1%'", i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    FdSource source(STDIN_FILENO);
    auto paths = aio.blockOn(store->importPaths(source, NoCheckSigs));

    for (auto & i : paths)
        cout << fmt("%s\n", store->printStorePath(i)) << std::flush;
}


/* Initialise the Nix databases. */
static void opInit(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (!opArgs.empty())
        throw UsageError("no arguments expected");
    /* Doesn't do anything right now; database tables are initialised
       automatically. */
}


/* Verify the consistency of the Nix environment. */
static void
opVerify(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    if (!opArgs.empty())
        throw UsageError("no arguments expected");

    bool checkContents = false;
    RepairFlag repair = NoRepair;

    for (auto & i : opFlags)
        if (i == "--check-contents") checkContents = true;
        else if (i == "--repair") repair = Repair;
        else throw UsageError("unknown flag '%1%'", i);

    if (aio.blockOn(store->verifyStore(checkContents, repair))) {
        printTaggedWarning("not all store errors were fixed");
        throw Exit(1);
    }
}


/* Verify whether the contents of the given store path have not changed. */
static void
opVerifyPath(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty())
        throw UsageError("no flags expected");

    int status = 0;

    for (auto & i : opArgs) {
        auto path = store->followLinksToStorePath(i);
        printMsg(lvlTalkative, "checking path '%s'...", store->printStorePath(path));
        auto info = aio.blockOn(store->queryPathInfo(path));
        HashSink sink(info->narHash.type);
        aio.blockOn(aio.blockOn(store->narFromPath(path))->drainInto(sink));
        auto current = sink.finish();
        if (current.first != info->narHash) {
            printError("path '%s' was modified! expected hash '%s', got '%s'",
                store->printStorePath(path),
                info->narHash.to_string(Base::SRI, true),
                current.first.to_string(Base::SRI, true));
            status = 1;
        }
    }

    throw Exit(status);
}


/* Repair the contents of the given path by redownloading it using a
   substituter (if available). */
static void
opRepairPath(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty())
        throw UsageError("no flags expected");

    for (auto & i : opArgs)
        aio.blockOn(store->repairPath(store->followLinksToStorePath(i)));
}

/* Optimise the disk space usage of the Nix store by hard-linking
   files with the same contents. */
static void
opOptimise(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    if (!opArgs.empty() || !opFlags.empty())
        throw UsageError("no arguments expected");

    aio.blockOn(store->optimiseStore());
}

/* Serve the nix store in a way usable by a restricted ssh user. */
static void
opServe(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    bool writeAllowed = false;
    for (auto & i : opFlags)
        if (i == "--write") writeAllowed = true;
        else throw UsageError("unknown flag '%1%'", i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    FdSource in(STDIN_FILENO);
    FdSink out(STDOUT_FILENO);

    /* Exchange the greeting. */
    unsigned int magic = readNum<unsigned>(in);
    if (magic != SERVE_MAGIC_1) throw Error("protocol mismatch");
    out << SERVE_MAGIC_2 << SERVE_PROTOCOL_VERSION;
    out.flush();
    ServeProto::Version clientVersion = readNum<unsigned>(in);

    ServeProto::ReadConn rconn {
        .from = in,
        .store = *store,
        .version = clientVersion,
    };
    ServeProto::WriteConn wconn {
        .store = *store,
        .version = clientVersion,
    };

    auto getBuildSettings = [&]() {
        // FIXME: changing options here doesn't work if we're
        // building through the daemon.
        verbosity = lvlError;
        settings.keepLog.override(false);
        settings.useSubstitutes.override(false);
        settings.maxSilentTime.override(readNum<unsigned>(in));
        settings.buildTimeout.override(readNum<unsigned>(in));
        if (GET_PROTOCOL_MINOR(clientVersion) >= 2)
            settings.maxLogSize.override(readNum<unsigned long>(in));
        if (GET_PROTOCOL_MINOR(clientVersion) >= 3) {
            auto nrRepeats = readNum<unsigned>(in);
            if (nrRepeats != 0) {
                throw Error("client requested repeating builds, but this is not currently implemented");
            }
            // Ignore 'enforceDeterminism'. It used to be true by
            // default, but also only never had any effect when
            // `nrRepeats == 0`.  We have already asserted that
            // `nrRepeats` in fact is 0, so we can safely ignore this
            // without doing something other than what the client
            // asked for.
            readNum<unsigned>(in);

            settings.runDiffHook.override(true);
        }
        if (GET_PROTOCOL_MINOR(clientVersion) >= 7) {
            settings.keepFailed.override((bool) readNum<unsigned>(in));
        }
    };

    while (true) {
        ServeProto::Command cmd;
        try {
            cmd = (ServeProto::Command) readNum<unsigned>(in);
        } catch (EndOfFile & e) {
            break;
        }

        switch (cmd) {

            case ServeProto::Command::QueryValidPaths: {
                bool lock = readNum<unsigned>(in);
                bool substitute = readNum<unsigned>(in);
                auto paths = ServeProto::Serialise<StorePathSet>::read(rconn);
                if (lock && writeAllowed)
                    for (auto & path : paths)
                        aio.blockOn(store->addTempRoot(path));

                if (substitute && writeAllowed) {
                    aio.blockOn(store->substitutePaths(paths));
                }

                auto valid = aio.blockOn(store->queryValidPaths(paths));
                out << ServeProto::write(wconn, valid);
                break;
            }

            case ServeProto::Command::QueryPathInfos: {
                auto paths = ServeProto::Serialise<StorePathSet>::read(rconn);
                // !!! Maybe we want a queryPathInfos?
                for (auto & i : paths) {
                    try {
                        auto info = aio.blockOn(store->queryPathInfo(i));
                        out << store->printStorePath(info->path);
                        out << ServeProto::write(wconn, static_cast<const UnkeyedValidPathInfo &>(*info));
                    } catch (InvalidPath &) {
                    }
                }
                out << "";
                break;
            }

            case ServeProto::Command::DumpStorePath:
                aio.blockOn(aio.blockOn(store->narFromPath(store->parseStorePath(readString(in))))
                                ->drainInto(out));
                break;

            case ServeProto::Command::ImportPaths: {
                if (!writeAllowed) throw Error("importing paths is not allowed");
                aio.blockOn(store->importPaths(in, NoCheckSigs)); // FIXME: should we skip sig checking?
                out << 1; // indicate success
                break;
            }

            case ServeProto::Command::ExportPaths: {
                readNum<unsigned>(in); // obsolete
                aio.blockOn(store->exportPaths(
                    ServeProto::Serialise<StorePathSet>::read(rconn), out
                ));
                break;
            }

            case ServeProto::Command::BuildPaths: {

                if (!writeAllowed) throw Error("building paths is not allowed");

                std::vector<StorePathWithOutputs> paths;
                for (auto & s : readStrings<Strings>(in))
                    paths.push_back(parsePathWithOutputs(*store, s));

                getBuildSettings();

                try {
                    MonitorFdHup monitor(in.fd);
                    aio.blockOn(store->buildPaths(toDerivedPaths(paths)));
                    out << 0;
                } catch (Error & e) {
                    assert(e.info().status);
                    out << e.info().status << e.msg();
                }
                break;
            }

            case ServeProto::Command::BuildDerivation: { /* Used by hydra-queue-runner. */

                if (!writeAllowed) throw Error("building paths is not allowed");

                auto drvPath = store->parseStorePath(readString(in));
                BasicDerivation drv;
                readDerivation(in, *store, drv, Derivation::nameFromPath(drvPath));

                getBuildSettings();

                MonitorFdHup monitor(in.fd);
                auto status = aio.blockOn(store->buildDerivation(drvPath, drv));

                out << ServeProto::write(wconn, status);
                break;
            }

            case ServeProto::Command::QueryClosure: {
                bool includeOutputs = readNum<unsigned>(in);
                StorePathSet closure;
                aio.blockOn(store->computeFSClosure(
                    ServeProto::Serialise<StorePathSet>::read(rconn),
                    closure,
                    false,
                    includeOutputs
                ));
                out << ServeProto::write(wconn, closure);
                break;
            }

            case ServeProto::Command::AddToStoreNar: {
                if (!writeAllowed) throw Error("importing paths is not allowed");

                auto path = readString(in);
                auto deriver = readString(in);
                ValidPathInfo info {
                    store->parseStorePath(path),
                    Hash::parseAny(readString(in), HashType::SHA256),
                };
                if (deriver != "")
                    info.deriver = store->parseStorePath(deriver);
                info.references = ServeProto::Serialise<StorePathSet>::read(rconn);
                info.registrationTime = readNum<time_t>(in);
                info.narSize = readNum<uint64_t>(in);
                info.ultimate = readBool(in);
                info.sigs = readStrings<StringSet>(in);
                info.ca = ContentAddress::parseOpt(readString(in));

                if (info.narSize == 0)
                    throw Error("narInfo is too old and missing the narSize field");

                struct SizedSource : Source
                {
                    Source & orig;
                    size_t remain;
                    SizedSource(Source & orig, size_t size) : orig(orig), remain(size) {}
                    size_t read(char * data, size_t len) override
                    {
                        if (this->remain <= 0) {
                            throw EndOfFile("sized: unexpected end-of-file");
                        }
                        len = std::min(len, this->remain);
                        size_t n = this->orig.read(data, len);
                        this->remain -= n;
                        return n;
                    }

                    size_t drainAll()
                    {
                        std::vector<char> buf(8192);
                        size_t sum = 0;
                        while (this->remain > 0) {
                            size_t n = read(buf.data(), buf.size());
                            sum += n;
                        }
                        return sum;
                    }
                };

                SizedSource sizedSource(in, info.narSize);
                AsyncSourceInputStream stream{sizedSource};

                aio.blockOn(store->addToStore(info, stream, NoRepair, NoCheckSigs));

                // consume all the data that has been sent before continuing.
                sizedSource.drainAll();

                out << 1; // indicate success

                break;
            }

            default:
                throw Error("unknown serve command %1%", cmd);
        }

        out.flush();
    }
}

static void opGenerateBinaryCacheKey(
    std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs
)
{
    for (auto & i : opFlags)
        throw UsageError("unknown flag '%1%'", i);

    if (opArgs.size() != 3) throw UsageError("three arguments expected");
    auto i = opArgs.begin();
    std::string keyName = *i++;
    std::string secretKeyFile = *i++;
    std::string publicKeyFile = *i++;

    auto secretKey = SecretKey::generate(keyName);

    writeFile(publicKeyFile, secretKey.toPublicKey().to_string());
    umask(0077);
    writeFile(secretKeyFile, secretKey.to_string());
}

static void
opVersion(std::shared_ptr<Store> store, AsyncIoRoot & aio, Strings opFlags, Strings opArgs)
{
    printVersion("nix-store");
}


/* Scan the arguments; find the operation, set global flags, put all
   other flags in a list, and put all other arguments in another
   list. */
static int main_nix_store(AsyncIoRoot & aio, std::string programName, Strings argv)
{
    {
        Strings opFlags, opArgs;
        Operation op = 0;
        bool readFromStdIn = false;
        std::string opName;
        bool showHelp = false;

        LegacyArgs(aio, programName, [&](Strings::iterator & arg, const Strings::iterator & end) {
            Operation oldOp = op;

            if (*arg == "--help")
                showHelp = true;
            else if (*arg == "--version")
                op = opVersion;
            else if (*arg == "--realise" || *arg == "--realize" || *arg == "-r") {
                op = opRealise;
                opName = "-realise";
            }
            else if (*arg == "--add" || *arg == "-A"){
                op = opAdd;
                opName = "-add";
            }
            else if (*arg == "--add-fixed") {
                op = opAddFixed;
                opName = arg->substr(1);
            }
            else if (*arg == "--print-fixed-path")
                op = opPrintFixedPath;
            else if (*arg == "--delete") {
                op = opDelete;
                opName = arg->substr(1);
            }
            else if (*arg == "--query" || *arg == "-q") {
                op = opQuery;
                opName = "-query";
            }
            else if (*arg == "--print-env") {
                op = opPrintEnv;
                opName = arg->substr(1);
            }
            else if (*arg == "--read-log" || *arg == "-l") {
                op = opReadLog;
                opName = "-read-log";
            }
            else if (*arg == "--dump-db") {
                op = opDumpDB;
                opName = arg->substr(1);
            }
            else if (*arg == "--load-db") {
                op = opLoadDB;
                opName = arg->substr(1);
            }
            else if (*arg == "--register-validity")
                op = opRegisterValidity;
            else if (*arg == "--check-validity")
                op = opCheckValidity;
            else if (*arg == "--gc") {
                op = opGC;
                opName = arg->substr(1);
            }
            else if (*arg == "--dump") {
                op = opDump;
                opName = arg->substr(1);
            }
            else if (*arg == "--restore") {
                op = opRestore;
                opName = arg->substr(1);
            }
            else if (*arg == "--export") {
                op = opExport;
                opName = arg->substr(1);
            }
            else if (*arg == "--import") {
                op = opImport;
                opName = arg->substr(1);
            }
            else if (*arg == "--init")
                op = opInit;
            else if (*arg == "--verify") {
                op = opVerify;
                opName = arg->substr(1);
            }
            else if (*arg == "--verify-path") {
                op = opVerifyPath;
                opName = arg->substr(1);
            }
            else if (*arg == "--repair-path") {
                op = opRepairPath;
                opName = arg->substr(1);
            }
            else if (*arg == "--optimise" || *arg == "--optimize") {
                op = opOptimise;
                opName = "-optimise";
            }
            else if (*arg == "--serve") {
                op = opServe;
                opName = arg->substr(1);
            }
            else if (*arg == "--generate-binary-cache-key") {
                op = opGenerateBinaryCacheKey;
                opName = arg->substr(1);
            }
            else if (*arg == "--add-root")
                gcRoot = absPath(getArg(*arg, arg, end));
            else if (*arg == "--stdin" && !isatty(STDIN_FILENO))
                readFromStdIn = true;
            else if (*arg == "--indirect")
                ;
            else if (*arg == "--no-output")
                noOutput = true;
            else if (*arg != "" && arg->at(0) == '-') {
                opFlags.push_back(*arg);
                if (*arg == "--max-freed" || *arg == "--max-links" || *arg == "--max-atime") /* !!! hack */
                    opFlags.push_back(getArg(*arg, arg, end));
            }
            else
                opArgs.push_back(*arg);

            if (readFromStdIn && op != opImport && op != opRestore && op != opServe) {
                 std::string word;
                 while (std::cin >> word) {
                       opArgs.emplace_back(std::move(word));
                 };
            }

            if (oldOp && oldOp != op)
                throw UsageError("only one operation may be specified");

            return true;
        }).parseCmdline(argv);

        if (showHelp) showManPage("nix-store" + opName);
        if (!op) throw UsageError("no operation specified");

        std::shared_ptr<Store> store;
        if (op != opDump && op != opRestore) /* !!! hack */
            store = aio.blockOn(openStore());

        op(store, aio, std::move(opFlags), std::move(opArgs));

        return 0;
    }
}

void registerLegacyNixStore() {
    LegacyCommandRegistry::add("nix-store", main_nix_store);
}

}
