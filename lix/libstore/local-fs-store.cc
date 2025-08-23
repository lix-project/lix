#include "lix/libutil/archive.hh"
#include "lix/libstore/fs-accessor.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/local-fs-store.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/compression.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/result.hh"

namespace nix {

kj::Promise<Result<Path>> LocalStoreAccessor::toRealPath(const Path & path, bool requireValidPath)
try {
    auto storePath = store->toStorePath(path).first;
    if (requireValidPath && !TRY_AWAIT(store->isValidPath(storePath))) {
        throw InvalidPath(
            "path '%1%' does not exist in the store", store->printStorePath(storePath)
        );
    }
    co_return store->getRealStoreDir() + std::string(path, store->config().storeDir.size());
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<FSAccessor::Stat>> LocalStoreAccessor::stat(const Path & path)
try {
    auto realPath = TRY_AWAIT(toRealPath(path));

    struct stat st;
    if (lstat(realPath.c_str(), &st)) {
        if (errno == ENOENT || errno == ENOTDIR) {
            co_return {Type::tMissing, 0, false};
        }
        throw SysError("getting status of '%1%'", path);
    }

    if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        throw Error("file '%1%' has unsupported type", path);
    }

    co_return {
        S_ISREG(st.st_mode)       ? Type::tRegular
            : S_ISLNK(st.st_mode) ? Type::tSymlink
                                  : Type::tDirectory,
        S_ISREG(st.st_mode) ? (uint64_t) st.st_size : 0,
        S_ISREG(st.st_mode) && st.st_mode & S_IXUSR
    };
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<StringSet>> LocalStoreAccessor::readDirectory(const Path & path)
try {
    auto realPath = TRY_AWAIT(toRealPath(path));

    auto entries = nix::readDirectory(realPath);

    StringSet res;
    for (auto & entry : entries) {
        res.insert(entry.name);
    }

    co_return res;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::string>>
LocalStoreAccessor::readFile(const Path & path, bool requireValidPath)
try {
    co_return nix::readFile(TRY_AWAIT(toRealPath(path, requireValidPath)));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::string>> LocalStoreAccessor::readLink(const Path & path)
try {
    co_return nix::readLink(TRY_AWAIT(toRealPath(path)));
} catch (...) {
    co_return result::current_exception();
}

ref<FSAccessor> LocalFSStore::getFSAccessor()
{
    return make_ref<LocalStoreAccessor>(ref<LocalFSStore>::unsafeFromPtr(
            std::dynamic_pointer_cast<LocalFSStore>(shared_from_this())));
}

kj::Promise<Result<box_ptr<AsyncInputStream>>>
LocalFSStore::narFromPath(const StorePath & path, const Activity * context)
try {
    if (!TRY_AWAIT(isValidPath(path, context))) {
        throw Error("path '%s' does not exist in store", printStorePath(path));
    }
    co_return make_box_ptr<AsyncGeneratorInputStream>(
        dumpPath(getRealStoreDir() + std::string(printStorePath(path), config().storeDir.size()))
    );
} catch (...) {
    co_return result::current_exception();
}

const std::string LocalFSStore::drvsLogDir = "drvs";

kj::Promise<Result<std::optional<std::string>>>
LocalFSStore::getBuildLogExact(const StorePath & path)
try {
    auto baseName = path.to_string();

    for (int j = 0; j < 2; j++) {

        Path logPath =
            j == 0
            ? fmt("%s/%s/%s/%s", config().logDir, drvsLogDir, baseName.substr(0, 2), baseName.substr(2))
            : fmt("%s/%s/%s", config().logDir, drvsLogDir, baseName);
        Path logBz2Path = logPath + ".bz2";

        if (pathExists(logPath))
            co_return readFile(logPath);

        else if (pathExists(logBz2Path)) {
            try {
                co_return decompress("bzip2", readFile(logBz2Path));
            } catch (Error &) { }
        }

    }

    co_return std::nullopt;
} catch (...) {
    co_return result::current_exception();
}

}
