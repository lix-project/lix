#pragma once
/**
 * @file
 *
 * @brief SourcePath
 */

#include "lix/libutil/ref.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/canon-path.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/repair-flag.hh"
#include "lix/libutil/input-accessor.hh"

namespace nix {

/**
 * Note there is a decent chance this type soon goes away because the problem is solved another way.
 * See the discussion in https://github.com/NixOS/nix/pull/9985.
 */
enum class SymlinkResolution {
    /**
     * Resolve symlinks in the ancestors only.
     *
     * Only the last component of the result is possibly a symlink.
     */
    Ancestors,

    /**
     * Resolve symlinks fully, realpath(3)-style.
     *
     * No component of the result will be a symlink.
     */
    Full,
};

/**
 * An abstraction for accessing source files during
 * evaluation. Currently, it's just a wrapper around `CanonPath` that
 * accesses files in the regular filesystem, but in the future it will
 * support fetching files in other ways.
 */
struct SourcePath
{
    CanonPath path;

    SourcePath(CanonPath path)
        : path(std::move(path))
    { }

    std::string_view baseName() const;

    /**
     * Construct the parent of this `SourcePath`. Aborts if `this`
     * denotes the root.
     */
    SourcePath parent() const;

    /**
     * If this `SourcePath` denotes a regular file (not a symlink),
     * return its contents; otherwise throw an error.
     */
    std::string readFile() const
    { return nix::readFile(path.abs()); }

    /**
     * Return whether this `SourcePath` denotes a file (of any type)
     * that exists
    */
    bool pathExists() const
    { return nix::pathExists(path.abs()); }

    /**
     * Return stats about this `SourcePath`, or throw an exception if
     * it doesn't exist.
     */
    InputAccessor::Stat lstat() const;

    /**
     * Return stats about this `SourcePath`, or std::nullopt if it
     * doesn't exist.
     */
    std::optional<InputAccessor::Stat> maybeLstat() const;

    /**
     * If this `SourcePath` denotes a directory (not a symlink),
     * return its directory entries; otherwise throw an error.
     */
    InputAccessor::DirEntries readDirectory() const;

    /**
     * If this `SourcePath` denotes a symlink, return its target;
     * otherwise throw an error.
     */
    std::string readLink() const
    { return nix::readLink(path.abs()); }

    /**
     * Dump this `SourcePath` to `sink` as a NAR archive.
     */
    void dumpPath(
        Sink & sink,
        PathFilter & filter = defaultPathFilter) const
    { sink << nix::dumpPath(path.abs(), filter); }

    /**
     * Return the location of this path in the "real" filesystem, if
     * it has a physical location.
     */
    std::optional<CanonPath> getPhysicalPath() const
    { return path; }

    std::string to_string() const
    { return path.abs(); }

    /**
     * Append a `CanonPath` to this path.
     */
    SourcePath operator + (const CanonPath & x) const
    { return {path + x}; }

    /**
     * Append a single component `c` to this path. `c` must not
     * contain a slash. A slash is implicitly added between this path
     * and `c`.
     */
    SourcePath operator + (std::string_view c) const
    {  return {path + c}; }

    bool operator == (const SourcePath & x) const
    {
        return path == x.path;
    }

    bool operator != (const SourcePath & x) const
    {
        return path != x.path;
    }

    bool operator < (const SourcePath & x) const
    {
        return path < x.path;
    }

    /**
     * Resolve any symlinks in this `SourcePath` according to the
     * given resolution mode.
     *
     * @param mode might only be a temporary solution for this.
     * See the discussion in https://github.com/NixOS/nix/pull/9985.
     */
    SourcePath resolveSymlinks(
        SymlinkResolution mode = SymlinkResolution::Full) const;
};

std::ostream & operator << (std::ostream & str, const SourcePath & path);

}
