#pragma once
/**
 * @file
 *
 * Utiltities for working with the file sytem and file paths.
 */

#include "lix/libutil/async-io.hh"
#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/file-descriptor.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <functional>
#include <optional>

#ifndef HAVE_STRUCT_DIRENT_D_TYPE
#define DT_UNKNOWN 0
#define DT_REG 1
#define DT_LNK 2
#define DT_DIR 3
#endif

namespace nix {

struct Sink;
struct Source;

/**
 * Get the current working directory.
 *
 * Throw an error if the current directory cannot get got.
 */
Path getCwd();

/**
 * @return An absolutized path, resolving paths relative to the
 * specified directory, or the current directory otherwise.  The path
 * is also canonicalised.
 */
Path absPath(Path path,
    std::optional<PathView> dir = {},
    bool resolveSymlinks = false);

/**
 * Canonicalise a path by removing all `.` or `..` components and
 * double or trailing slashes.  Optionally resolves all symlink
 * components such that each component of the resulting path is *not*
 * a symbolic link.
 */
Path canonPath(PathView path, bool resolveSymlinks = false);

/**
 * Resolves a file path to a fully absolute path with no symbolic links.
 *
 * @param path The path to resolve. If it is relative, it will be resolved relative
 * to the process's current directory.
 *
 * @note This is not a pure function; it performs this resolution by querying
 * the filesystem.
 *
 * @note @ref path sadly must be (a reference to) an owned string, as std::string_view
 * are not valid C strings...
 *
 * @return The fully resolved path.
 */
Path realPath(Path const & path);

/**
 * Resolve a tilde path like `~/puppy.nix` into an absolute path.
 *
 * If `home` is given, it's substituted for `~/` at the start of the input
 * `path`. Otherwise, an error is thrown.
 *
 * If the path starts with `~` but not `~/`, an error is thrown.
 */
Path tildePath(Path const & path, const std::optional<Path> & home = std::nullopt);

/**
 * Change the permissions of a path
 * Not called `chmod` as it shadows and could be confused with
 * `int chmod(char *, mode_t)`, which does not handle errors
 */
void chmodPath(const Path & path, mode_t mode);

/**
 * @return The directory part of the given canonical path, i.e.,
 * everything before the final `/`.  If the path is the root or an
 * immediate child thereof (e.g., `/foo`), this means `/`
 * is returned.
 */
Path dirOf(const PathView path);

/**
 * @return the base name of the given canonical path, i.e., everything
 * following the final `/` (trailing slashes are removed).
 */
std::string_view baseNameOf(std::string_view path);

/**
 * Perform tilde expansion on a path.
 */
std::string expandTilde(std::string_view path);

/**
 * Check whether 'path' is a descendant of 'dir'. Both paths must be
 * canonicalized.
 */
bool isInDir(std::string_view path, std::string_view dir);

/**
 * Check whether 'path' is equal to 'dir' or a descendant of
 * 'dir'. Both paths must be canonicalized.
 */
bool isDirOrInDir(std::string_view path, std::string_view dir);

/**
 * Get status of `path`.
 */
struct stat stat(const Path & path);
struct stat lstat(const Path & path);

/**
 * `stat` the given path if it exists.
 * @return std::nullopt if the path doesn't exist, or an optional containing the result of `stat` otherwise
 */
std::optional<struct stat> maybeStat(const Path & path);

/**
 * `lstat` the given path if it exists.
 * @return std::nullopt if the path doesn't exist, or an optional containing the result of `lstat` otherwise
 */
std::optional<struct stat> maybeLstat(const Path & path);

/**
 * @return true iff the given path exists.
 */
bool pathExists(const Path & path);

/**
 * A version of pathExists that returns false on a permission error.
 * Useful for inferring default paths across directories that might not
 * be readable. Optionally resolves symlinks to determine if the real
 * path exists.
 * @return true iff the given path can be accessed and exists
 */
bool pathAccessible(const Path & path, bool resolveSymlinks = false);

/**
 * Read the contents (target) of a symbolic link.  The result is not
 * in any way canonicalised.
 */
Path readLink(const Path & path);

bool isLink(const Path & path);

/**
 * Read the contents of a directory.  The entries `.` and `..` are
 * removed.
 */
struct DirEntry
{
    std::string name;
    ino_t ino;
    /**
     * one of DT_*
     */
    unsigned char type;
    DirEntry(std::string name, ino_t ino, unsigned char type)
        : name(std::move(name)), ino(ino), type(type) { }
};

typedef std::vector<DirEntry> DirEntries;

DirEntries readDirectory(const Path & path);

unsigned char getFileType(const Path & path);

/**
 * Read the contents of a file into a string.
 */
std::string readFile(const Path & path);
Generator<Bytes> readFileSource(const Path & path);

/**
 * Write a string to a file.
 */
void writeFile(
    const Path & path, std::string_view s, mode_t mode = 0666, bool allowInterrupts = true
);
void writeFileUninterruptible(const Path & path, std::string_view s, mode_t mode = 0666);
void writeFile(const Path & path, Source & source, mode_t mode = 0666);

void writeFile(
    AutoCloseFD & fd, std::string_view s, mode_t mode = 0666, bool allowInterrupts = true
);
kj::Promise<Result<void>>
writeFile(const Path & path, AsyncInputStream & source, mode_t mode = 0666);

/**
 * Write a string to a file and flush the file and its parents direcotry to disk.
 */
void writeFileAndSync(const Path & path, std::string_view s, mode_t mode = 0666);

/**
 * Flush a file's parent directory to disk
 */
void syncParent(const Path & path);

/**
 * Delete a path; i.e., in the case of a directory, it is deleted
 * recursively. It's not an error if the path does not exist. The
 * second variant returns the number of bytes and blocks freed.
 */
void deletePath(const Path & path);
void deletePathUninterruptible(const Path & path);

void deletePath(const Path & path, uint64_t & bytesFreed);

/**
 * Create a directory and all its parents, if necessary.  Returns the
 * list of created directories, in order of creation.
 */
Paths createDirs(const Path & path);
inline Paths createDirs(PathView path)
{
    return createDirs(Path(path));
}

/**
 * Create a symlink. Throws if the symlink exists.
 */
void createSymlink(const Path & target, const Path & link);

/**
 * Atomically create or replace a symlink.
 */
void replaceSymlink(const Path & target, const Path & link);

void renameFile(const Path & src, const Path & dst);

/**
 * Similar to 'renameFile', but fallback to a copy+remove if `src` and `dst`
 * are on a different filesystem.
 *
 * Beware that this might not be atomic because of the copy that happens behind
 * the scenes
 */
void moveFile(const Path & src, const Path & dst);

struct CopyFileFlags
{
    /**
     * Delete the file after copying.
     */
    bool deleteAfter = false;

