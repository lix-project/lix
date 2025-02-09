#pragma once
///@file

#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"

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

bool lockFile(int fd, LockType lockType);
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

    FdLock(int fd, LockType lockType, bool wait, std::string_view waitMsg);

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
