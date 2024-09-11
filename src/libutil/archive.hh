#pragma once
///@file

#include "generator.hh"
#include "types.hh"
#include "serialise.hh"
#include "file-system.hh"


namespace nix {


/**
 * dumpPath creates a Nix archive of the specified path.
 *
 * @param path the file system data to dump. Dumping is recursive so if
 * this is a directory we dump it and all its children.
 *
 * @param [out] sink The serialised archive is fed into this sink.
 *
 * @param filter Can be used to skip certain files.
 *
 * The format is as follows:
 *
 * ```
 * IF path points to a REGULAR FILE:
 *   dump(path) = attrs(
 *     [ ("type", "regular")
 *     , ("contents", contents(path))
 *     ])
 *
 * IF path points to a DIRECTORY:
 *   dump(path) = attrs(
 *     [ ("type", "directory")
 *     , ("entries", concat(map(f, sort(entries(path)))))
 *     ])
 *     where f(fn) = attrs(
 *       [ ("name", fn)
 *       , ("file", dump(path + "/" + fn))
 *       ])
 *
 * where:
 *
 *   attrs(as) = concat(map(attr, as)) + encN(0)
 *   attrs((a, b)) = encS(a) + encS(b)
 *
 *   encS(s) = encN(len(s)) + s + (padding until next 64-bit boundary)
 *
 *   encN(n) = 64-bit little-endian encoding of n.
 *
 *   contents(path) = the contents of a regular file.
 *
 *   sort(strings) = lexicographic sort by 8-bit value (strcmp).
 *
 *   entries(path) = the entries of a directory, without `.` and
 *   `..`.
 *
 *   `+` denotes string concatenation.
 * ```
 */
WireFormatGenerator dumpPath(Path path,
    PathFilter & filter = defaultPathFilter);

/**
 * Same as dumpPath(), but returns the last modified date of the path.
 */
WireFormatGenerator dumpPathAndGetMtime(Path path, time_t & mtime,
    PathFilter & filter = defaultPathFilter);

/**
 * Dump an archive with a single file with these contents.
 *
 * @param s Contents of the file.
 */
WireFormatGenerator dumpString(std::string_view s);

/**
 * \todo Fix this API, it sucks.
 * A visitor for NAR parsing that performs filesystem (or virtual-filesystem)
 * actions to restore a NAR.
 *
 * Methods of this may arbitrarily fail due to filename collisions.
 */
struct NARParseVisitor
{
    virtual void createDirectory(const Path & path) { };

    virtual void createRegularFile(const Path & path) { };
    virtual void closeRegularFile() { };
    virtual void isExecutable() { };
    virtual void preallocateContents(uint64_t size) { };
    virtual void receiveContents(std::string_view data) { };

    virtual void createSymlink(const Path & path, const std::string & target) { };
};

namespace nar {

struct MetadataString;
struct MetadataRaw;
struct File;
struct Symlink;
struct Directory;
using Entry = std::variant<MetadataString, MetadataRaw, File, Symlink, Directory>;

struct MetadataString
{
    std::string_view data;
};

struct MetadataRaw
{
    Bytes raw;
};

struct File
{
    const Path & path;
    bool executable;
    uint64_t size;
    Generator<Bytes> contents;
};

struct Symlink
{
    const Path & path;
    const Path & target;
};

struct Directory
{
    const Path & path;
    Generator<Entry> contents;
};

Generator<Entry> parse(Source & source);

}

WireFormatGenerator parseAndCopyDump(NARParseVisitor & sink, Source & source);
void parseDump(NARParseVisitor & sink, Source & source);

void restorePath(const Path & path, Source & source);

/**
 * Read a NAR from 'source' and return it as a generator.
 */
WireFormatGenerator copyNAR(Source & source);


inline constexpr std::string_view narVersionMagic1 = "nix-archive-1";

inline constexpr std::string_view caseHackSuffix = "~nix~case~hack~";


}
