#pragma once
///@file

#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include <chrono>

namespace nix {

/**
 * Open (possibly create) a lock file and return the file descriptor.
 * -1 is returned if create is false and the lock could not be opened
 * because it doesn't exist.  Any other error throws an exception.
 */
AutoCloseFD openLockFile(const Path & path, bool create);

/**
 * Delete an open lock file.
 */
void deleteLockFile(const Path & path, int fd);

enum LockType { ltRead, ltWrite };

void lockFile(int fd, LockType lockType);
/**
 * Same as `lockFile`, but with a timeout. This timeout uses the POSIX `alarm`
 * facility and a `SIGALRM` handler. Using this function from multiple threads
 * in the same process is not safe: all `SIGALRM` handlers set previously will
 * be overwritten while this function is executing and are restored on return.
 */
bool unsafeLockFileSingleThreaded(int fd, LockType lockType, std::chrono::seconds timeout);
bool tryLockFile(int fd, LockType lockType);
void unlockFile(int fd);

class PathLocks
{
private:
    typedef std::pair<int, Path> FDPair;
    std::list<FDPair> fds;
    bool deletePaths;

public:
    PathLocks();
    PathLocks(const PathSet & paths,
        const std::string & waitMsg = "");
    bool lockPaths(const PathSet & _paths,
        const std::string & waitMsg = "",
        bool wait = true);
    ~PathLocks();
    void unlock();
    void setDeletion(bool deletePaths);
};

struct FdLock
{
    int fd;
    bool acquired = false;

    FdLock(int fd, LockType lockType);
    FdLock(int fd, LockType lockType, std::string_view waitMsg);

    ~FdLock()
    {
        try {
            if (acquired)
                unlockFile(fd);
        } catch (SysError &) {
            ignoreExceptionInDestructor();
        }
    }
};

}
