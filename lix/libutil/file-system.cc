#include <sys/time.h>
#include <cstdlib>
#include <filesystem>
#include <atomic>
#include <random>

#include "lix/libutil/environment-variables.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/users.hh"

namespace fs = std::filesystem;

namespace nix {

Path getCwd() {
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf))) {
        throw SysError("cannot get cwd");
    }
    return Path(buf);
}

Path absPath(Path path, std::optional<PathView> dir, bool resolveSymlinks)
{
    if (path.empty() || path[0] != '/') {
        if (!dir) {
            path = concatStrings(getCwd(), "/", path);
        } else {
            path = concatStrings(*dir, "/", path);
        }
    }
    return canonPath(path, resolveSymlinks);
}


Path canonPath(PathView path, bool resolveSymlinks)
{
    if (path == "" || path[0] != '/')
        throw Error("not an absolute path: '%1%'", path);

    std::string s;
    s.reserve(256);

    std::string temp;

    /* Count the number of times we follow a symlink and stop at some
       arbitrary (but high) limit to prevent infinite loops. */
    unsigned int followCount = 0, maxFollow = 1024;

    while (1) {

        /* Skip slashes. */
        while (!path.empty() && path[0] == '/') path.remove_prefix(1);
        if (path.empty()) break;

        /* Ignore `.'. */
        if (path == "." || path.substr(0, 2) == "./")
            path.remove_prefix(1);

        /* If `..', delete the last component. */
        else if (path == ".." || path.substr(0, 3) == "../")
        {
            if (!s.empty()) s.erase(s.rfind('/'));
            path.remove_prefix(2);
        }

        /* Normal component; copy it. */
        else {
            s += '/';
            if (const auto slash = path.find('/'); slash == std::string::npos) {
                s += path;
                path = {};
            } else {
                s += path.substr(0, slash);
                path = path.substr(slash);
            }

            /* If s points to a symlink, resolve it and continue from there */
            if (resolveSymlinks && isLink(s)) {
                if (++followCount >= maxFollow)
                    throw Error("infinite symlink recursion in path '%1%'", path);
                temp = concatStrings(readLink(s), path);
                path = temp;
                if (!temp.empty() && temp[0] == '/') {
                    s.clear();  /* restart for symlinks pointing to absolute path */
                } else {
                    s = dirOf(s);
                    if (s == "/") {  // we don’t want trailing slashes here, which dirOf only produces if s = /
                        s.clear();
                    }
                }
            }
        }
    }

    return s.empty() ? "/" : std::move(s);
}

Path realPath(Path const & path)
{
    // With nullptr, realpath() malloc's and returns a new c-string.
    char * resolved = realpath(path.c_str(), nullptr);
    int saved = errno;
    if (resolved == nullptr) {
        throw SysError(saved, "cannot get realpath for '%s'", path);
    }

    Finally const _free([&] { free(resolved); });

    // There's not really a from_raw_parts() for std::string.
    // The copy is not a big deal.
    Path ret(resolved);

    return ret;
}

Path tildePath(Path const & path, const std::optional<Path> & home)
{
    if (path.starts_with("~/")) {
        if (home) {
            return *home + "/" + path.substr(2);
        } else {
            throw UsageError("`~` path not allowed: %1%", path);
        }
    } else if (path.starts_with('~')) {
        throw UsageError("`~` paths must start with `~/`: %1%", path);
    } else {
        return path;
    }
}

void chmodPath(const Path & path, mode_t mode)
{
    if (chmod(path.c_str(), mode) == -1)
        throw SysError("setting permissions on '%s'", path);
}

Path dirOf(const PathView path)
{
    Path::size_type pos = path.rfind('/');
    if (pos == std::string::npos)
        return ".";
    return pos == 0 ? "/" : Path(path, 0, pos);
}


std::string_view baseNameOf(std::string_view path)
{
    if (path.empty())
        return "";

    auto last = path.size() - 1;
    if (path[last] == '/' && last > 0)
        last -= 1;

    auto pos = path.rfind('/', last);
    if (pos == std::string::npos)
        pos = 0;
    else
        pos += 1;

    return path.substr(pos, last - pos + 1);
}


std::string expandTilde(std::string_view path)
{
    // TODO: expand ~user ?
    auto tilde = path.substr(0, 2);
    if (tilde == "~/" || tilde == "~")
        return getHome() + std::string(path.substr(1));
    else
        return std::string(path);
}


bool isInDir(std::string_view path, std::string_view dir)
{
    return path.substr(0, 1) == "/"
        && path.substr(0, dir.size()) == dir
        && path.size() >= dir.size() + 2
        && path[dir.size()] == '/';
}


