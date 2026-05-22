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
    std::string name;

    void check(int err, const std::string & reason = "failed to extract archive (%s)");

    TarArchive(std::string name, Source & source, bool raw = false);

    TarArchive(const Path & path, std::optional<std::string> name = {});

    /// disable copy constructor
    TarArchive(const TarArchive &) = delete;

    void close();
};

kj::Promise<Result<void>> unpackTarfile(std::string name, AsyncInputStream & source, const Path & destDir);

void unpackTarfile(std::string name, const Path & tarFile, const Path & destDir);
}
