#pragma once
///@file

#include "lix/libstore/common-protocol.hh"
#include "lix/libutil/serialise-async.hh"

namespace nix {

#define SERVE_MAGIC_1 0x390c9deb
#define SERVE_MAGIC_2 0x5452eecb

// This must remain at 2.7 (Nix 2.18) forever in Lix, since the protocol
// versioning is monotonic, so if we ever change it in the future, it will
// break compatibility with any potential CppNix-originated protocol changes.
//
// Lix intends to replace this protocol entirely.
#define SERVE_PROTOCOL_VERSION (2 << 8 | 7)
#define GET_PROTOCOL_MAJOR(x) ((x) & 0xff00)
#define GET_PROTOCOL_MINOR(x) ((x) & 0x00ff)


class Store;
struct Source;

// items being serialised
struct BuildResult;
struct UnkeyedValidPathInfo;


/**
 * The "serve protocol", used by ssh:// stores.
 *
 * This `struct` is basically just a `namespace`; We use a type rather
 * than a namespace just so we can use it as a template argument.
 */
struct ServeProto
{
    /**
     * Enumeration of all the request types for the protocol.
     */
    enum struct Command : uint64_t;

    /**
     * Version type for the protocol.
     *
     * @todo Convert to struct with separate major vs minor fields.
     */
    using Version = unsigned int;

    /**
     * A unidirectional read connection, to be used by the read half of the
     * canonical serializers below.
     */
    struct ReadConn {
        Source & from;
        const Store & store;
        Version version;
    };

    /**
     * A unidirectional write connection, to be used by the write half of the
     * canonical serializers below.
     */
    struct WriteConn {
        const Store & store;
        Version version;
    };

    /**
     * Data type for canonical pairs of serialisers for the serve protocol.
     *
     * See https://en.cppreference.com/w/cpp/language/adl for the broader
     * concept of what is going on here.
     */
    template<typename T>
    struct Serialise;
    // This is the definition of `Serialise` we *want* to put here, but
    // do not do so.
    //
    // See `worker-protocol.hh` for a longer explanation.
#if 0
    {
        static T read(ReadConn conn);
        static WireFormatGenerator write(WriteConn conn, const T & t);
    };
#endif

    /**
     * Wrapper function around `ServeProto::Serialise<T>::write` that allows us to
     * infer the type instead of having to write it down explicitly.
     */
    template<typename T>
    [[nodiscard]]
    static WireFormatGenerator write(WriteConn conn, const T & t)
    {
        return ServeProto::Serialise<T>::write(conn, t);
    }

    /**
     * Create a `ServeProto::ReadConn` using the async input stream `from` and pass
     * it to `fn`. `fn` will be run asynchronously on a fresh stack using kj fibers
     * and can thus safely use synchronous deserializers with very little overhead.
     */
    static auto readAsync(auto & from, Store & store, ServeProto::Version version, auto fn)
    {
        return deserializeFrom(from, [&store, version, fn{std::move(fn)}](Source & wrapped) {
            return fn(ServeProto::ReadConn{wrapped, store, version});
        });
    }
};

enum struct ServeProto::Command : uint64_t
{
    QueryValidPaths = 1,
    QueryPathInfos = 2,
    DumpStorePath = 3,
    ImportPaths = 4,
    ExportPaths = 5,
    BuildPaths = 6,
    QueryClosure = 7,
    BuildDerivation = 8,
    AddToStoreNar = 9,
};

/**
 * Convenience for sending operation codes.
 *
 * @todo Switch to using `ServeProto::Serialize` instead probably. But
 * this was not done at this time so there would be less churn.
 */
inline Sink & operator << (Sink & sink, ServeProto::Command op)
{
    return sink << (uint64_t) op;
}

/**
 * Convenience for debugging.
 *
 * @todo Perhaps render known opcodes more nicely.
 */
inline std::ostream & operator << (std::ostream & s, ServeProto::Command op)
{
    return s << (uint64_t) op;
}

/**
 * Declare a canonical serialiser pair for the worker protocol.
 *
 * We specialise the struct merely to indicate that we are implementing
 * the function for the given type.
 *
 * Some sort of `template<...>` must be used with the caller for this to
 * be legal specialization syntax. See below for what that looks like in
 * practice.
 */
#define DECLARE_SERVE_SERIALISER(T) \
    struct ServeProto::Serialise< T > \
    { \
        static T read(ServeProto::ReadConn conn); \
        [[nodiscard]] static WireFormatGenerator write(ServeProto::WriteConn conn, const T & t); \
    };

template<>
DECLARE_SERVE_SERIALISER(BuildResult);
template<>
DECLARE_SERVE_SERIALISER(UnkeyedValidPathInfo);

template<typename T>
DECLARE_SERVE_SERIALISER(std::vector<T>);
template<typename T>
DECLARE_SERVE_SERIALISER(std::set<T>);
template<typename... Ts>
DECLARE_SERVE_SERIALISER(std::tuple<Ts...>);

#define COMMA_ ,
template<typename K, typename V>
DECLARE_SERVE_SERIALISER(std::map<K COMMA_ V>);
#undef COMMA_

}
