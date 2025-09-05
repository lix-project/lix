#include "lix/libstore/globals.hh"
#include "lix/libstore/local-store.hh"
#include "lix/libstore/pathlocks.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/unix-domain-socket.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/thread-name.hh"

#include <kj/async.h>
#include <queue>
#include <regex>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace nix {


constexpr static const std::string_view gcSocketPath = "/gc-socket/socket";
constexpr static const std::string_view gcRootsDir = "gcroots";


static void makeSymlink(const Path & link, const Path & target)
{
    /* Create directories up to `gcRoot'. */
    createDirs(dirOf(link));

    /* Create the new symlink. */
    Path tempLink = makeTempPath(link);
    unlink(tempLink.c_str()); // just in case; ignore errors
    createSymlink(target, tempLink);

    /* Atomically replace the old one. */
    renameFile(tempLink, link);
}


kj::Promise<Result<void>> LocalStore::addIndirectRoot(const Path & path)
try {
    std::string hash = hashString(HashType::SHA1, path).to_string(Base::Base32, false);
    Path realRoot = canonPath(fmt("%1%/%2%/auto/%3%", config().stateDir, gcRootsDir, hash));
    makeSymlink(realRoot, path);
    return {result::success()};
} catch (...) {
    return {result::current_exception()};
}


kj::Promise<Result<Path>> IndirectRootStore::addPermRoot(const StorePath & storePath, const Path & _gcRoot)
try {
    Path gcRoot(canonPath(_gcRoot));

    if (isInStore(gcRoot))
        throw Error(
                "creating a garbage collector root (%1%) in the Nix store is forbidden "
                "(are you running nix-build inside the store?)", gcRoot);

    /* Register this root with the garbage collector, if it's
       running. This should be superfluous since the caller should
       have registered this root yet, but let's be on the safe
       side. */
    TRY_AWAIT(addTempRoot(storePath));

    /* Don't clobber the link if it already exists and doesn't
       point to the Nix store. */
    if (pathExists(gcRoot) && (!isLink(gcRoot) || !isInStore(readLink(gcRoot))))
        throw Error("cannot create symlink '%1%'; already exists", gcRoot);
    makeSymlink(gcRoot, printStorePath(storePath));
    TRY_AWAIT(addIndirectRoot(gcRoot));

    co_return gcRoot;
} catch (...) {
    co_return result::current_exception();
}


void LocalStore::createTempRootsFile()
{
    if (auto fdTempRoots(_fdTempRoots.lock()); *fdTempRoots) {
        return;
    }

    /* Create the temporary roots file for this process. */
    while (true) {
        auto tmp = makeTempPath(fnTempRoots, ".tmp");
        AutoCloseFD fd{open(tmp.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600)};
        if (!fd && errno != EEXIST) {
            throw SysError("opening lock file '%1%'", tmp);
        }
        // if we can't lock it then GC must've found and deleted it, so we try again.
        // if we *can* lock it GC may have still deleted it, and rename will tell us.
        if (!tryLockFile(fd.get(), ltWrite)) {
            unlink(tmp.c_str()); // just to be sure it's gone
            continue;
        } else if (auto fdTempRoots(_fdTempRoots.lock()); *fdTempRoots) {
            if (unlink(tmp.c_str()) == -1) {
                throw SysError("deleting lock file '%1%'", tmp);
            }
            break;
        } else if (rename(tmp.c_str(), fnTempRoots.c_str()) == -1) {
            if (errno != ENOENT) {
                throw SysError("moving lock file '%1%'", tmp);
            }
        } else {
            debug("acquired write lock on '%s'", fnTempRoots);
            *fdTempRoots = std::move(fd);
            break;
        }
    }
}


