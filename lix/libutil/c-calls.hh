#pragma once
/**
 * @file
 *
 * NUL-safe wrappers for C functions.
 */

#include "file-descriptor.hh"
#include "file-system.hh"
#include <grp.h>
#include <sys/statvfs.h>

namespace nix {

class CString
{
    friend CString requireCString(const std::string & s);

    const std::string * s;

    explicit CString(const std::string & s) : s(&s) {}

public:
    operator const char *() const
    {
        return s->c_str();
    }

    const char * asCStr() const
    {
        return *this;
    }
};

CString requireCString(const std::string & s);
}

namespace nix::sys {

AutoCloseFD open(const std::string & path, int flags);
AutoCloseFD open(const std::string & path, int flags, mode_t mode);

AutoCloseFD openat(int dir, const std::string & path, int flags);
AutoCloseFD openat(int dir, const std::string & path, int flags, mode_t mode);

AutoCloseDir opendir(const std::string & path);

[[nodiscard]]
int mkdir(const std::string & path, mode_t mode);

[[nodiscard]]
int lstat(const std::string & path, struct ::stat * st);

[[nodiscard]]
int stat(const std::string & path, struct ::stat * st);

[[nodiscard]]
int unlink(const std::string & path);

[[nodiscard]]
int access(const std::string & path, int mode);

[[nodiscard]]
int chmod(const std::string & path, mode_t mode);

[[nodiscard]]
int chown(const std::string & path, uid_t uid, gid_t gid);

[[nodiscard]]
int fchownat(int dir, const std::string & path, uid_t uid, gid_t gid, int flags);

[[nodiscard]]
int lchown(const std::string & path, uid_t uid, gid_t gid);

[[nodiscard]]
int rename(const std::string & oldPath, const std::string & newPath);

[[nodiscard]]
int utimes(const std::string & path, const struct ::timeval times[2]);
[[nodiscard]]
int lutimes(const std::string & path, const struct ::timeval times[2]);

[[nodiscard]]
int link(const std::string & oldPath, const std::string & newPath);

[[nodiscard]]
int symlink(const std::string & target, const std::string & linkpath);

[[nodiscard]]
int unlinkat(int dir, const std::string & path, int flags);

[[nodiscard]]
int remove(const std::string & path);

[[nodiscard]]
int rmdir(const std::string & path);

[[nodiscard]]
int fstatat(int dir, const std::string & path, struct ::stat * st, int flags);

[[nodiscard]]
int fchmodat(int dir, const std::string & path, mode_t mode, int flags);

[[nodiscard]]
int statvfs(const std::string & path, struct ::statvfs * st);

#if __linux__
[[nodiscard]]
int mount(
    const std::string & source,
    const std::string & target,
    const std::string & filesystemtype,
    unsigned long mountflags,
    const void * data
);

[[nodiscard]]
ssize_t llistxattr(const std::string & path, char * list, size_t size);

[[nodiscard]]
ssize_t lremovexattr(const std::string & path, const std::string & name);

[[nodiscard]]
ssize_t getxattr(const std::string & path, const std::string & name, void * value, size_t size);
#endif

[[nodiscard]]
int chdir(const std::string & path);

[[nodiscard]]
int chroot(const std::string & path);

AutoCloseFD mkstemp(std::string & path);

[[nodiscard]]
ssize_t readlink(const std::string & path, char * buf, size_t bufsiz);

int execv(const std::string & path, const std::list<std::string> & argv);
int execvp(const std::string & path, const std::list<std::string> & argv);
int execve(
    const std::string & path,
    const std::list<std::string> & argv,
    const std::list<std::string> & envp
);

char * getenv(const std::string & name);

[[nodiscard]]
int setenv(const std::string & name, const std::string & value, int overwrite);

[[nodiscard]]
int unsetenv(const std::string & name);

struct group * getgrnam(const std::string & name);

struct Passwd
{
    std::string pw_name;
    std::string pw_passwd;
    uid_t pw_uid;
    gid_t pw_gid;
    std::string pw_gecos;
    std::string pw_dir;
    std::string pw_shell;
};

std::optional<Passwd> getpwnam(const std::string & name);
}
