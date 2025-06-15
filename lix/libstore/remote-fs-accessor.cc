#include "lix/libstore/remote-fs-accessor.hh"
#include "lix/libstore/nar-accessor.hh"
#include "lix/libutil/json.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace nix {

RemoteFSAccessor::RemoteFSAccessor(ref<Store> store, const Path & cacheDir)
    : store(store)
    , cacheDir(cacheDir)
{
    if (cacheDir != "")
        createDirs(cacheDir);
}

Path RemoteFSAccessor::makeCacheFile(std::string_view hashPart, const std::string & ext)
{
    assert(cacheDir != "");
    return fmt("%s/%s.%s", cacheDir, hashPart, ext);
}

kj::Promise<Result<ref<FSAccessor>>>
RemoteFSAccessor::addToCache(std::string_view hashPart, std::string && nar)
try {
    if (cacheDir != "") {
        try {
            /* FIXME: do this asynchronously. */
            writeFile(makeCacheFile(hashPart, "nar"), nar);
        } catch (...) {
            ignoreExceptionExceptInterrupt();
        }
    }

    auto narAccessor = makeNarAccessor(std::move(nar));
    nars.emplace(hashPart, narAccessor);

    if (cacheDir != "") {
        try {
            JSON j = TRY_AWAIT(listNar(narAccessor, "", true));
            writeFile(makeCacheFile(hashPart, "ls"), j.dump());
        } catch (...) {
            ignoreExceptionExceptInterrupt();
        }
    }

    co_return narAccessor;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::pair<ref<FSAccessor>, Path>>>
RemoteFSAccessor::fetch(const Path & path_, bool requireValidPath)
try {
    auto path = canonPath(path_);

    auto [storePath, restPath] = store->toStorePath(path);

    if (requireValidPath && !TRY_AWAIT(store->isValidPath(storePath)))
        throw InvalidPath("path '%1%' does not exist in remote store", store->printStorePath(storePath));

    auto i = nars.find(std::string(storePath.hashPart()));
    if (i != nars.end()) co_return {i->second, restPath};

    std::string listing;
    Path cacheFile;

    if (cacheDir != "" && pathExists(cacheFile = makeCacheFile(storePath.hashPart(), "nar"))) {

        try {
            listing = nix::readFile(makeCacheFile(storePath.hashPart(), "ls"));

            auto narAccessor = makeLazyNarAccessor(listing,
                [cacheFile](uint64_t offset, uint64_t length) {

                    AutoCloseFD fd{open(cacheFile.c_str(), O_RDONLY | O_CLOEXEC)};
                    if (!fd)
                        throw SysError("opening NAR cache file '%s'", cacheFile);

                    if (lseek(fd.get(), offset, SEEK_SET) != (off_t) offset)
                        throw SysError("seeking in '%s'", cacheFile);

                    std::string buf(length, 0);
                    readFull(fd.get(), buf.data(), length);

                    return buf;
                });

            nars.emplace(storePath.hashPart(), narAccessor);
            co_return {narAccessor, restPath};

        } catch (SysError &) { }

        try {
            auto narAccessor = makeNarAccessor(nix::readFile(cacheFile));
            nars.emplace(storePath.hashPart(), narAccessor);
            co_return {narAccessor, restPath};
        } catch (SysError &) { }
    }

    StringSink sink;
    TRY_AWAIT(TRY_AWAIT(store->narFromPath(storePath))->drainInto(sink));
    co_return {TRY_AWAIT(addToCache(storePath.hashPart(), std::move(sink.s))), restPath};
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<FSAccessor::Stat>> RemoteFSAccessor::stat(const Path & path)
try {
    auto res = TRY_AWAIT(fetch(path));
    co_return TRY_AWAIT(res.first->stat(res.second));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<StringSet>> RemoteFSAccessor::readDirectory(const Path & path)
try {
    auto res = TRY_AWAIT(fetch(path));
    co_return TRY_AWAIT(res.first->readDirectory(res.second));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::string>>
RemoteFSAccessor::readFile(const Path & path, bool requireValidPath)
try {
    auto res = TRY_AWAIT(fetch(path, requireValidPath));
    co_return TRY_AWAIT(res.first->readFile(res.second));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::string>> RemoteFSAccessor::readLink(const Path & path)
try {
    auto res = TRY_AWAIT(fetch(path));
    co_return TRY_AWAIT(res.first->readLink(res.second));
} catch (...) {
    co_return result::current_exception();
}

}