    /**
     * Follow symlinks and copy the eventual target.
     */
    bool followSymlinks = false;
};

/**
 * Recursively copy the content of `oldPath` to `newPath`. If `andDelete` is
 * `true`, then also remove `oldPath` (making this equivalent to `moveFile`, but
 * with the guaranty that the destination will be “fresh”, with no stale inode
 * or file descriptor pointing to it).
 */
void copyFile(const Path & oldPath, const Path & newPath, CopyFileFlags flags);

/**
 * Automatic cleanup of resources.
 */


class AutoDelete
{
    Path path;
    bool del;
    bool recursive;
public:
    KJ_DISALLOW_COPY_AND_MOVE(AutoDelete);

    AutoDelete();
    AutoDelete(const Path & p, bool recursive = true);
    ~AutoDelete();
    void cancel();
    void reset(const Path & p, bool recursive = true);
    operator Path() const { return path; }
    operator PathView() const { return path; }
};

struct DIRDeleter
{
    void operator()(DIR * dir) const {
        closedir(dir);
    }
};

typedef std::unique_ptr<DIR, DIRDeleter> AutoCloseDir;

/**
 * Create a temporary directory in a given parent directory.
 */
Path createTempSubdir(const Path & parent, const Path & prefix = "nix",
    bool includePid = true, bool useGlobalCounter = true, mode_t mode = 0755);

/**
 * Return temporary path constructed by appending a suffix to a root path.
 *
 * The constructed path looks like `<root><suffix>-<pid>-<unique>`. To create a
 * path nested in a directory, provide a suffix starting with `/`.
 */
Path makeTempPath(const Path & root, const Path & suffix = ".tmp");

/**
 * Used in various places.
 */
typedef std::function<bool(const Path & path)> PathFilter;

extern PathFilter defaultPathFilter;

}
