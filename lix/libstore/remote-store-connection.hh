#pragma once
///@file

#include "lix/libstore/remote-store.hh"
#include "lix/libstore/worker-protocol.hh"
#include "lix/libstore/worker-protocol-impl.hh"
#include "lix/libutil/pool.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/serialise.hh"
#include <type_traits>

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

    std::exception_ptr processStderr(bool flush = true);
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

    void processStderr(bool flush = true);

    kj::Promise<Result<void>>
    withFramedSinkAsync(std::function<kj::Promise<Result<void>>(Sink & sink)> fun);

    template<typename R = void, typename... Args>
    kj::Promise<Result<R>> sendCommand(Args &&... args)
    try {
        ((handle->to << std::forward<Args>(args)), ...);
        processStderr();
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
        std::packaged_task<void()> stderrHandler;

        explicit FramedSinkHandler(ConnectionHandle & conn, ThreadPool & handlerThreads);

        ~FramedSinkHandler() noexcept(false);
    };
};

}
