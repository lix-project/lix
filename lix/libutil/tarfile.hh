#pragma once
///@file

#include "async-io.hh"
#include "lix/libutil/serialise.hh"
#include <archive.h>

namespace nix {

MakeError(ArchiveError, Error);

struct TarArchive
{
    std::unique_ptr<struct archive, decltype([](auto * p) { archive_read_free(p); })> archive;
    Source * source;
    std::vector<unsigned char> buffer;

    void check(int err, const std::string & reason = "failed to extract archive (%s)");

    TarArchive(Source & source, bool raw = false);

    TarArchive(const Path & path);

    /// disable copy constructor
    TarArchive(const TarArchive &) = delete;

    void close();
};

kj::Promise<Result<void>> unpackTarfile(AsyncInputStream & source, const Path & destDir);

void unpackTarfile(const Path & tarFile, const Path & destDir);

}
