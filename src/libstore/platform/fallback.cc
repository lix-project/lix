#include "platform/fallback.hh"

namespace nix {
static RegisterStoreImplementation<FallbackLocalStore, LocalStoreConfig> regLocalStore;
}
