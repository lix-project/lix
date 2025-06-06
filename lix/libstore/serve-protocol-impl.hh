#pragma once
/**
 * @file
 *
 * Template implementations (as opposed to mere declarations).
 *
 * This file is an exmample of the "impl.hh" pattern. See the
 * contributing guide.
 */

#include "lix/libstore/serve-protocol.hh"
#include "lix/libstore/length-prefixed-protocol-helper.hh"

namespace nix {

/* protocol-agnostic templates */

#define SERVE_USE_LENGTH_PREFIX_SERIALISER(TEMPLATE, T) \
    TEMPLATE T ServeProto::Serialise< T >::read(ServeProto::ReadConn conn) \
    { \
        return LengthPrefixedProtoHelper<ServeProto, T >::read(conn); \
    } \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */ \
    TEMPLATE [[nodiscard]] WireFormatGenerator ServeProto::Serialise< T >::write(ServeProto::WriteConn conn, const T & t) \
    { \
        return LengthPrefixedProtoHelper<ServeProto, T >::write(conn, t); \
    }

SERVE_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::vector<T>)
SERVE_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::set<T>)
SERVE_USE_LENGTH_PREFIX_SERIALISER(template<typename... Ts>, std::tuple<Ts...>)

#define COMMA_ ,
SERVE_USE_LENGTH_PREFIX_SERIALISER(
    template<typename K COMMA_ typename V>,
    std::map<K COMMA_ V>)
#undef COMMA_

/**
 * Use `CommonProto` where possible.
 */
template<typename T>
struct ServeProto::Serialise
{
    static T read(ServeProto::ReadConn conn)
    {
        return CommonProto::Serialise<T>::read(
            CommonProto::ReadConn{.from = conn.from, .store = conn.store}
        );
    }
    [[nodiscard]]
    static WireFormatGenerator write(ServeProto::WriteConn conn, const T & t)
    {
        return CommonProto::Serialise<T>::write(CommonProto::WriteConn{conn.store}, t);
    }
};

/* protocol-specific templates */

}
