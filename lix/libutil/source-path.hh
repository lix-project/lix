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

class CheckedSourcePath;

/**
 * An abstraction for manipulating path names during evaluation.
 */
struct SourcePath
{
protected:
    CanonPath path;

public:
    SourcePath(CanonPath path)
        : path(std::move(path))
    { }

    std::string_view baseName() const;

    /**
     * Construct the parent of this `SourcePath`. Aborts if `this`
     * denotes the root.
     */
    SourcePath parent() const;

    const CanonPath & canonical() const { return path; }

    std::string to_string() const
    { return path.abs(); }

    /**
     * Converts this `SourcePath` into a checked `SourcePath`, consuming it.
     */
    CheckedSourcePath unsafeIntoChecked();

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
};

std::ostream & operator << (std::ostream & str, const SourcePath & path);

/**
 * An abstraction for accessing source files during
 * evaluation. Currently, it's just a wrapper around `CanonPath` that
 * accesses files in the regular filesystem, but in the future it will
 * support fetching files in other ways.
 */
class CheckedSourcePath : public SourcePath
{
    friend struct SourcePath;

    CheckedSourcePath(CanonPath path): SourcePath(std::move(path)) {}

public:
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
     * Return stats about this `SourcePath`, or throw an exception if
     * it doesn't exist. Symlinks are resolved by this function.
     */
    InputAccessor::Stat stat() const;

    /**
     * Return stats about this `SourcePath`, or std::nullopt if it
     * doesn't exist. Symlinks are resolved by this function.
     */
    std::optional<InputAccessor::Stat> maybeStat() const;

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
};

inline CheckedSourcePath SourcePath::unsafeIntoChecked()
{
    return CheckedSourcePath(std::move(path));
}

}
