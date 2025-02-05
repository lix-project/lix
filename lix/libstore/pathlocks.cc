#include "lix/libstore/pathlocks.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/signals.hh"

#include <cerrno>

#include <fcntl.h>
#include <kj/common.h>
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

void lockFile(int fd, LockType lockType)
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
    // start a thread to lock the file synchronously, waiting for SIGUSR1 to signal
    // that the call was canceled. SIGUSR1 is already set aside for such signaling.

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
        pthread_kill(locker.native_handle(), SIGUSR1);
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

bool unsafeLockFileSingleThreaded(int fd, LockType lockType, std::chrono::seconds timeout)
{
    int type = convertLockType(lockType);

    auto old = signal(SIGALRM, [](int) {});
    alarm(timeout.count());
    KJ_DEFER({
        alarm(0);
        signal(SIGALRM, old);
    });

    while (flock(fd, type) != 0) {
        checkInterrupt();
        if (errno != EINTR)
            throw SysError("acquiring lock");
        else
            return false;
    }

    return true;
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


PathLocks::PathLocks()
{
}


PathLocks::PathLocks(const PathSet & paths, const std::string & waitMsg)
{
    lockPaths(paths, waitMsg);
}


bool PathLocks::lockPathsImpl(const PathSet & paths,
    const std::string & waitMsg, bool wait)
{
    assert(fds.empty());

    /* Note that `fds' is built incrementally so that the destructor
       will only release those locks that we have already acquired. */

    /* Acquire the lock for each path in sorted order. This ensures
       that locks are always acquired in the same order, thus
       preventing deadlocks. */
    for (auto & path : paths) {
        checkInterrupt();
        Path lockPath = path + ".lock";

        debug("locking path '%1%'", path);

        AutoCloseFD fd;

        while (1) {

            /* Open/create the lock file. */
            fd = openLockFile(lockPath, true);

            /* Acquire an exclusive lock. */
            if (!tryLockFile(fd.get(), ltWrite)) {
                if (wait) {
                    if (waitMsg != "") printError(waitMsg);
                    lockFile(fd.get(), ltWrite);
                } else {
                    /* Failed to lock this path; release all other
                       locks. */
                    unlock();
                    return false;
                }
            }

            debug("lock acquired on '%1%'", lockPath);

            /* Check that the lock file hasn't become stale (i.e.,
               hasn't been unlinked). */
            struct stat st;
            if (fstat(fd.get(), &st) == -1)
                throw SysError("statting lock file '%1%'", lockPath);
            if (st.st_nlink == 0)
                /* This lock file has been unlinked, so we're holding
                   a lock on a deleted file.  This means that other
                   processes may create and acquire a lock on
                   `lockPath', and proceed.  So we must retry. */
                debug("open lock file '%1%' has become stale", lockPath);
            else
                break;
        }

        /* Use borrow so that the descriptor isn't closed. */
        fds.push_back(FDPair(fd.release(), lockPath));
    }

    return true;
}


PathLocks::~PathLocks()
{
    try {
        unlock();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}


void PathLocks::unlock()
{
    for (auto & i : fds) {
        // delete the file. if another file descriptor is used to acquire a lock on
        // this file it will figure out that the file is stale once it calls stat()
        // and inspects the link count. if unlink fails we merely leave around some
        // stale lock file paths that can be reused or cleaned up by other threads.
        unlink(i.second.c_str());
        // clobber file contents for compatibility wither other nix implementations
        writeFull(i.first, "d");

        if (close(i.first) == -1) {
            printError("error (ignored): cannot close lock file on '%1%'", i.second);
        }

        debug("lock released on '%1%'", i.second);
    }

    fds.clear();
}


FdLock::FdLock(AutoCloseFD & fd, LockType lockType, DontWait)
{
    if (tryLockFile(fd.get(), lockType)) {
        this->fd.reset(&fd);
    }
}

FdLock::FdLock(AutoCloseFD & fd, LockType lockType, std::string_view waitMsg)
{
    if (!tryLockFile(fd.get(), lockType)) {
        printInfo("%s", waitMsg);
        lockFile(fd.get(), lockType);
        this->fd.reset(&fd);
    }
}

kj::Promise<Result<FdLock>>
FdLock::lockAsync(AutoCloseFD & fd, LockType lockType, std::string_view waitMsg)
try {
    if (!tryLockFile(fd.get(), lockType)) {
        printInfo("%s", waitMsg);
        TRY_AWAIT(lockFileAsyncInner(fd.get(), lockType));
    }
    co_return FdLock{fd};
} catch (...) {
    co_return result::current_exception();
}

}