bool isDirOrInDir(std::string_view path, std::string_view dir)
{
    return path == dir || isInDir(path, dir);
}


struct stat stat(const Path & path)
{
    struct stat st;
    if (stat(path.c_str(), &st))
        throw SysError("getting status of '%1%'", path);
    return st;
}


struct stat lstat(const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError("getting status of '%1%'", path);
    return st;
}

std::optional<struct stat> maybeStat(const Path & path)
{
    std::optional<struct stat> st{std::in_place};
    if (stat(path.c_str(), &*st))
    {
        if (errno == ENOENT || errno == ENOTDIR)
            st.reset();
        else
            throw SysError("getting status of '%s'", path);
    }
    return st;
}

std::optional<struct stat> maybeLstat(const Path & path)
{
    std::optional<struct stat> st{std::in_place};
    if (lstat(path.c_str(), &*st))
    {
        if (errno == ENOENT || errno == ENOTDIR)
            st.reset();
        else
            throw SysError("getting status of '%s'", path);
    }
    return st;
}

bool pathExists(const Path & path)
{
    return maybeLstat(path).has_value();
}

bool pathAccessible(const Path & path, bool resolveSymlinks)
{
    try {
        return resolveSymlinks ? maybeStat(path).has_value() : pathExists(path);
    } catch (SysError & e) {
        switch (e.errNo) {
        case EPERM:
            // operation not permitted error, can occur in darwin sandbox
        case EACCES:
            // permission error
        case ELOOP:
            // path component is a looping symlink
            // this seems like a reasonable condition to handle as well
            return false;
        default:
            throw;
        }
    }
}


Path readLink(const Path & path)
{
    checkInterrupt();
    std::vector<char> buf;
    for (ssize_t bufSize = PATH_MAX/4; true; bufSize += bufSize/2) {
        buf.resize(bufSize);
        ssize_t rlSize = readlink(path.c_str(), buf.data(), bufSize);
        if (rlSize == -1)
            if (errno == EINVAL)
                throw Error("'%1%' is not a symlink", path);
            else
                throw SysError("reading symbolic link '%1%'", path);
        else if (rlSize < bufSize)
            return std::string(buf.data(), rlSize);
    }
}


bool isLink(const Path & path)
{
    struct stat st = lstat(path);
    return S_ISLNK(st.st_mode);
}


static DirEntries readDirectory(DIR *dir, const Path & path, bool interruptible)
{
    DirEntries entries;
    entries.reserve(64);

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir)) { /* sic */
        if (interruptible) {
            checkInterrupt();
        }
        std::string name = dirent->d_name;
        if (name == "." || name == "..") continue;
        entries.emplace_back(name, dirent->d_ino,
#ifdef HAVE_STRUCT_DIRENT_D_TYPE
            dirent->d_type
#else
            DT_UNKNOWN
#endif
        );
    }
    if (errno) throw SysError("reading directory '%1%'", path);

    return entries;
}

static DirEntries readDirectory(const Path & path, bool interruptible)
{
    AutoCloseDir dir(opendir(path.c_str()));
    if (!dir) throw SysError("opening directory '%1%'", path);

    return readDirectory(dir.get(), path, interruptible);
}

DirEntries readDirectory(const Path & path)
{
    return readDirectory(path, true);
}


unsigned char getFileType(const Path & path)
{
    struct stat st = lstat(path);
    if (S_ISDIR(st.st_mode)) return DT_DIR;
    if (S_ISLNK(st.st_mode)) return DT_LNK;
    if (S_ISREG(st.st_mode)) return DT_REG;
    return DT_UNKNOWN;
}


std::string readFile(const Path & path)
{
    AutoCloseFD fd{open(path.c_str(), O_RDONLY | O_CLOEXEC)};
    if (!fd)
        throw SysError("opening file '%1%'", path);
    return readFile(fd.get());
}


Generator<Bytes> readFileSource(const Path & path)
{
    AutoCloseFD fd{open(path.c_str(), O_RDONLY | O_CLOEXEC)};
    if (!fd)
        throw SysError("opening file '%s'", path);
    return [](AutoCloseFD fd) -> Generator<Bytes> {
        co_yield drainFDSource(fd.get());
    }(std::move(fd));
}

void writeFile(const Path & path, std::string_view s, mode_t mode, bool allowInterrupts)
{
    AutoCloseFD fd{open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC, mode)};
    if (!fd)
        throw SysError("opening file '%1%'", path);

    writeFile(fd, s, mode, allowInterrupts);

    /* Close explicitly to propagate the exceptions. */
    fd.close();
}

