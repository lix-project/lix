#include "lix/libstore/pathlocks.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/types.hh"

#include <cerrno>

#include <fcntl.h>
#include <kj/common.h>
#include <optional>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>


namespace nix {


AutoCloseFD openLockFile(const Path & path, bool create)
{
    AutoCloseFD fd{open(path.c_str(), O_CLOEXEC | O_RDWR | (create ? O_CREAT : 0), 0600)};
    if (!fd && (create || errno != ENOENT))
        throw SysError("opening lock file '%1%'", path);

    return fd;
}


static int convertLockType(LockType lockType)
{
    if (lockType == ltRead) return LOCK_SH;
    else if (lockType == ltWrite) return LOCK_EX;
    else abort();
}

void lockFile(int fd, LockType lockType, NeverAsync)
{
    int type = convertLockType(lockType);

    while (flock(fd, type) != 0) {
        checkInterrupt();
        if (errno != EINTR)
            throw SysError("acquiring lock");
    }
}

static kj::Promise<Result<void>> lockFileAsyncInner(int fd, LockType lockType)
try {
    // start a thread to lock the file synchronously, waiting for an
    // INTERRUPT_NOTIFY_SIGNAL to signal that the call was canceled.

    int type = convertLockType(lockType);
    auto pfp = kj::newPromiseAndCrossThreadFulfiller<Result<void>>();

    std::thread locker([&] {
        while (flock(fd, type) != 0 && pfp.fulfiller->isWaiting()) {
            if (errno != EINTR) {
                pfp.fulfiller->fulfill(std::make_exception_ptr(SysError("acquiring lock")));
                return;
            }
        }
        pfp.fulfiller->fulfill(result::success());
    });
    auto cancel = kj::defer([&] {
        pthread_kill(locker.native_handle(), INTERRUPT_NOTIFY_SIGNAL);
        locker.join();
    });

    TRY_AWAIT(makeInterruptible(std::move(pfp.promise)));
    locker.join();
    cancel.cancel();
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> lockFileAsync(int fd, LockType lockType)
try {
    if (tryLockFile(fd, lockType)) {
        return {result::success()};
    }

    return lockFileAsyncInner(fd, lockType);
} catch (...) {
    return {result::current_exception()};
}

bool tryLockFile(int fd, LockType lockType)
{
    int type = convertLockType(lockType);

    while (flock(fd, type | LOCK_NB) != 0) {
        checkInterrupt();
        if (errno == EWOULDBLOCK) return false;
        if (errno != EINTR)
            throw SysError("acquiring lock");
    }

    return true;
}

void unlockFile(int fd)
{
    while (flock(fd, LOCK_UN) != 0) {
        if (errno != EINTR) {
            throw SysError("releasing lock");
        }
    }
}


static bool isPathLockValid(AutoCloseFD & fd, const Path & lockPath)
{
    /* Check that the lock file hasn't become stale (i.e.,
       hasn't been unlinked). */
    struct stat st;
    if (fstat(fd.get(), &st) == -1)
        throw SysError("statting lock file '%1%'", lockPath);
    if (st.st_nlink == 0) {
        /* This lock file has been unlinked, so we're holding
           a lock on a deleted file.  This means that other
           processes may create and acquire a lock on
           `lockPath', and proceed.  So we must retry. */
        debug("open lock file '%1%' has become stale", lockPath);
        return false;
    } else {
        return true;
    }
}

std::optional<PathLock>
PathLock::lockImpl(const Path & path, std::string_view waitMsg, bool wait, NeverAsync)
{
    Path lockPath = path + ".lock";

    debug("locking path '%1%'", path);

    while (1) {

        /* Open/create the lock file. */
        auto fd = openLockFile(lockPath, true);

        /* Acquire an exclusive lock. */
        if (!tryLockFile(fd.get(), ltWrite)) {
            if (wait) {
                if (waitMsg != "") {
                    printError("%1%", Uncolored(waitMsg));
                }
                lockFile(fd.get(), ltWrite);
            } else {
                return std::nullopt;
            }
        }

        debug("lock acquired on '%1%'", lockPath);
        if (isPathLockValid(fd, lockPath))
            return PathLock{std::move(fd), lockPath};
    }
}

kj::Promise<Result<PathLock>> lockPathAsync(const Path & path, std::string_view waitMsg)
try {
    Path lockPath = path + ".lock";
    debug("locking path '%1%'", path);

    while (1) {
        auto fd = openLockFile(lockPath, true);
        TRY_AWAIT(lockFileAsync(fd.get(), ltWrite));
        debug("lock acquired on '%1%'", lockPath);
        if (isPathLockValid(fd, lockPath))
            co_return PathLock{std::move(fd), lockPath};
    }
} catch (...) {
    co_return result::current_exception();
}

PathLock lockPath(const Path & path, std::string_view waitMsg, NeverAsync)
{
    return std::move(*PathLock::lockImpl(path, waitMsg, true));
}

std::optional<PathLock> tryLockPath(const Path & path)
{
    return PathLock::lockImpl(path, "", false, always_progresses);
}

static std::optional<PathLocks>
lockPathsImpl(const PathSet & paths, std::string_view waitMsg, bool wait, NeverAsync = {})
{
    PathLocks result;

    /* Acquire the lock for each path in sorted order. This ensures
       that locks are always acquired in the same order, thus
       preventing deadlocks. */
    for (auto & path : paths) {
        if (wait) {
            result.push_back(lockPath(path, waitMsg));
        } else if (auto p = tryLockPath(path); p) {
            result.push_back(std::move(*p));
        } else {
            return std::nullopt;
        }
    }

    return result;
}

PathLocks lockPaths(const PathSet & paths, std::string_view waitMsg, NeverAsync)
{
    return std::move(*lockPathsImpl(paths, waitMsg, true));
}

std::optional<PathLocks> tryLockPaths(const PathSet & paths)
{
    return lockPathsImpl(paths, "", false, always_progresses);
}


PathLock::~PathLock()
{
    try {
        unlock();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}


void PathLock::unlock()
{
    if (fd) {
        // delete the file. if another file descriptor is used to acquire a lock on
        // this file it will figure out that the file is stale once it calls stat()
        // and inspects the link count. if unlink fails we merely leave around some
        // stale lock file paths that can be reused or cleaned up by other threads.
        unlink(path.c_str());
        // clobber file contents for compatibility wither other nix implementations
        writeFull(fd.get(), "d");

        if (close(fd.release()) == -1) {
            printError("error (ignored): cannot close lock file on '%1%'", path);
        }

        debug("lock released on '%1%'", path);
    }
}


FdLock::FdLock(AutoCloseFD & fd, LockType lockType, DontWait)
{
    if (tryLockFile(fd.get(), lockType)) {
        this->fd.reset(&fd);
    }
}

FdLock::FdLock(AutoCloseFD & fd, LockType lockType, std::string_view waitMsg, NeverAsync)
{
    if (!tryLockFile(fd.get(), lockType)) {
        printInfo("%s", Uncolored(waitMsg));
        lockFile(fd.get(), lockType);
        this->fd.reset(&fd);
    }
}

kj::Promise<Result<FdLock>>
FdLock::lockAsync(AutoCloseFD & fd, LockType lockType, std::string_view waitMsg)
try {
    if (!tryLockFile(fd.get(), lockType)) {
        printInfo("%s", Uncolored(waitMsg));
        TRY_AWAIT(lockFileAsyncInner(fd.get(), lockType));
    }
    co_return FdLock{fd};
} catch (...) {
    co_return result::current_exception();
}

}
