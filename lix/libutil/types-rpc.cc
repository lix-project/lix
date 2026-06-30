#include "lix/libutil/types-rpc.hh"
#include "libutil/error.hh"
#include "libutil/fmt.hh"
#include "libutil/rpc.hh"
#include "types.capnp.h"
#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/common.h>
#include <kj/encoding.h>
#include <kj/exception.h>
#include <kj/io.h>
#include <optional>
#include <string_view>

namespace nix::rpc::error::v1 {

std::string encodeLossy(const ::nix::ErrorInfo & e)
{
    capnp::MallocMessageBuilder msg;
    RPC_FILL(msg, initRoot<Error>, e);

    const auto text = e.msg.str();

    kj::VectorOutputStream out;
    capnp::writePackedMessage(out, msg);
    return fmt(
        "%s %s%s%s",
        text.length() > 128 ? "(oversize message)" : text,
        V1_ERRORS->getHeader().cStr(),
        kj::encodeBase64(out.getArray()).cStr(),
        V1_ERRORS->getTrailer().cStr()
    );
}

std::optional<::nix::ErrorInfo> tryDecode(std::string_view source)
{
    const auto dataStart = source.rfind(V1_ERRORS->getHeader().cStr());
    if (dataStart == source.npos) {
        return std::nullopt;
    }
    source.remove_prefix(dataStart + V1_ERRORS->getHeader().size());
    const auto dataEnd = source.find(V1_ERRORS->getTrailer().cStr());
    if (dataEnd == source.npos) {
        return std::nullopt;
    }
    source = source.substr(0, dataEnd);

    auto decoded = kj::decodeBase64(kj::arrayPtr(source.begin(), source.end()));
    if (decoded.hadErrors) {
        return std::nullopt;
    }

    try {
        kj::ArrayInputStream stream{decoded};
        capnp::PackedMessageReader reader{stream};
        return from(reader.getRoot<Error>());
    } catch (kj::Exception & e) { // NOLINT(lix-foreign-exceptions): capnp packet format errors
        return std::nullopt;
    }
}
}
