#include "lix/libutil/archive.hh"
#include "lix/libstore/fs-accessor.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/local-fs-store.hh"
#include "lix/libstore/globals.hh"
#include "lix/libutil/compression.hh"
#include "lix/libstore/derivations.hh"

namespace nix {

struct LocalStoreAccessor : public FSAccessor
{
    ref<LocalFSStore> store;

    LocalStoreAccessor(ref<LocalFSStore> store) : store(store) { }

    Path toRealPath(const Path & path, bool requireValidPath = true)
    {
        auto storePath = store->toStorePath(path).first;
        if (requireValidPath && !store->isValidPath(storePath))
            throw InvalidPath("path '%1%' does not exist in the store", store->printStorePath(storePath));
        return store->getRealStoreDir() + std::string(path, store->config().storeDir.size());
    }

    FSAccessor::Stat stat(const Path & path) override
    {
        auto realPath = toRealPath(path);

        struct stat st;
        if (lstat(realPath.c_str(), &st)) {
            if (errno == ENOENT || errno == ENOTDIR) return {Type::tMissing, 0, false};
            throw SysError("getting status of '%1%'", path);
        }

        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
            throw Error("file '%1%' has unsupported type", path);

        return {
            S_ISREG(st.st_mode) ? Type::tRegular :
            S_ISLNK(st.st_mode) ? Type::tSymlink :
            Type::tDirectory,
            S_ISREG(st.st_mode) ? (uint64_t) st.st_size : 0,
            S_ISREG(st.st_mode) && st.st_mode & S_IXUSR};
    }

    StringSet readDirectory(const Path & path) override
    {
        auto realPath = toRealPath(path);

        auto entries = nix::readDirectory(realPath);

        StringSet res;
        for (auto & entry : entries)
            res.insert(entry.name);

        return res;
    }

    std::string readFile(const Path & path, bool requireValidPath = true) override
    {
        return nix::readFile(toRealPath(path, requireValidPath));
    }

    std::string readLink(const Path & path) override
    {
        return nix::readLink(toRealPath(path));
    }
};

ref<FSAccessor> LocalFSStore::getFSAccessor()
{
    return make_ref<LocalStoreAccessor>(ref<LocalFSStore>(
            std::dynamic_pointer_cast<LocalFSStore>(shared_from_this())));
}

box_ptr<Source> LocalFSStore::narFromPath(const StorePath & path)
{
    if (!isValidPath(path))
        throw Error("path '%s' does not exist in store", printStorePath(path));
    return make_box_ptr<GeneratorSource>(dumpPath(getRealStoreDir() + std::string(printStorePath(path), config().storeDir.size())));
}

const std::string LocalFSStore::drvsLogDir = "drvs";

std::optional<std::string> LocalFSStore::getBuildLogExact(const StorePath & path)
{
    auto baseName = path.to_string();

    for (int j = 0; j < 2; j++) {

        Path logPath =
            j == 0
            ? fmt("%s/%s/%s/%s", config().logDir, drvsLogDir, baseName.substr(0, 2), baseName.substr(2))
            : fmt("%s/%s/%s", config().logDir, drvsLogDir, baseName);
        Path logBz2Path = logPath + ".bz2";

        if (pathExists(logPath))
            return readFile(logPath);

        else if (pathExists(logBz2Path)) {
            try {
                return decompress("bzip2", readFile(logBz2Path));
            } catch (Error &) { }
        }

    }

    return std::nullopt;
}

}
