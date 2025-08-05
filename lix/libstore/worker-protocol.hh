#pragma once
///@file

#include "lix/libstore/common-protocol.hh"
#include "lix/libutil/serialise-async.hh"
#include "path-info.hh"
#include <kj/async.h>

namespace nix {


#define WORKER_MAGIC_1 0x6e697863
#define WORKER_MAGIC_2 0x6478696f

// This must remain 1.35 (Nix 2.18) forever in Lix, since the protocol has
// diverged in CppNix such that we cannot assign newer versions ourselves, the
// protocol is bad in design and implementation and Lix intends to replace it
// entirely.
#define PROTOCOL_VERSION (1 << 8 | 35)
#define MIN_SUPPORTED_MINOR_WORKER_PROTO_VERSION 35
#define MIN_SUPPORTED_WORKER_PROTO_VERSION (1 << 8 | MIN_SUPPORTED_MINOR_WORKER_PROTO_VERSION)

#define GET_PROTOCOL_MAJOR(x) ((x) & 0xff00)
#define GET_PROTOCOL_MINOR(x) ((x) & 0x00ff)


#define REMOVE_AFTER_DROPPING_PROTO_MINOR(protoMinor) \
    static_assert(MIN_SUPPORTED_MINOR_WORKER_PROTO_VERSION <= (protoMinor))


#define STDERR_NEXT  0x6f6c6d67
#define STDERR_LAST  0x616c7473
#define STDERR_ERROR 0x63787470
#define STDERR_START_ACTIVITY 0x53545254
#define STDERR_STOP_ACTIVITY  0x53544f50
#define STDERR_RESULT         0x52534c54


class Store;
struct Source;

// items being serialised
struct DerivedPath;
struct BuildResult;
struct KeyedBuildResult;
struct ValidPathInfo;
struct UnkeyedValidPathInfo;
enum TrustedFlag : bool;


/**
 * The "worker protocol", used by unix:// and ssh-ng:// stores.
 *
 * This `struct` is basically just a `namespace`; We use a type rather
 * than a namespace just so we can use it as a template argument.
 */
struct WorkerProto
{
    /**
     * Enumeration of all the request types for the protocol.
     */
    enum struct Op : uint64_t;

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

        ReadConn(Source & from, const Store & store, Version version)
            : from(from)
            , store(store)
            , version(version)
        {
            assert(version >= MIN_SUPPORTED_WORKER_PROTO_VERSION);
        }
    };

    /**
     * A unidirectional write connection, to be used by the write half of the
     * canonical serializers below.
     */
    struct WriteConn {
        const Store & store;
        Version version;

        WriteConn(const Store & store, Version version) : store(store), version(version)
        {
            assert(version >= MIN_SUPPORTED_WORKER_PROTO_VERSION);
        }
    };

    /**
     * Data type for canonical pairs of serialisers for the worker protocol.
     *
     * See https://en.cppreference.com/w/cpp/language/adl for the broader
     * concept of what is going on here.
     */
    template<typename T>
    struct Serialise;
    // This is the definition of `Serialise` we *want* to put here, but
    // do not do so.
    //
    // The problem is that if we do so, C++ will think we have
    // seralisers for *all* types. We don't, of course, but that won't
    // cause an error until link time. That makes for long debug cycles
    // when there is a missing serialiser.
    //
    // By not defining it globally, and instead letting individual
    // serialisers specialise the type, we get back the compile-time
    // errors we would like. When no serialiser exists, C++ sees an
    // abstract "incomplete" type with no definition, and any attempt to
    // use `to` or `from` static methods is a compile-time error because
    // they don't exist on an incomplete type.
    //
    // This makes for a quicker debug cycle, as desired.
#if 0
    {
        static T read(ReadConn conn);
        static WireFormatGenerator write(WriteConn conn, const T & t);
    };
#endif

    /**
     * Wrapper function around `WorkerProto::Serialise<T>::write` that allows us to
     * infer the type instead of having to write it down explicitly.
     */
    template<typename T>
    [[nodiscard]]
    static WireFormatGenerator write(WriteConn conn, const T & t)
    {
        return WorkerProto::Serialise<T>::write(conn, t);
    }

