#include "lix/libexpr/function-trace.hh"
#include "lix/libutil/logging.hh"

namespace nix {

FunctionCallTrace::FunctionCallTrace(const Pos & pos) : pos(pos) {
    auto duration = std::chrono::high_resolution_clock::now().time_since_epoch();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
    printMsg(lvlInfo, "function-trace entered %1% at %2%", Uncolored(pos), Uncolored(ns.count()));
}

FunctionCallTrace::~FunctionCallTrace() {
    auto duration = std::chrono::high_resolution_clock::now().time_since_epoch();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
    printMsg(lvlInfo, "function-trace exited %1% at %2%", Uncolored(pos), Uncolored(ns.count()));
}

}
