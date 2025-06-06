#pragma once
/**
 * @file
 *
 * Template implementations (as opposed to mere declarations).
 *
 * This file is an exmample of the "impl.hh" pattern. See the
 * contributing guide.
 */

#include "lix/libstore/worker-protocol.hh"
#include "lix/libstore/length-prefixed-protocol-helper.hh"

namespace nix {

/* protocol-agnostic templates */

#define WORKER_USE_LENGTH_PREFIX_SERIALISER(TEMPLATE, T) \
    TEMPLATE T WorkerProto::Serialise< T >::read(WorkerProto::ReadConn conn) \
    { \
        return LengthPrefixedProtoHelper<WorkerProto, T >::read(conn); \
    } \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */ \
    TEMPLATE [[nodiscard]] WireFormatGenerator WorkerProto::Serialise< T >::write(WorkerProto::WriteConn conn, const T & t) \
    { \
        return LengthPrefixedProtoHelper<WorkerProto, T >::write(conn, t); \
    }

WORKER_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::vector<T>)
WORKER_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::set<T>)
WORKER_USE_LENGTH_PREFIX_SERIALISER(template<typename... Ts>, std::tuple<Ts...>)

#define COMMA_ ,
WORKER_USE_LENGTH_PREFIX_SERIALISER(
    template<typename K COMMA_ typename V>,
    std::map<K COMMA_ V>)
#undef COMMA_

/**
 * Use `CommonProto` where possible.
 */
template<typename T>
struct WorkerProto::Serialise
{
    static T read(WorkerProto::ReadConn conn)
    {
        return CommonProto::Serialise<T>::read(
            CommonProto::ReadConn{.from = conn.from, .store = conn.store}
        );
    }
    [[nodiscard]]
    static WireFormatGenerator write(WorkerProto::WriteConn conn, const T & t)
    {
        return CommonProto::Serialise<T>::write(CommonProto::WriteConn{.store = conn.store}, t);
    }
};

/* protocol-specific templates */

}
