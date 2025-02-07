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

    co_return
        settings.readOnlyMode
        ? store.computeStorePathForPath(name, path.canonical().abs(), method, HashType::SHA256, filter2).first
        : store.addToStore(name, path.canonical().abs(), method, HashType::SHA256, filter2, repair);
} catch (...) {
    co_return result::current_exception();
}


}
