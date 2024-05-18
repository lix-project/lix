#pragma once
/**
 * @file
 *
 * Template implementations (as opposed to mere declarations).
 *
 * This file is an exmample of the "impl.hh" pattern. See the
 * contributing guide.
 */

#include "worker-protocol.hh"
#include "length-prefixed-protocol-helper.hh"

namespace nix {

/* protocol-agnostic templates */

#define WORKER_USE_LENGTH_PREFIX_SERIALISER(TEMPLATE, T) \
    TEMPLATE T WorkerProto::Serialise< T >::read(const Store & store, WorkerProto::ReadConn conn) \
    { \
        return LengthPrefixedProtoHelper<WorkerProto, T >::read(store, conn); \
    } \
    TEMPLATE [[nodiscard]] WireFormatGenerator WorkerProto::Serialise< T >::write(const Store & store, WorkerProto::WriteConn conn, const T & t) \
    { \
        return LengthPrefixedProtoHelper<WorkerProto, T >::write(store, conn, t); \
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
    static T read(const Store & store, WorkerProto::ReadConn conn)
    {
        return CommonProto::Serialise<T>::read(store,
            CommonProto::ReadConn { .from = conn.from });
    }
    [[nodiscard]]
    static WireFormatGenerator write(const Store & store, WorkerProto::WriteConn conn, const T & t)
    {
        return CommonProto::Serialise<T>::write(store,
            CommonProto::WriteConn { .to = conn.to },
            t);
    }
};

/* protocol-specific templates */

}
