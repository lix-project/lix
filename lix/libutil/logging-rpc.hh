#pragma once
///@file

#include "lix/libutil/logging.capnp.h"
#include "logging.hh"
#include "rpc.hh" // IWYU pragma: keep
#include <optional>

namespace nix::rpc::log {
inline std::optional<nix::ActivityType> from(ActivityType at)
{
    // clang-format off
    switch (at) {
    case ActivityType::UNKNOWN:         return actUnknown;
    case ActivityType::COPY_PATH:       return actCopyPath;
    case ActivityType::FILE_TRANSFER:   return actFileTransfer;
    case ActivityType::REALISE:         return actRealise;
    case ActivityType::COPY_PATHS:      return actCopyPaths;
    case ActivityType::BUILDS:          return actBuilds;
    case ActivityType::BUILD:           return actBuild;
    case ActivityType::OPTIMISE_STORE:  return actOptimiseStore;
    case ActivityType::VERIFY_PATHS:    return actVerifyPaths;
    case ActivityType::SUBSTITUTE:      return actSubstitute;
    case ActivityType::QUERY_PATH_INFO: return actQueryPathInfo;
    case ActivityType::POST_BUILD_HOOK: return actPostBuildHook;
    case ActivityType::BUILD_WAITING:   return actBuildWaiting;
    default:                            return std::nullopt;
    }
    // clang-format on
}

inline ActivityType to(const nix::ActivityType & at)
{
    // clang-format off
    switch (at) {
    case actUnknown:       return log::ActivityType::UNKNOWN;
    case actCopyPath:      return log::ActivityType::COPY_PATH;
    case actFileTransfer:  return log::ActivityType::FILE_TRANSFER;
    case actRealise:       return log::ActivityType::REALISE;
    case actCopyPaths:     return log::ActivityType::COPY_PATHS;
    case actBuilds:        return log::ActivityType::BUILDS;
    case actBuild:         return log::ActivityType::BUILD;
    case actOptimiseStore: return log::ActivityType::OPTIMISE_STORE;
    case actVerifyPaths:   return log::ActivityType::VERIFY_PATHS;
    case actSubstitute:    return log::ActivityType::SUBSTITUTE;
    case actQueryPathInfo: return log::ActivityType::QUERY_PATH_INFO;
    case actPostBuildHook: return log::ActivityType::POST_BUILD_HOOK;
    case actBuildWaiting:  return log::ActivityType::BUILD_WAITING;
    }
    // clang-format on
}

inline std::optional<nix::ResultType> from(ResultType rt)
{
    // clang-format off
    switch (rt) {
    case ResultType::FILE_LINKED:         return resFileLinked;
    case ResultType::BUILD_LOG_LINE:      return resBuildLogLine;
    case ResultType::UNTRUSTED_PATH:      return resUntrustedPath;
    case ResultType::CORRUPTED_PATH:      return resCorruptedPath;
    case ResultType::SET_PHASE:           return resSetPhase;
    case ResultType::PROGRESS:            return resProgress;
    case ResultType::SET_EXPECTED:        return resSetExpected;
    case ResultType::POST_BUILD_LOG_LINE: return resPostBuildLogLine;
    default:                              return std::nullopt;
    }
    // clang-format on
}

inline ResultType to(const nix::ResultType & rt)
{
    // clang-format off
    switch (rt) {
    case resFileLinked:       return ResultType::FILE_LINKED;
    case resBuildLogLine:     return ResultType::BUILD_LOG_LINE;
    case resUntrustedPath:    return ResultType::UNTRUSTED_PATH;
    case resCorruptedPath:    return ResultType::CORRUPTED_PATH;
    case resSetPhase:         return ResultType::SET_PHASE;
    case resProgress:         return ResultType::PROGRESS;
    case resSetExpected:      return ResultType::SET_EXPECTED;
    case resPostBuildLogLine: return ResultType::POST_BUILD_LOG_LINE;
    }
    // clang-format on
}
}

namespace nix::rpc {
template<>
struct Convert<log::Event::Field, nix::Logger::Field>
{
    static nix::Logger::Field convert(log::Event::Field::Reader r, auto &&...)
    {
        if (r.isI()) {
            return {r.getI()};
        } else {
            return {to<std::string>(r.getS())};
        }
    }
};

template<>
struct Fill<log::Event::Field, Logger::Field>
{
    static void fill(log::Event::Field::Builder fb, const Logger::Field & e, auto &&...)
    {
        if (e.type == Logger::Field::tInt) {
            fb.setI(e.i);
        } else {
            LIX_RPC_FILL(fb, setS, e.s);
        }
    }
};
}

namespace nix::rpc::log {
/**
 * create an RPC-backed logger. this logger will flush its contents periodically
 * (current fixed to a 100ms interval) or once the log buffer fills up enough to
 * warrant immediate traffic (currently fixed to approximately 1 MiB of buffered
 * log traffic). *any* error caught during rpc calls will terminate the process.
 */
Logger * makeRpcLoggerClient(LogStream::Client remote);

class RpcLoggerServer : public LogStream::Server
{
private:
    const Activity & parent;
    std::map<ActivityId, Activity> activities;

public:
    RpcLoggerServer(const Activity & parent) : parent(parent) {}
    virtual ~RpcLoggerServer() noexcept(false) = default;

    kj::Promise<void> push(PushContext context) override;
    kj::Promise<void> synchronize(SynchronizeContext context) override;
};
}
