#include "lix/libfetchers/fetch-to-store.hh"
#include "lix/libfetchers/fetchers.hh"
#include "lix/libfetchers/cache.hh"

namespace nix {

kj::Promise<Result<StorePath>> fetchToStore(
    Store & store,
    const CheckedSourcePath & path,
    std::string_view name,
    FileIngestionMethod method,
    PathFilter * filter,
    RepairFlag repair)
try {
    Activity act(*logger, lvlChatty, actUnknown, fmt("copying '%s' to the store", path));

    auto filter2 = filter ? *filter : defaultPathFilter;
    auto physicalPath = path.canonical().abs();

    if (settings.readOnlyMode) {
        switch (method) {
        case FileIngestionMethod::Recursive:
            co_return store.computeStorePathForPathRecursive(name, physicalPath, filter2);
        case FileIngestionMethod::Flat:
            co_return store.computeStorePathForPathFlat(name, physicalPath);
        }
    } else {
        co_return TRY_AWAIT(
            store.addToStore(name, physicalPath, method, HashType::SHA256, filter2, repair)
        );
    }
} catch (...) {
    co_return result::current_exception();
}


}
