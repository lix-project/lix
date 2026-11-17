#include "lix/libstore/log-store.hh"

namespace nix {

kj::Promise<Result<std::optional<std::string>>> LogStore::getBuildLog(const StorePath & path)
try {
    if (path.isDerivation()) {
        co_return TRY_AWAIT(getBuildLogExact(path));
    }

    try {
        if (auto info = TRY_AWAIT(queryPathInfo(path)); info->deriver) {
            co_return TRY_AWAIT(getBuildLogExact(*info->deriver));
        }
    } catch (InvalidPath &) {
    }

    co_return std::nullopt;
} catch (...) {
    co_return result::current_exception();
}
}
