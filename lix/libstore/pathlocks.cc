#include "lix/libstore/pathlocks.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/signals.hh"

#include <cerrno>

#include <fcntl.h>
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


void deleteLockFile(const Path & path, int fd)
{
    /* Get rid of the lock file.  Have to be careful not to introduce
       races.  Write a (meaningless) token to the file to indicate to
       other processes waiting on this lock that the lock is stale
       (deleted). */
    unlink(path.c_str());
    writeFull(fd, "d");
    /* Note that the result of unlink() is ignored; removing the lock
       file is an optimisation, not a necessity. */
}

static int convertLockType(LockType lockType)
{
    if (lockType == ltRead) return LOCK_SH;
    else if (lockType == ltWrite) return LOCK_EX;
    else abort();
}

bool lockFile(int fd, LockType lockType)
{
    int type = convertLockType(lockType);

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
    : deletePaths(false)
{
}


PathLocks::PathLocks(const PathSet & paths, const std::string & waitMsg)
    : deletePaths(false)
{
    lockPaths(paths, waitMsg);
}


bool PathLocks::lockPaths(const PathSet & paths,
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
            if (st.st_size != 0)
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
        if (deletePaths) deleteLockFile(i.second, i.first);

        if (close(i.first) == -1)
            printError(
                "error (ignored): cannot close lock file on '%1%'",
                i.second);

        debug("lock released on '%1%'", i.second);
    }

    fds.clear();
}


void PathLocks::setDeletion(bool deletePaths)
{
    this->deletePaths = deletePaths;
}


FdLock::FdLock(int fd, LockType lockType, bool wait, std::string_view waitMsg)
    : fd(fd)
{
    if (wait) {
        if (!tryLockFile(fd, lockType)) {
            printInfo("%s", waitMsg);
            acquired = lockFile(fd, lockType);
        }
    } else
        acquired = tryLockFile(fd, lockType);
}


}