void writeFile(AutoCloseFD & fd, std::string_view s, mode_t mode, bool allowInterrupts)
{
    assert(fd);
    try {
        writeFull(fd.get(), s, allowInterrupts);
    } catch (Error & e) {
        e.addTrace({}, "writing file '%1%'", fd.guessOrInventPath());
        throw;
    }
}

void writeFileUninterruptible(const Path & path, std::string_view s, mode_t mode)
{
    writeFile(path, s, mode, false);
}

void writeFileAndSync(const Path & path, std::string_view s, mode_t mode)
{
    {
        AutoCloseFD fd{open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC, mode)};
        if (!fd) {
            throw SysError("opening file '%1%'", path);
        }

        writeFile(fd, s, mode);
        fd.fsync();
        /* Close explicitly to ensure that exceptions are propagated. */
        fd.close();
    }

    syncParent(path);
}

static AutoCloseFD openForWrite(const Path & path, mode_t mode)
{
    AutoCloseFD fd{open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC, mode)};
    if (!fd)
        throw SysError("opening file '%1%'", path);
    return fd;
}

static void closeForWrite(const Path & path, AutoCloseFD & fd, bool sync)
{
    if (sync)
        fd.fsync();
    // Explicitly close to make sure exceptions are propagated.
    fd.close();
    if (sync)
        syncParent(path);
}

void writeFile(const Path & path, Source & source, mode_t mode)
{
    AutoCloseFD fd = openForWrite(path, mode);

    std::vector<char> buf(64 * 1024);

    try {
        while (true) {
            try {
                auto n = source.read(buf.data(), buf.size());
                writeFull(fd.get(), {buf.data(), n});
            } catch (EndOfFile &) { break; }
        }
    } catch (Error & e) {
        e.addTrace({}, "writing file '%1%'", path);
        throw;
    }
    closeForWrite(path, fd, false);
}

