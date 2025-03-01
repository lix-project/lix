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
    const PreparedDump & contents,
    std::string_view name,
    RepairFlag repair)
try {
    Activity act(
        *logger, lvlChatty, actUnknown, fmt("copying '%s' to the store", contents.rootPath)
    );

    co_return settings.readOnlyMode
        ? store.computeStorePathForPathRecursive(name, contents)
        : TRY_AWAIT(store.addToStoreRecursive(name, contents, HashType::SHA256, repair));
} catch (...) {
    co_return result::current_exception();
}


}
