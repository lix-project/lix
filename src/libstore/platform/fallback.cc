#include "platform/fallback.hh"

namespace nix {
void registerLocalStore() {
    Implementations::add<FallbackLocalStore, LocalStoreConfig>();
}
}