kj::Promise<Result<void>> writeFile(const Path & path, AsyncInputStream & source, mode_t mode)
try {
    AutoCloseFD fd = openForWrite(path, mode);

    std::vector<char> buf(64 * 1024);

    try {
        while (true) {
            if (auto n = TRY_AWAIT(source.read(buf.data(), buf.size()))) {
                writeFull(fd.get(), {buf.data(), *n});
            } else {
                break;
            }
        }
    } catch (Error & e) {
        e.addTrace({}, "writing file '%1%'", path);
        throw;
    }
    closeForWrite(path, fd, false);
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

void syncParent(const Path & path)
{
    AutoCloseFD fd{open(dirOf(path).c_str(), O_RDONLY, 0)};
    if (!fd)
        throw SysError("opening file '%1%'", path);
    fd.fsync();
}

/* TODO(horrors): a better structure that links all parent fds for the traversal root
 * should be considered for this code
 */
static void _deletePath(int parentfd, const std::string & name, uint64_t & bytesFreed, bool interruptible)
{
    /* This ensures that `name` is an immediate child of `parentfd`. */
    assert(!name.empty() && name.find('/') == std::string::npos && "`name` is an immediate child to `parentfd`");

    if (interruptible) {
        checkInterrupt();
    }

    /* FIXME(horrors): there's a minor TOCTOU here.
     * we fstatat the inode nofollow, check if this is a directory
     * and then open it.
     * a better alternative is open it as O_PATH as a namefd.
     * if it's a directory, it can be openat with the namefd.
     */

    struct stat st;
    if (fstatat(parentfd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == -1) {
        if (errno == ENOENT) return;
        throw SysError("getting status of '%1%' in directory '%2%'", name, guessOrInventPathFromFD(parentfd));
    }

    if (!S_ISDIR(st.st_mode)) {
        /* We are about to delete a file. Will it likely free space? */

        switch (st.st_nlink) {
            /* Yes: last link. */
            case 1:
                bytesFreed += st.st_size;
                break;
            /* Maybe: yes, if 'auto-optimise-store' or manual optimisation
               was performed. Instead of checking for real let's assume
               it's an optimised file and space will be freed.

               In worst case we will double count on freed space for files
               with exactly two hardlinks for unoptimised packages.
             */
            case 2:
                bytesFreed += st.st_size;
                break;
            /* No: 3+ links. */
            default:
                break;
        }
    }

    if (S_ISDIR(st.st_mode)) {
        /* Make the directory accessible. */
        const auto PERM_MASK = S_IRUSR | S_IWUSR | S_IXUSR;
        if ((st.st_mode & PERM_MASK) != PERM_MASK) {
            if (fchmodat(parentfd, name.c_str(), st.st_mode | PERM_MASK, 0) == -1) {
                throw SysError("chmod '%1%' in directory '%2%'", name, guessOrInventPathFromFD(parentfd));
            }
        }

        int fd = openat(parentfd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (fd == -1)
            throw SysError("opening directory '%1%' in directory '%2%'", name, guessOrInventPathFromFD(parentfd));
        AutoCloseDir dir(fdopendir(fd));
        if (!dir)
            throw SysError("opening directory '%1%' in directory '%2%'", name, guessOrInventPathFromFD(parentfd));
        for (auto & i : readDirectory(dir.get(), name, interruptible))
            _deletePath(dirfd(dir.get()), i.name, bytesFreed, interruptible);
    }

    int flags = S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0;
    if (unlinkat(parentfd, name.c_str(), flags) == -1) {
        if (errno == ENOENT) return;
        throw SysError("cannot unlink '%1%' in directory '%2%'", name, guessOrInventPathFromFD(parentfd));
    }
}

static void _deletePath(const Path & path, uint64_t & bytesFreed, bool interruptible)
{
    Path dir = dirOf(path);
    if (dir == "")
        dir = "/";

    AutoCloseFD dirfd{open(dir.c_str(), O_RDONLY)};
    if (!dirfd) {
        if (errno == ENOENT) return;
        throw SysError("opening directory '%1%'", path);
    }

    _deletePath(dirfd.get(), std::string(baseNameOf(path)), bytesFreed, interruptible);
}


void deletePath(const Path & path)
{
    uint64_t dummy;
    deletePath(path, dummy);
}

void deletePathUninterruptible(const Path & path)
{
    uint64_t dummy;
    _deletePath(path, dummy, false);
}


void deletePath(const Path & path, uint64_t & bytesFreed)
{
    //Activity act(*logger, lvlDebug, "recursively deleting path '%1%'", path);
    bytesFreed = 0;
    _deletePath(path, bytesFreed, true);
}

Paths createDirs(const Path & path)
{
    Paths created;
    if (path == "/") return created;

    struct stat st;
    if (lstat(path.c_str(), &st) == -1) {
        created = createDirs(dirOf(path));
        if (mkdir(path.c_str(), 0777) == -1 && errno != EEXIST)
            throw SysError("creating directory '%1%'", path);
        st = lstat(path);
        created.push_back(path);
    }

    if (S_ISLNK(st.st_mode) && stat(path.c_str(), &st) == -1)
        throw SysError("statting symlink '%1%'", path);

    if (!S_ISDIR(st.st_mode)) throw Error("'%1%' is not a directory", path);

    return created;
}


//////////////////////////////////////////////////////////////////////

AutoDelete::AutoDelete() : del{false} {}

AutoDelete::AutoDelete(const std::string & p, bool recursive) : path(p)
{
    del = true;
    this->recursive = recursive;
}

AutoDelete::~AutoDelete()
{
    try {
        if (del) {
            if (recursive)
                deletePath(path);
            else {
                if (remove(path.c_str()) == -1)
                    throw SysError("cannot unlink '%1%'", path);
            }
        }
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

void AutoDelete::cancel()
{
    del = false;
}

void AutoDelete::reset(const Path & p, bool recursive) {
    path = p;
    this->recursive = recursive;
    del = true;
}

//////////////////////////////////////////////////////////////////////

static Path tempName(PathView parent, const Path & prefix, bool includePid,
    std::atomic<unsigned int> & counter)
{
    auto tmpRoot = canonPath(parent, true);
    if (includePid)
        return fmt("%1%/%2%-%3%-%4%", tmpRoot, prefix, getpid(), counter++);
    else
        return fmt("%1%/%2%-%3%", tmpRoot, prefix, counter++);
}

Path createTempSubdir(const Path & parent, const Path & prefix,
    bool includePid, bool useGlobalCounter, mode_t mode)
{
    static std::atomic<unsigned int> globalCounter = 0;
    std::atomic<unsigned int> localCounter = 0;
    auto & counter(useGlobalCounter ? globalCounter : localCounter);

    while (1) {
        checkInterrupt();
        Path tmpDir = tempName(parent, prefix, includePid, counter);
        if (mkdir(tmpDir.c_str(), mode) == 0) {
#if __FreeBSD__
            /* Explicitly set the group of the directory.  This is to
               work around around problems caused by BSD's group
               ownership semantics (directories inherit the group of
               the parent).  For instance, the group of /tmp on
               FreeBSD is "wheel", so all directories created in /tmp
               will be owned by "wheel"; but if the user is not in
               "wheel", then "tar" will fail to unpack archives that
               have the setgid bit set on directories. */
            if (chown(tmpDir.c_str(), (uid_t) -1, getegid()) != 0)
                throw SysError("setting group of directory '%1%'", tmpDir);
#endif
            return tmpDir;
        }
        if (errno != EEXIST)
            throw SysError("creating directory '%1%'", tmpDir);
    }
}

Path makeTempPath(const Path & root, const Path & suffix)
{
    // start the counter at a random value to minimize issues with preexisting temp paths
    static std::atomic_uint_fast32_t counter(std::random_device{}());
    return fmt("%1%%2%-%3%-%4%", root, suffix, getpid(), counter.fetch_add(1, std::memory_order_relaxed));
}

void createSymlink(const Path & target, const Path & link)
{
    if (symlink(target.c_str(), link.c_str()))
        throw SysError("creating symlink from '%1%' to '%2%'", link, target);
}

void replaceSymlink(const Path & target, const Path & link)
{
    for (unsigned int n = 0; true; n++) {
        Path tmp = canonPath(fmt("%s/.%d_%s", dirOf(link), n, baseNameOf(link)));

        try {
            createSymlink(target, tmp);
        } catch (SysError & e) {
            if (e.errNo == EEXIST) continue;
            throw;
        }

        renameFile(tmp, link);

        break;
    }
}

void setWriteTime(const fs::path & p, const struct stat & st)
{
    struct timeval times[2];
    times[0] = {
        .tv_sec = st.st_atime,
        .tv_usec = 0,
    };
    times[1] = {
        .tv_sec = st.st_mtime,
        .tv_usec = 0,
    };
    if (lutimes(p.c_str(), times) != 0)
        throw SysError("changing modification time of '%s'", p);
}

void copy(const fs::directory_entry & from, const fs::path & to, CopyFileFlags flags)
{
    // TODO: Rewrite the `is_*` to use `symlink_status()`
    auto statOfFrom = lstat(from.path().c_str());
    auto fromStatus = from.symlink_status();

    // Mark the directory as writable so that we can delete its children
    if (flags.deleteAfter && fs::is_directory(fromStatus)) {
        fs::permissions(from.path(), fs::perms::owner_write, fs::perm_options::add | fs::perm_options::nofollow);
    }


    if (fs::is_symlink(fromStatus) || fs::is_regular_file(fromStatus)) {
        auto opts = fs::copy_options::overwrite_existing;

        if (!flags.followSymlinks) {
            opts |= fs::copy_options::copy_symlinks;
        }

        fs::copy(from.path(), to, opts);
    } else if (fs::is_directory(fromStatus)) {
        fs::create_directory(to);
        for (auto & entry : fs::directory_iterator(from.path())) {
            copy(entry, to / entry.path().filename(), flags);
        }
    } else {
        throw Error("file '%s' has an unsupported type", from.path());
    }

    setWriteTime(to, statOfFrom);
    if (flags.deleteAfter) {
        if (!fs::is_symlink(fromStatus))
            fs::permissions(from.path(), fs::perms::owner_write, fs::perm_options::add | fs::perm_options::nofollow);
        fs::remove(from.path());
    }
}


void copyFile(const Path & oldPath, const Path & newPath, CopyFileFlags flags)
{
    return copy(fs::directory_entry(fs::path(oldPath)), fs::path(newPath), flags);
}

void renameFile(const Path & oldName, const Path & newName)
{
    fs::rename(oldName, newName);
}

void moveFile(const Path & oldName, const Path & newName)
{
    try {
        renameFile(oldName, newName);
    } catch (fs::filesystem_error & e) { // NOLINT(lix-foreign-exceptions)
        auto oldPath = fs::path(oldName);
        auto newPath = fs::path(newName);
        // For the move to be as atomic as possible, copy to a temporary
        // directory
        fs::path temp = createTempSubdir(newPath.parent_path(), "rename-tmp");
        Finally removeTemp = [&]() { fs::remove(temp); };
        auto tempCopyTarget = temp / "copy-target";
        if (e.code().value() == EXDEV) {
            fs::remove(newPath);
            printTaggedWarning("Can’t rename %s as %s, copying instead", oldName, newName);
            copy(fs::directory_entry(oldPath), tempCopyTarget, { .deleteAfter = true });
            renameFile(tempCopyTarget, newPath);
        }
    }
}

}
