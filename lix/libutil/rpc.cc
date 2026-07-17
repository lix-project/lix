#include "libutil/rpc.hh"
#include "libutil/error.hh"
#include "libutil/logging.hh"
#include "libutil/types-rpc.hh"
#include <exception>
#include <kj/async.h>

namespace nix::rpc::detail {
std::exception_ptr unwrapErrorRaw(kj::Exception & e, std::source_location loc)
{
    nix::Error fe{e.getDescription().cStr()};
    fe.addAsyncTrace(loc, "RPC call");
    return std::make_exception_ptr(std::move(fe));
}

std::exception_ptr unwrapErrorV1(kj::Exception & e, std::source_location loc)
{
    if (auto decoded = error::v1::tryDecode(e.getDescription().cStr())) {
        nix::Error fe(std::move(*decoded));
        fe.addAsyncTrace(loc, "RPC call");
        return std::make_exception_ptr(std::move(fe));
    } else {
        return unwrapErrorRaw(e, loc);
    }
}

void rethrowAsErrorV1()
{
    try {
        throw; // NOLINT(lix-foreign-exceptions)
    } catch (const nix::BaseError & e) {
        kj::throwFatalException(
            kj::Exception(kj::Exception::Type::FAILED, "remote", 0, kj::str(error::v1::encodeLossy(e.info())))
        );
    }
}

kj::Promise<void> flushLogger()
{
    return logger->flush().ignoreResult();
}
}
