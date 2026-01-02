#include <cerrno>
#include <grp.h>
#include <optional>
#include <pwd.h>
#include <string>
#include <sys/mount.h>
#include <sys/xattr.h>

#include "c-calls.hh"
#include "error.hh"
#include "strings.hh"

namespace nix {
CString requireCString(const std::string & s)
{
    if (s.contains('\0')) {
        std::string p{s};
        for (auto pos = p.find('\0'); pos != p.npos; pos = p.find('\0')) {
            p.replace(pos, 1, "␀");
        }
        throw Error(
            "string %s that contains NUL bytes was used in a place that doesn't allow this", p
        );
    }
    return CString{s};
}
}

namespace nix::sys {

AutoCloseFD open(const std::string & path, int flags)
{
    return AutoCloseFD{::open(requireCString(path), flags)};
}

AutoCloseFD open(const std::string & path, int flags, mode_t mode)
{
    return AutoCloseFD{::open(requireCString(path), flags, mode)};
}

AutoCloseFD openat(int dir, const std::string & path, int flags)
{
    return AutoCloseFD{::openat(dir, requireCString(path), flags)};
}

AutoCloseFD openat(int dir, const std::string & path, int flags, mode_t mode)
{
    return AutoCloseFD{::openat(dir, requireCString(path), flags, mode)};
}

AutoCloseDir opendir(const std::string & path)
{
    return AutoCloseDir{::opendir(requireCString(path))};
}

int mkdir(const std::string & path, mode_t mode)
{
    return ::mkdir(requireCString(path), mode);
}

int lstat(const std::string & path, struct ::stat * st)
{
    return ::lstat(requireCString(path), st);
}

int stat(const std::string & path, struct ::stat * st)
{
    return ::stat(requireCString(path), st);
}

int unlink(const std::string & path)
{
    return ::unlink(requireCString(path));
}

int access(const std::string & path, int mode)
{
    return ::access(requireCString(path), mode);
}

int chmod(const std::string & path, mode_t mode)
{
    return ::chmod(requireCString(path), mode);
}

int chown(const std::string & path, uid_t uid, gid_t gid)
{
    return ::chown(requireCString(path), uid, gid);
}

int fchownat(int dir, const std::string & path, uid_t uid, gid_t gid, int flags)
{
    return ::fchownat(dir, requireCString(path), uid, gid, flags);
}

int lchown(const std::string & path, uid_t uid, gid_t gid)
{
    return ::lchown(requireCString(path), uid, gid);
}

int rename(const std::string & oldPath, const std::string & newPath)
{
    return ::rename(requireCString(oldPath), requireCString(newPath));
}

int utimes(const std::string & path, const struct ::timeval times[2])
{
    return ::utimes(requireCString(path), times);
}

int lutimes(const std::string & path, const struct ::timeval times[2])
{
    return ::lutimes(requireCString(path), times);
}

int link(const std::string & oldPath, const std::string & newPath)
{
    return ::link(requireCString(oldPath), requireCString(newPath));
}

int symlink(const std::string & target, const std::string & linkpath)
{
    return ::symlink(requireCString(target), requireCString(linkpath));
}

int unlinkat(int dir, const std::string & path, int flags)
{
    return ::unlinkat(dir, requireCString(path), flags);
}

int remove(const std::string & path)
{
    return ::remove(requireCString(path));
}

int rmdir(const std::string & path)
{
    return ::rmdir(requireCString(path));
}

int fstatat(int dir, const std::string & path, struct ::stat * st, int flags)
{
    return ::fstatat(dir, requireCString(path), st, flags);
}

int fchmodat(int dir, const std::string & path, mode_t mode, int flags)
{
    return ::fchmodat(dir, requireCString(path), mode, flags);
}

int statvfs(const std::string & path, struct ::statvfs * st)
{
    return ::statvfs(requireCString(path), st);
}

#if __linux__
int mount(
    const std::string & source,
    const std::string & target,
    const std::string & filesystemtype,
    unsigned long mountflags,
    const void * data
)
{
    return ::mount(
        requireCString(source),
        requireCString(target),
        requireCString(filesystemtype),
        mountflags,
        data
    );
}

ssize_t llistxattr(const std::string & path, char * list, size_t size)
{
    return ::llistxattr(requireCString(path), list, size);
}

ssize_t lremovexattr(const std::string & path, const std::string & name)
{
    return ::lremovexattr(requireCString(path), requireCString(name));
}

ssize_t getxattr(const std::string & path, const std::string & name, void * value, size_t size)
{
    return ::getxattr(requireCString(path), requireCString(name), value, size);
}
#endif

#if __APPLE__
ssize_t llistxattr(const std::string & path, char * list, size_t size)
{
    return ::listxattr(requireCString(path), list, size, XATTR_NOFOLLOW);
}

ssize_t lremovexattr(const std::string & path, const std::string & name)
{
    return ::removexattr(requireCString(path), requireCString(name), XATTR_NOFOLLOW);
}

ssize_t getxattr(const std::string & path, const std::string & name, void * value, size_t size)
{
    return ::getxattr(requireCString(path), requireCString(name), value, size, 0, XATTR_NOFOLLOW);
}
#endif

int chdir(const std::string & path)
{
    return ::chdir(requireCString(path));
}

int chroot(const std::string & path)
{
    return ::chroot(requireCString(path));
}

AutoCloseFD mkstemp(std::string & path)
{
    return AutoCloseFD{::mkstemp(path.data())};
}

ssize_t readlink(const std::string & path, char * buf, size_t bufsiz)
{
    return ::readlink(requireCString(path), buf, bufsiz);
}

int execv(const std::string & path, const std::list<std::string> & argv)
{
    return ::execv(requireCString(path), stringsToCharPtrs(argv).data());
}

int execvp(const std::string & path, const std::list<std::string> & argv)
{
    return ::execvp(requireCString(path), stringsToCharPtrs(argv).data());
}

int execve(
    const std::string & path,
    const std::list<std::string> & argv,
    const std::list<std::string> & envp
)
{
    // NOLINTNEXTLINE(lix-unsafe-c-calls)
    return ::execve(
        requireCString(path), stringsToCharPtrs(argv).data(), stringsToCharPtrs(envp).data()
    );
}

char * getenv(const std::string & name)
{
    return ::getenv(requireCString(name));
}

int setenv(const std::string & name, const std::string & value, int overwrite)
{
    return ::setenv(requireCString(name), requireCString(value), overwrite);
}

int unsetenv(const std::string & name)
{
    return ::unsetenv(requireCString(name));
}

struct group * getgrnam(const std::string & name)
{
    return ::getgrnam(requireCString(name));
}

std::optional<Passwd> getpwnam(const std::string & name)
{
    std::vector<char> buf(1024);
    auto cName = requireCString(name);
    struct passwd pw, *result;

    while (true) {
        const auto err = getpwnam_r(cName, &pw, buf.data(), buf.size(), &result);
        if (err == ERANGE) {
            buf.resize(buf.size() * 2);
            continue;
        } else if (err != 0) {
            throw SysError(err, "getpwnam");
        }
        if (!result) {
            return std::nullopt;
        }
        return {{
            .pw_name = result->pw_name,
            .pw_passwd = result->pw_passwd,
            .pw_uid = result->pw_uid,
            .pw_gid = result->pw_gid,
            .pw_gecos = result->pw_gecos,
            .pw_dir = result->pw_dir,
            .pw_shell = result->pw_shell,
        }};
    }
}
}
