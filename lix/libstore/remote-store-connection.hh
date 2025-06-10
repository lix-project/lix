#pragma once
///@file

#include "lix/libstore/remote-store.hh"
#include "lix/libstore/worker-protocol.hh"
#include "lix/libstore/worker-protocol-impl.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/pool.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/serialise.hh"
#include <kj/async.h>
#include <tuple>
#include <type_traits>
#include <utility>

namespace nix {

/**
 * Bidirectional connection (send and receive) used by the Remote Store
 * implementation.
 *
 * Contains `Source` and `Sink` for actual communication, along with
 * other information learned when negotiating the connection.
 */
struct RemoteStore::Connection
{
    /**
     * Send with this.
     */
    FdSink to;

    /**
     * Receive with this.
     */
    FdSource from;

    /**
     * The store this connection belongs to.
     */
    Store * store;

    /**
     * The worker protocol version of the connected daemon. This may be newer
     * than this Lix supports.
     */
    WorkerProto::Version daemonVersion;

    /**
     * Whether the remote side trusts us or not.
     *
     * 3 values: "yes", "no", or `std::nullopt` for "unknown".
     *
     * Note that the "remote side" might not be just the end daemon, but
     * also an intermediary forwarder that can make its own trusting
     * decisions. This would be the intersection of all their trust
     * decisions, since it takes only one link in the chain to start
     * denying operations.
     */
    std::optional<TrustedFlag> remoteTrustsUs;

    /**
     * The version of the Nix daemon that is processing our requests.
     *
     * Do note, it may or may not communicating with another daemon,
     * rather than being an "end" `LocalStore` or similar.
     */
    std::optional<std::string> daemonNixVersion;

    /**
     * Time this connection was established.
     */
    std::chrono::time_point<std::chrono::steady_clock> startTime;

    /**
     * Coercion to `WorkerProto::ReadConn`. This makes it easy to use the
     * factored out worker protocol searlizers with a
     * `RemoteStore::Connection`.
     *
     * The worker protocol connection types are unidirectional, unlike
     * this type.
     */
    operator WorkerProto::ReadConn ()
    {
        return WorkerProto::ReadConn{from, *store, daemonVersion};
    }

    /**
     * Coercion to `WorkerProto::WriteConn`. This makes it easy to use the
     * factored out worker protocol searlizers with a
     * `RemoteStore::Connection`.
     *
     * The worker protocol connection types are unidirectional, unlike
     * this type.
     */
    operator WorkerProto::WriteConn ()
    {
        return WorkerProto::WriteConn{*store, daemonVersion};
    }

    virtual ~Connection();

    // wrapper type for remote errors because `Result<std::exception_ptr>`
    // does not work very well and `Result<Result<void>>` is too confusing
    struct [[nodiscard]] RemoteError
    {
        std::exception_ptr e;
    };

    kj::Promise<Result<RemoteError>> processStderr();
};

/**
 * A wrapper around Pool<RemoteStore::Connection>::Handle that marks
 * the connection as bad (causing it to be closed) if a non-daemon
 * exception is thrown before the handle is closed. Such an exception
 * causes a deviation from the expected protocol and therefore a
 * desynchronization between the client and daemon.
 */
struct RemoteStore::ConnectionHandle
{
    Pool<RemoteStore::Connection>::Handle handle;
    Sync<ThreadPool> & handlerThreads;
    bool daemonException = false;

    ConnectionHandle(
        Pool<RemoteStore::Connection>::Handle && handle, Sync<ThreadPool> & handlerThreads
    )
        : handle(std::move(handle))
        , handlerThreads(handlerThreads)
    {
    }

    ConnectionHandle(ConnectionHandle && h)
        : handle(std::move(h.handle))
        , handlerThreads(h.handlerThreads)
        , daemonException(h.daemonException)
    {
        h.daemonException = false;
    }

    ~ConnectionHandle();

    RemoteStore::Connection & operator * () { return *handle; }
    RemoteStore::Connection * operator -> () { return &*handle; }

    kj::Promise<Result<void>> processStderr();

    kj::Promise<Result<void>>
    withFramedSinkAsync(std::function<kj::Promise<Result<void>>(Sink & sink)> fun);

    template<typename R = void, typename... Args>
    kj::Promise<Result<R>> sendCommand(Args &&... args)
    try {
        constexpr auto LastArgIdx = sizeof...(Args) - 1;
        using AllArgsT = std::tuple<Args &&...>;
        using LastArgT = std::tuple_element_t<LastArgIdx, AllArgsT>;

        // if the last argument can be serialized normally we will serialize *all*
        // arguments at once and hand off to the remote. if the last argument does
        // not have a serializer we assume it's a callback for a subframe protocol
        // and serialize all *preceding* arguments normally before handing over to
        // the subframing layer (which is then responsible for any error handling)
        if constexpr (requires { handle->to << std::declval<LastArgT>(); }) {
            ((handle->to << std::forward<Args>(args)), ...);
            handle->to.flush();
            LIX_TRY_AWAIT(processStderr());
        } else {
            using ImmediateArgsIdxs = std::make_index_sequence<sizeof...(Args) - 1>;
            AllArgsT allArgs(std::forward<Args>(args)...);

            [&]<size_t... Ids>(std::integer_sequence<size_t, Ids...>) {
                ((handle->to << std::forward<std::tuple_element_t<Ids, AllArgsT>>(
                      std::get<Ids>(allArgs)
                  )),
                 ...);
                handle->to.flush();
            }(ImmediateArgsIdxs{});

            LIX_TRY_AWAIT(withFramedSinkAsync(std::get<LastArgIdx>(allArgs)));
        }

        if constexpr (std::is_void_v<R>) {
            co_return result::success();
        } else {
            co_return WorkerProto::Serialise<R>::read(*handle);
        }
    } catch (...) {
        co_return result::current_exception();
    }

private:
    struct FramedSinkHandler
    {
        std::exception_ptr ex;
        std::packaged_task<void(AsyncIoRoot &)> stderrHandler;

        explicit FramedSinkHandler(ConnectionHandle & conn, ThreadPool & handlerThreads);

        ~FramedSinkHandler() noexcept(false);
    };
};

}
