#include "platform/fallback.hh"

namespace nix {
void registerLocalStore() {
    Implementations::addConfigOnly<LocalStoreConfig>();
}
}
