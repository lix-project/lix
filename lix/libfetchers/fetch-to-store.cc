#include "lix/libfetchers/fetch-to-store.hh"
#include "lix/libfetchers/fetchers.hh"
#include "lix/libfetchers/cache.hh"

namespace nix {

kj::Promise<Result<StorePath>> fetchToStoreFlat(
    Store & store,
    const CheckedSourcePath & path,
    std::string_view name,
    RepairFlag repair)
try {
    Activity act(*logger, lvlChatty, actUnknown, fmt("copying '%s' to the store", path));
    auto physicalPath = path.canonical().abs();

    co_return settings.readOnlyMode
        ? store.computeStorePathForPathFlat(name, physicalPath)
        : TRY_AWAIT(store.addToStoreFlat(name, physicalPath, HashType::SHA256, repair));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<StorePath>> fetchToStoreRecursive(
    Store & store,
    const CheckedSourcePath & path,
    std::string_view name,
    PathFilter * filter,
    RepairFlag repair)
try {
    Activity act(*logger, lvlChatty, actUnknown, fmt("copying '%s' to the store", path));

    auto filter2 = filter ? *filter : defaultPathFilter;
    auto physicalPath = path.canonical().abs();

    co_return settings.readOnlyMode
        ? store.computeStorePathForPathRecursive(name, physicalPath, filter2)
        : TRY_AWAIT(store.addToStoreRecursive(name, physicalPath, HashType::SHA256, filter2, repair));
} catch (...) {
    co_return result::current_exception();
}


}