kj::Promise<Result<void>> LocalStore::addTempRoot(const StorePath & path)
try {
    if (config().readOnly) {
      debug("Read-only store doesn't support creating lock files for temp roots, but nothing can be deleted anyways.");
      co_return result::success();
    }

    createTempRootsFile();

    /* Open/create the global GC lock file. */
    auto & fdGCLock = [&]() -> auto & {
        auto fdGCLock(_fdGCLock.lock());
        if (!*fdGCLock)
            *fdGCLock = openGCLock();
        return *fdGCLock;
    }();

 restart:
    /* Try to acquire a shared global GC lock (non-blocking). This
       only succeeds if the garbage collector is not currently
       running. */
    FdLock gcLock(fdGCLock, ltRead, FdLock::dont_wait);

    if (!gcLock.valid()) {
        /* We couldn't get a shared global GC lock, so the garbage
           collector is running. So we have to connect to the garbage
           collector and inform it about our root. */
        auto fdRootsSocket(_fdRootsSocket.lock());

        if (!*fdRootsSocket) {
            auto socketPath = config().stateDir.get() + gcSocketPath;
            debug("connecting to '%s'", socketPath);
            *fdRootsSocket = createUnixDomainSocket();
            try {
                nix::connect(fdRootsSocket->get(), socketPath);
            } catch (SysError & e) {
                /* The garbage collector may have exited or not
                   created the socket yet, so we need to restart. */
                if (e.errNo == ECONNREFUSED || e.errNo == ENOENT) {
                    debug("GC socket connection refused: %s", e.msg());
                    fdRootsSocket->close();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    goto restart;
                }
                throw;
            }
        }

        try {
            debug("sending GC root '%s'", printStorePath(path));
            writeFull(fdRootsSocket->get(), printStorePath(path) + "\n", false);
            char c;
            readFull(fdRootsSocket->get(), &c, 1);
            assert(c == '1');
            debug("got ack for GC root '%s'", printStorePath(path));
        } catch (SysError & e) {
            /* The garbage collector may have exited, so we need to
               restart. */
            if (e.errNo == EPIPE || e.errNo == ECONNRESET) {
                debug("GC socket disconnected");
                fdRootsSocket->close();
                goto restart;
            }
            throw;
        } catch (EndOfFile & e) {
            debug("GC socket disconnected");
            fdRootsSocket->close();
            goto restart;
        }
    }

    /* Record the store path in the temporary roots file so it will be
       seen by a future run of the garbage collector. */
    auto s = printStorePath(path) + '\0';
    writeFull(_fdTempRoots.lock()->get(), s);
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


static std::string censored = "{censored}";


void LocalStore::findTempRoots(Roots & tempRoots, bool censor)
{
    /* Read the `temproots' directory for per-process temporary root
       files. */
    for (auto & i : readDirectory(tempRootsDir)) {
        if (i.name[0] == '.') {
            // Ignore hidden files. Some package managers (notably portage) create
            // those to keep the directory alive.
            continue;
        }
        Path path = tempRootsDir + "/" + i.name;

        pid_t pid = std::stoi(i.name);

        debug("reading temporary root file '%1%'", path);
        AutoCloseFD fd(open(path.c_str(), O_CLOEXEC | O_RDWR, 0666));
        if (!fd) {
            /* It's okay if the file has disappeared. */
            if (errno == ENOENT) continue;
            throw SysError("opening temporary roots file '%1%'", path);
        }

        /* Try to acquire a write lock without blocking.  This can
           only succeed if the owning process has died.  In that case
           we don't care about its temporary roots. */
        if (tryLockFile(fd.get(), ltWrite)) {
            printInfo("removing stale temporary roots file '%1%'", path);
            unlink(path.c_str());
            writeFull(fd.get(), "d");
            continue;
        }

        /* Read the entire file. */
        auto contents = readFile(fd.get());

        /* Extract the roots. */
        std::string::size_type pos = 0, end;

        while ((end = contents.find((char) 0, pos)) != std::string::npos) {
            Path root(contents, pos, end - pos);
            debug("got temporary root '%s'", root);
            tempRoots[parseStorePath(root)].emplace(censor ? censored : fmt("{temp:%d}", pid));
            pos = end + 1;
        }
    }
}


kj::Promise<Result<void>>
LocalStore::findRoots(const Path & path, unsigned char type, Roots & roots)
try {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    auto foundRoot = [&](const Path & path, const Path & target) -> kj::Promise<Result<void>> {
        try {
            auto storePath = toStorePath(target).first;
            if (TRY_AWAIT(isValidPath(storePath)))
                roots[std::move(storePath)].emplace(path);
            else
                printInfo("skipping invalid root from '%1%' to '%2%'", path, target);
        } catch (BadStorePath &) {
        } catch (...) {
            co_return result::current_exception();
        }
        co_return result::success();
    };

    try {

        if (type == DT_UNKNOWN)
            type = getFileType(path);

        if (type == DT_DIR) {
            for (auto & i : readDirectory(path))
                TRY_AWAIT(findRoots(path + "/" + i.name, i.type, roots));
        }

        else if (type == DT_LNK) {
            Path target = readLink(path);
            if (isInStore(target))
                TRY_AWAIT(foundRoot(path, target));

            /* Handle indirect roots. */
            else {
                target = absPath(target, dirOf(path));
                if (!pathExists(target)) {
                    if (isInDir(path, config().stateDir + "/" + gcRootsDir + "/auto")) {
                        printInfo("removing stale link from '%1%' to '%2%'", path, target);
                        unlink(path.c_str());
                    }
                } else {
                    struct stat st2 = lstat(target);
                    if (!S_ISLNK(st2.st_mode)) co_return result::success();
                    Path target2 = readLink(target);
                    if (isInStore(target2)) TRY_AWAIT(foundRoot(target, target2));
                }
            }
        }

        else if (type == DT_REG) {
            auto storePath =
                maybeParseStorePath(config().storeDir + "/" + std::string(baseNameOf(path)));
            if (storePath && TRY_AWAIT(isValidPath(*storePath)))
                roots[std::move(*storePath)].emplace(path);
        }

    }

    catch (SysError & e) {
        /* We only ignore permanent failures. */
        if (e.errNo == EACCES || e.errNo == ENOENT || e.errNo == ENOTDIR)
            printInfo("cannot read potential root '%1%'", path);
        else
            throw;
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> LocalStore::findRootsNoTemp(Roots & roots, bool censor)
try {
    /* Process direct roots in {gcroots,profiles}. */
    TRY_AWAIT(findRoots(config().stateDir + "/" + gcRootsDir, DT_UNKNOWN, roots));
    TRY_AWAIT(findRoots(config().stateDir + "/profiles", DT_UNKNOWN, roots));

    /* Add additional roots returned by different platforms-specific
       heuristics.  This is typically used to add running programs to
       the set of roots (to prevent them from being garbage collected). */
    TRY_AWAIT(findRuntimeRoots(roots, censor));

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<Roots>> LocalStore::findRoots(bool censor)
try {
    Roots roots;
    TRY_AWAIT(findRootsNoTemp(roots, censor));

    findTempRoots(roots, censor);

    co_return roots;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> LocalStore::findPlatformRoots(UncheckedRoots & unchecked)
try {
    // N.B. This is (read: undertested!) fallback code only used for
    // non-Darwin, non-Linux platforms. Both major platforms have
    // platform-specific code in lix/libstore/platform/
    try {
        std::regex lsofRegex = regex::parse(R"(^n(/.*)$)");
        auto lsofLines = tokenizeString<std::vector<std::string>>(
            TRY_AWAIT(runProgram(LSOF, true, {"-n", "-w", "-F", "n"})), "\n"
        );
        for (const auto & line : lsofLines) {
            std::smatch match;
            if (std::regex_match(line, match, lsofRegex))
                unchecked[match[1]].emplace("{lsof}");
        }
    } catch (ExecError & e) {
        /* lsof not installed, lsof failed */
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> LocalStore::findRuntimeRoots(Roots & roots, bool censor)
try {
    UncheckedRoots unchecked;

    TRY_AWAIT(findPlatformRoots(unchecked));

    for (auto & [target, links] : unchecked) {
        if (!isInStore(target)) continue;
        try {
            auto path = toStorePath(target).first;
            if (!TRY_AWAIT(isValidPath(path))) continue;
            debug("got additional root '%1%'", printStorePath(path));
            if (censor)
                roots[path].insert(censored);
            else
                roots[path].insert(links.begin(), links.end());
        } catch (BadStorePath &) { }
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


struct GCLimitReached : BaseException { };


/**
 * Delegate class to expose just the operations required to perform GC on a store.
 */
class GCStoreDelegate {
    LocalStore const & store;

    public:
    GCStoreDelegate(LocalStore const & store) : store(store) {}

    std::optional<StorePath> maybeParseStorePath(std::string_view path) const {
        return store.maybeParseStorePath(path);
    }
};


/**
 * Class holding a server to receive new GC roots.
 */
class GCOperation {
    const GCStoreDelegate store;

    std::thread serverThread;
    Pipe shutdownPipe;

    AutoCloseFD fdServer;

    struct Shared
    {
        // The temp roots only store the hash part to make it easier to
        // ignore suffixes like '.lock', '.chroot' and '.check'.
        std::unordered_set<std::string> tempRoots;

        // Hash part of the store path currently being deleted, if
        // any.
        std::optional<std::string> pending;
    };

    void runServerThread();

    std::condition_variable wakeup;
    Sync<Shared> _shared;

    public:
    GCOperation(LocalStore const & store, Path stateDir) : store(store)
    {
        /* Start the server for receiving new roots. */
        shutdownPipe.create();

        auto socketPath = stateDir + gcSocketPath;
        createDirs(dirOf(socketPath));
        fdServer = createUnixDomainSocket(socketPath, 0666);

        makeNonBlocking(fdServer.get());

        serverThread = std::thread([this]() {
            setCurrentThreadName("gc server");
            runServerThread();
        });
    }

    void addTempRoot(std::string rootHashPart)
    {
        _shared.lock()->tempRoots.insert(rootHashPart);
    }

    void releasePending()
    {
        auto shared(_shared.lock());
        shared->pending.reset();
        wakeup.notify_all();
    }

    /**
     * Marks a path as pending deletion if it is not in tempRoots.
     *
     * Returns whether it was marked for deletion.
     */
    bool markPendingIfPresent(std::string const & hashPart)
    {
        auto shared(_shared.lock());
        if (shared->tempRoots.count(hashPart)) {
            return false;
        }
        shared->pending = hashPart;
        return true;
    }

    ~GCOperation();
};

void GCOperation::runServerThread()
{
    Sync<std::map<int, std::thread>> connections;

    Finally cleanup([&]() {
        debug("GC roots server shutting down");
        fdServer.close();
        while (true) {
            auto item = remove_begin(*connections.lock());
            if (!item) break;
            auto & [fd, thread] = *item;
            shutdown(fd, SHUT_RDWR);
            thread.join();
        }
    });

    while (true) {
        std::vector<struct pollfd> fds;
        fds.push_back({.fd = shutdownPipe.readSide.get(), .events = POLLIN});
        fds.push_back({.fd = fdServer.get(), .events = POLLIN});
        auto count = poll(fds.data(), fds.size(), -1);
        assert(count != -1);

        if (fds[0].revents)
            /* Parent is asking us to quit. */
            break;

        if (fds[1].revents) {
            /* Accept a new connection. */
            assert(fds[1].revents & POLLIN);
            AutoCloseFD fdClient{accept(fdServer.get(), nullptr, nullptr)};
            if (!fdClient) continue;

            debug("GC roots server accepted new client");

            /* Process the connection in a separate thread. */
            auto fdClient_ = fdClient.get();
            std::thread clientThread([&, fdClient = std::move(fdClient)]() {
                setCurrentThreadName("gc server connection");
                Finally cleanup([&]() {
                    auto conn(connections.lock());
                    auto i = conn->find(fdClient.get());
                    if (i != conn->end()) {
                        i->second.detach();
                        conn->erase(i);
                    }
                });

                /* On macOS, accepted sockets inherit the
                   non-blocking flag from the server socket, so
                   explicitly make it blocking. */
                try {
                    makeBlocking(fdClient.get());
                } catch (...) {
                    abort();
                }

                while (true) {
                    try {
                        auto path = readLine(fdClient.get());
                        auto storePath = store.maybeParseStorePath(path);
                        if (storePath) {
                            debug("got new GC root '%s'", path);
                            auto hashPart = std::string(storePath->hashPart());
                            auto shared(_shared.lock());
                            shared->tempRoots.insert(hashPart);
                            /* If this path is currently being
                               deleted, then we have to wait until
                               deletion is finished to ensure that
                               the client doesn't start
                               re-creating it before we're
                               done. FIXME: ideally we would use a
                               FD for this so we don't block the
                               poll loop. */
                            while (shared->pending == hashPart) {
                                debug("synchronising with deletion of path '%s'", path);
                                shared.wait(wakeup);
                            }
                        } else
                            printError("received garbage instead of a root from client");
                        writeFull(fdClient.get(), "1", false);
                    } catch (Error & e) {
                        debug("reading GC root from client: %s", e.msg());
                        break;
                    }
                }
            });

            connections.lock()->insert({fdClient_, std::move(clientThread)});
        }
    }
}

GCOperation::~GCOperation()
{
    writeFull(shutdownPipe.writeSide.get(), "x", false);
    {
        auto shared(_shared.lock());
        wakeup.notify_all();
    }
    if (serverThread.joinable()) serverThread.join();
}


kj::Promise<Result<void>> LocalStore::collectGarbage(const GCOptions & options, GCResults & results)
try {
    bool deleteSpecific = options.action == GCOptions::gcDeleteSpecific || options.action == GCOptions::gcTryDeleteSpecific;
    bool shouldDelete = options.action == GCOptions::gcDeleteDead || deleteSpecific;
    bool gcKeepOutputs = settings.gcKeepOutputs;
    bool gcKeepDerivations = settings.gcKeepDerivations;

    StorePathSet roots, dead, alive;

    /* Using `--ignore-liveness' with `--delete' can have unintended
       consequences if `keep-outputs' or `keep-derivations' are true
       (the garbage collector will recurse into deleting the outputs
       or derivers, respectively).  So disable them. */
    if (deleteSpecific && options.ignoreLiveness) {
        gcKeepOutputs = false;
        gcKeepDerivations = false;
    }

    if (shouldDelete)
        deletePath(reservedSpacePath);

    /* Acquire the global GC root. Note: we don't use fdGCLock
       here because then in auto-gc mode, another thread could
       downgrade our exclusive lock. */
    auto fdGCLock = openGCLock();
    FdLock gcLock = TRY_AWAIT(
        FdLock::lockAsync(fdGCLock, ltWrite, "waiting for the big garbage collector lock...")
    );

    /* Synchronisation point to test ENOENT handling in
       addTempRoot(), see tests/gc-non-blocking.sh. */
    if (auto p = getEnv("_NIX_TEST_GC_SYNC_1"))
        readFile(*p);

    GCOperation gcServer {*this, config().stateDir.get()};

    /* Find the roots.  Since we've grabbed the GC lock, the set of
       permanent roots cannot increase now. */
    printInfo("finding garbage collector roots...");
    Roots rootMap;
    if (!options.ignoreLiveness)
        TRY_AWAIT(findRootsNoTemp(rootMap, true));

    for (auto & i : rootMap) roots.insert(i.first);

    /* Read the temporary roots created before we acquired the global
       GC root. Any new roots will be sent to our socket. */
    Roots tempRoots;
    findTempRoots(tempRoots, true);
    for (auto & root : tempRoots) {
        gcServer.addTempRoot(std::string(root.first.hashPart()));
        roots.insert(root.first);
    }

    /* Synchronisation point for testing, see tests/functional/gc-non-blocking.sh. */
    if (auto p = getEnv("_NIX_TEST_GC_SYNC_2"))
        readFile(*p);

    /* Helper function that deletes a path from the store and throws
       GCLimitReached if we've deleted enough garbage. */
    auto deleteFromStore = [&](std::string_view baseName)
    {
        Path path = config().storeDir + "/" + std::string(baseName);
        Path realPath = config().realStoreDir + "/" + std::string(baseName);

        /* There may be temp directories in the store that are still in use
           by another process. We need to be sure that we can acquire an
           exclusive lock before deleting them. */
        if (baseName.find("tmp-", 0) == 0) {
            AutoCloseFD tmpDirFd{open(realPath.c_str(), O_RDONLY | O_DIRECTORY)};
            if (tmpDirFd.get() == -1 || !tryLockFile(tmpDirFd.get(), ltWrite)) {
                debug("skipping locked tempdir '%s'", realPath);
                return;
            }
        }

        printInfo("deleting '%1%'", path);

        results.paths.insert(path);

        uint64_t bytesFreed;
        deletePath(realPath, bytesFreed);
        results.bytesFreed += bytesFreed;

        if (results.bytesFreed > options.maxFreed) {
            printInfo("deleted more than %d bytes; stopping", options.maxFreed);
            throw GCLimitReached();
        }
    };

    std::map<StorePath, StorePathSet> referrersCache;

    /* Helper function that visits all paths reachable from `start`
       via the referrers edges and optionally derivers and derivation
       output edges. If none of those paths are roots, then all
       visited paths are garbage and are deleted. */
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    auto deleteReferrersClosure = [&](const StorePath & start) -> kj::Promise<Result<void>> {
        try {
            StorePathSet visited;
            std::queue<StorePath> todo;

            /* Wake up any GC client waiting for deletion of the paths in
               'visited' to finish. */
            Finally releasePending([&]() {
                gcServer.releasePending();
            });

            auto enqueue = [&](const StorePath & path) {
                if (visited.insert(path).second)
                    todo.push(path);
            };

            enqueue(start);

            while (auto path = pop(todo)) {
                checkInterrupt();

                /* Bail out if we've previously discovered that this path
                   is alive. */
                if (alive.count(*path)) {
                    alive.insert(start);
                    co_return result::success();
                }

                /* If we've previously deleted this path, we don't have to
                   handle it again. */
                if (dead.count(*path)) continue;

                // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
                auto markAlive = [&]() -> kj::Promise<Result<void>>
                {
                    try {
                        alive.insert(*path);
                        alive.insert(start);
                        try {
                            StorePathSet closure;
                            TRY_AWAIT(computeFSClosure(*path, closure,
                                /* flipDirection */ false, gcKeepOutputs, gcKeepDerivations));
                            for (auto & p : closure)
                                alive.insert(p);
                        } catch (InvalidPath &) { }
                        co_return result::success();
                    } catch (...) {
                        co_return result::current_exception();
                    }
                };

                /* If this is a root, bail out. */
                if (roots.count(*path)) {
                    debug("cannot delete '%s' because it's a root", printStorePath(*path));
                    TRY_AWAIT(markAlive());
                    co_return result::success();
                }

                if (!gcServer.markPendingIfPresent(std::string(path->hashPart()))) {
                    debug("cannot delete '%s' because it's a temporary root", printStorePath(*path));
                    TRY_AWAIT(markAlive());
                    co_return result::success();
                }

                if (TRY_AWAIT(isValidPath(*path))) {

                    /* Visit the referrers of this path. */
                    auto i = referrersCache.find(*path);
                    if (i == referrersCache.end()) {
                        StorePathSet referrers;
                        TRY_AWAIT(queryReferrers(*path, referrers));
                        referrersCache.emplace(*path, std::move(referrers));
                        i = referrersCache.find(*path);
                    }
                    for (auto & p : i->second)
                        enqueue(p);

                    /* If keep-derivations is set and this is a
                       derivation, then visit the derivation outputs. */
                    if (gcKeepDerivations && path->isDerivation()) {
                        for (auto & [name, outPath] :
                             TRY_AWAIT(queryDerivationOutputMap(*path)))
                        {
                            if (TRY_AWAIT(isValidPath(outPath)) &&
                                TRY_AWAIT(queryPathInfo(outPath))->deriver == *path)
                                enqueue(outPath);
                        }
                    }

                    /* If keep-outputs is set, then visit the derivers. */
                    if (gcKeepOutputs) {
                        auto derivers = TRY_AWAIT(queryValidDerivers(*path));
                        for (auto & i : derivers)
                            enqueue(i);
                    }
                }
            }

            for (auto & path : TRY_AWAIT(topoSortPaths(visited))) {
                if (!dead.insert(path).second) continue;
                if (shouldDelete) {
                    try {
                        TRY_AWAIT(invalidatePathChecked(path));
                        deleteFromStore(path.to_string());
                        referrersCache.erase(path);
                    } catch (PathInUse &) {
                        // References to upstream "bugs":
                        // https://github.com/NixOS/nix/issues/11923
                        // https://git.lix.systems/lix-project/lix/issues/621
                        printInfo("Skipping deletion of path '%1%' because it is now in use, preventing its removal.", printStorePath(path));
                    }
                }
            }
            co_return result::success();
        } catch (...) {
            co_return result::current_exception();
        }
    };

    PathSet kept;
    /* Either delete all garbage paths, or just the specified
       paths (for gcDeleteSpecific and gcTryDeleteSpecific). */
    if (deleteSpecific) {
        for (auto & i : options.pathsToDelete) {
            TRY_AWAIT(deleteReferrersClosure(i));
            if (!dead.count(i)) {
                std::string path(i.to_string());
                kept.insert(path);
                results.kept.insert(path);
            }
        }
        if (!kept.empty()) {
            printTalkative("Paths not deleted because they are still referenced by GC roots:");
            for (auto &path: kept) {
                printTalkative("%1%", Uncolored(path));
            }
        }

    } else if (options.maxFreed > 0) {

        if (shouldDelete)
            printInfo("deleting garbage...");
        else
            printInfo("determining live/dead paths...");

        try {
            AutoCloseDir dir(opendir(config().realStoreDir.get().c_str()));
            if (!dir) throw SysError("opening directory '%1%'", config().realStoreDir);

            /* Read the store and delete all paths that are invalid or
               unreachable. We don't use readDirectory() here so that
               GCing can start faster. */
            auto linksName = baseNameOf(linksDir);
            Paths entries;
            struct dirent * dirent;
            while (errno = 0, dirent = readdir(dir.get())) {
                checkInterrupt();
                std::string name = dirent->d_name;
                if (name == "." || name == ".." || name == linksName) continue;

                if (auto storePath = maybeParseStorePath(config().storeDir + "/" + name))
                    TRY_AWAIT(deleteReferrersClosure(*storePath));
                else
                    deleteFromStore(name);

            }
        } catch (GCLimitReached & e) {
        }
    }

    if (options.action == GCOptions::gcReturnLive) {
        for (auto & i : alive)
            results.paths.insert(printStorePath(i));
        co_return result::success();
    }

    if (options.action == GCOptions::gcReturnDead) {
        for (auto & i : dead)
            results.paths.insert(printStorePath(i));
        co_return result::success();
    }

    /* Unlink all files in /nix/store/.links that have a link count of 1,
       which indicates that there are no other links and so they can be
       safely deleted.  FIXME: race condition with optimisePath(): we
       might see a link count of 1 just before optimisePath() increases
       the link count. */
    if (options.action == GCOptions::gcDeleteDead || deleteSpecific) {
        printInfo("deleting unused links...");

        AutoCloseDir dir(opendir(linksDir.c_str()));
        if (!dir) throw SysError("opening directory '%1%'", linksDir);

        int64_t actualSize = 0, unsharedSize = 0;

        struct dirent * dirent;
        while (errno = 0, dirent = readdir(dir.get())) {
            checkInterrupt();
            std::string name = dirent->d_name;
            if (name == "." || name == "..") continue;
            Path path = linksDir + "/" + name;

            auto st = lstat(path);

            if (st.st_nlink != 1) {
                actualSize += st.st_size;
                unsharedSize += (st.st_nlink - 1) * st.st_size;
                continue;
            }

            printMsg(lvlTalkative, "deleting unused link '%1%'", path);

            if (unlink(path.c_str()) == -1)
                throw SysError("deleting '%1%'", path);

            /* Do not accound for deleted file here. Rely on deletePath()
               accounting.  */
        }

        struct stat st;
        if (stat(linksDir.c_str(), &st) == -1)
            throw SysError("statting '%1%'", linksDir);
        int64_t overhead = st.st_blocks * 512ULL;

        printInfo("note: currently hard linking saves %.2f MiB",
            ((unsharedSize - actualSize - overhead) / (1024.0 * 1024.0)));
    }
    if (options.action == GCOptions::gcDeleteSpecific && !kept.empty()) {
        std::ostringstream pathSummary;
        for (auto const [n, path]: enumerate(kept)) {
            pathSummary << "\n  " << path;
            const int summaryThreshold = 10;
            if (n >= summaryThreshold) {
                pathSummary << "\nand " << kept.size() - summaryThreshold << " others.";
                break;
            }
        }
        throw Error(
            "Cannot delete some of the given paths because they are still alive. "
            "Paths not deleted:"
            "%1%"
            "\nTo find out why, use nix-store --query --roots and nix-store --query --referrers."
            ,
            pathSummary.str()
        );
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> LocalStore::autoGC(bool sync)
try {
    static auto fakeFreeSpaceFile = getEnv("_NIX_TEST_FREE_SPACE_FILE");

    auto getAvail = [this]() -> uint64_t {
        if (fakeFreeSpaceFile)
            return std::stoll(readFile(*fakeFreeSpaceFile));

        struct statvfs st;
        if (statvfs(config().realStoreDir.get().c_str(), &st))
            throw SysError("getting filesystem info about '%s'", config().realStoreDir);

        return (uint64_t) st.f_bavail * st.f_frsize;
    };

    auto pfp = kj::newPromiseAndCrossThreadFulfiller<void>();

    {
        auto state(_gcState.lock());
        state->gcWaiters.push_back(std::move(pfp.fulfiller));

        if (state->gcRunning) {
            debug("waiting for auto-GC to finish");
            goto sync;
        }

        auto now = std::chrono::steady_clock::now();

        if (now < state->lastGCCheck + std::chrono::seconds(settings.minFreeCheckInterval)) {
            co_return result::success();
        }

        auto avail = getAvail();

        state->lastGCCheck = now;

        if (avail >= settings.minFree || avail >= settings.maxFree) co_return result::success();

        if (avail > state->availAfterGC * 0.97) co_return result::success();

        state->gcRunning = true;

        std::promise<void> promise;
        state->gcFuture = promise.get_future();

        std::thread([promise{std::move(promise)}, this, avail, getAvail]() mutable {
            setCurrentThreadName("auto gc");

            try {

                /* Wake up any threads waiting for the auto-GC to finish. */
                Finally wakeup([&]() {
                    auto state(_gcState.lock());
                    state->gcRunning = false;
                    state->lastGCCheck = std::chrono::steady_clock::now();
                    promise.set_value();
                    for (auto & waiter : state->gcWaiters) {
                        waiter->fulfill();
                    }
                    state->gcWaiters.clear();
                });

                AsyncIoRoot aio;

                GCOptions options;
                options.maxFreed = settings.maxFree - avail;

                printInfo("running auto-GC to free %d bytes", options.maxFreed);

                GCResults results;

                aio.blockOn(collectGarbage(options, results));

                _gcState.lock()->availAfterGC = getAvail();

            } catch (...) {
                // FIXME: we could propagate the exception to the
                // future, but we don't really care. (what??)
                ignoreExceptionInDestructor();
            }

        }).detach();
    }

 sync:
    // Wait for the future outside of the state lock.
    if (sync) co_await pfp.promise;

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


}