    /**
     * Create a `WorkerProto::ReadConn` from the async input stream `from` and pass
     * it to `fn`. `fn` will be run asynchronously on a fresh stack using kj fibers
     * and can thus safely use synchronous deserializers with very little overhead.
     */
    static auto readAsync(auto & from, Store & store, WorkerProto::Version version, auto fn)
    {
        return deserializeFrom(from, [&store, version, fn{std::move(fn)}](Source & wrapped) {
            return fn(WorkerProto::ReadConn{wrapped, store, version});
        });
    }
};

enum struct WorkerProto::Op : uint64_t
{
    IsValidPath = 1,
    HasSubstitutes = 3, // obsolete since 2012, stubbed to error
    QueryPathHash = 4, // obsolete since 2016, stubbed to error
    QueryReferences = 5, // obsolete since 2016, stubbed to error
    QueryReferrers = 6,
    AddToStore = 7,
    AddTextToStore = 8, // obsolete, removed
    BuildPaths = 9,
    EnsurePath = 10,
    AddTempRoot = 11,
    AddIndirectRoot = 12,
    SyncWithGC = 13, // obsolete since CppNix 2.5.0, removed
    FindRoots = 14,
    ExportPath = 16, // obsolete since 2017, stubbed to error
    QueryDeriver = 18, // obsolete since 2016, stubbed to error
    SetOptions = 19,
    CollectGarbage = 20,
    QuerySubstitutablePathInfo = 21,
    QueryDerivationOutputs = 22, // obsolete, removed
    QueryAllValidPaths = 23,
    QueryFailedPaths = 24, // obsolete, removed
    ClearFailedPaths = 25, // obsolete, removed
    QueryPathInfo = 26,
    ImportPaths = 27, // obsolete since 2016
    QueryDerivationOutputNames = 28, // obsolete since CppNix 2.4
    QueryPathFromHashPart = 29,
    QuerySubstitutablePathInfos = 30,
    QueryValidPaths = 31,
    QuerySubstitutablePaths = 32,
    QueryValidDerivers = 33,
    OptimiseStore = 34,
    VerifyStore = 35,
    BuildDerivation = 36,
    AddSignatures = 37,
    NarFromPath = 38,
    AddToStoreNar = 39,
    QueryMissing = 40,
    QueryDerivationOutputMap = 41,
    RegisterDrvOutput = 42,
    QueryRealisation = 43,
    AddMultipleToStore = 44,
    AddBuildLog = 45,
    BuildPathsWithResults = 46,
};

/**
 * Convenience for sending operation codes.
 *
 * @todo Switch to using `WorkerProto::Serialise` instead probably. But
 * this was not done at this time so there would be less churn.
 */
inline Sink & operator << (Sink & sink, WorkerProto::Op op)
{
    return sink << (uint64_t) op;
}

/**
 * Convenience for debugging.
 *
 * @todo Perhaps render known opcodes more nicely.
 */
inline std::ostream & operator << (std::ostream & s, WorkerProto::Op op)
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
#define DECLARE_WORKER_SERIALISER(T) \
    struct WorkerProto::Serialise< T > \
    { \
        static T read(WorkerProto::ReadConn conn); \
        [[nodiscard]] static WireFormatGenerator write(WorkerProto::WriteConn conn, const T & t); \
    };

template<>
DECLARE_WORKER_SERIALISER(DerivedPath);
template<>
DECLARE_WORKER_SERIALISER(BuildResult);
template<>
DECLARE_WORKER_SERIALISER(KeyedBuildResult);
template<>
DECLARE_WORKER_SERIALISER(ValidPathInfo);
template<>
DECLARE_WORKER_SERIALISER(UnkeyedValidPathInfo);
template<>
DECLARE_WORKER_SERIALISER(std::optional<TrustedFlag>);
template<>
DECLARE_WORKER_SERIALISER(std::optional<UnkeyedValidPathInfo>);
template<>
DECLARE_WORKER_SERIALISER(SubstitutablePathInfo);

template<typename T>
DECLARE_WORKER_SERIALISER(std::vector<T>);
template<typename T>
DECLARE_WORKER_SERIALISER(std::set<T>);
template<typename... Ts>
DECLARE_WORKER_SERIALISER(std::tuple<Ts...>);

#define COMMA_ ,
template<typename K, typename V>
DECLARE_WORKER_SERIALISER(std::map<K COMMA_ V>);
#undef COMMA_

}
