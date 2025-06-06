#pragma once
/**
 * @file
 *
 * Template implementations (as opposed to mere declarations).
 *
 * This file is an exmample of the "impl.hh" pattern. See the
 * contributing guide.
 */

#include "lix/libstore/common-protocol.hh"
#include "lix/libstore/length-prefixed-protocol-helper.hh"

namespace nix {

/* protocol-agnostic templates */

#define COMMON_USE_LENGTH_PREFIX_SERIALISER(TEMPLATE, T) \
    TEMPLATE T CommonProto::Serialise< T >::read(CommonProto::ReadConn conn) \
    { \
        return LengthPrefixedProtoHelper<CommonProto, T >::read(conn); \
    } \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */ \
    TEMPLATE [[nodiscard]] WireFormatGenerator CommonProto::Serialise< T >::write(CommonProto::WriteConn conn, const T & t) \
    { \
        return LengthPrefixedProtoHelper<CommonProto, T >::write(conn, t); \
    }

COMMON_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::vector<T>)
COMMON_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::set<T>)
COMMON_USE_LENGTH_PREFIX_SERIALISER(template<typename... Ts>, std::tuple<Ts...>)

#define COMMA_ ,
COMMON_USE_LENGTH_PREFIX_SERIALISER(
    template<typename K COMMA_ typename V>,
    std::map<K COMMA_ V>)
#undef COMMA_


/* protocol-specific templates */

}
