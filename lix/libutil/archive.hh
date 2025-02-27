#pragma once
///@file

#include "lix/libutil/generator.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/file-system.hh"


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
    /**
     * A type-erased file handle specific to this particular NARParseVisitor.
     */
    struct FileHandle
    {
        FileHandle() {}
        FileHandle(FileHandle const &) = delete;
        FileHandle & operator=(FileHandle &) = delete;

        /** Puts one block of data into the file */
        virtual void receiveContents(std::string_view data) = 0;

        /**
         * Explicitly closes the file. Further operations may throw an assert.
         * This exists so that closing can fail and throw an exception without doing so in a destructor.
         */
        virtual void close() = 0;

        virtual ~FileHandle() = default;
    };

    virtual ~NARParseVisitor() = default;

    virtual box_ptr<NARParseVisitor> createDirectory(const std::string & name) = 0;

    /**
     * Creates a regular file in the extraction output with the given size and executable flag.
     * The size is guaranteed to be the true size of the file.
     */
    virtual box_ptr<FileHandle> createRegularFile(const std::string & name, uint64_t size, bool executable) = 0;

    virtual void createSymlink(const std::string & name, const std::string & target) = 0;
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
    bool executable;
    uint64_t size;
    Generator<Bytes> contents;
};

struct Symlink
{
    Path target;
};

struct Directory
{
    Generator<std::pair<const std::string &, Entry>> contents;
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
