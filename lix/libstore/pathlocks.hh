#pragma once
///@file

#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/types.hh"
#include <chrono>
#include <kj/async.h>
#include <kj/common.h>

namespace nix {

/**
 * Open (possibly create) a lock file and return the file descriptor.
 * -1 is returned if create is false and the lock could not be opened
 * because it doesn't exist.  Any other error throws an exception.
 */
AutoCloseFD openLockFile(const Path & path, bool create);

enum LockType { ltRead, ltWrite };

void lockFile(int fd, LockType lockType, NeverAsync = {});
kj::Promise<Result<void>> lockFileAsync(int fd, LockType lockType);
bool tryLockFile(int fd, LockType lockType);
void unlockFile(int fd);

class PathLock
{
    friend kj::Promise<Result<PathLock>> lockPathAsync(const Path & path, std::string_view waitMsg);
    friend PathLock lockPath(const Path & path, std::string_view waitMsg, NeverAsync);
    friend std::optional<PathLock> tryLockPath(const Path & path);

    AutoCloseFD fd;
    Path path;

    PathLock(AutoCloseFD fd, const Path & path): fd(std::move(fd)), path(path) {}

    static std::optional<PathLock>
    lockImpl(const Path & path, std::string_view waitMsg, bool wait, NeverAsync = {});

public:
    PathLock(PathLock &&) = default;
    PathLock & operator=(PathLock &&) = default;
    ~PathLock();

    void unlock();
};

kj::Promise<Result<PathLock>> lockPathAsync(const Path & path, std::string_view waitMsg = "");
PathLock lockPath(const Path & path, std::string_view waitMsg = "", NeverAsync = {});
std::optional<PathLock> tryLockPath(const Path & path);

using PathLocks = std::list<PathLock>;

PathLocks lockPaths(const PathSet & paths, std::string_view waitMsg = "", NeverAsync = {});
std::optional<PathLocks> tryLockPaths(const PathSet & paths);

class FdLock
{
    struct Unlocker
    {
        void operator()(AutoCloseFD * fd)
        {
            try {
                unlockFile(fd->get());
            } catch (SysError &) {
                ignoreExceptionInDestructor();
            }
        }
    };

    std::unique_ptr<AutoCloseFD, Unlocker> fd;

    explicit FdLock(AutoCloseFD & fd): fd(&fd) {}

public:
    static constexpr struct DontWait { explicit DontWait() = default; } dont_wait;

    FdLock(AutoCloseFD & fd, LockType lockType, DontWait);
    FdLock(AutoCloseFD & fd, LockType lockType, std::string_view waitMsg, NeverAsync = {});

    static kj::Promise<Result<FdLock>>
    lockAsync(AutoCloseFD & fd, LockType lockType, std::string_view waitMsg);

    bool valid() const { return bool(fd); }
};

}
