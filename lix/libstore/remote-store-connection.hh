#pragma once
///@file

#include "lix/libstore/remote-store.hh"
#include "lix/libstore/worker-protocol.hh"
#include "lix/libstore/worker-protocol-impl.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/io-buffer.hh"
#include "lix/libutil/pool.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/serialise-async.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/signals.hh"
#include <kj/async.h>
#include <tuple>
#include <type_traits>
#include <utility>

namespace nix {

/**
 * Bidirectional connection (send and receive) used by the Remote Store
 * implementation.
 *
 * Contains a socket fd and IO buffer for actual communication, along with
 * other information learned when negotiating the connection.
 */
struct RemoteStore::Connection
{
    /**
     * Receive buffer, shared between sync Sources and async Streams.
     * All buffered receiving sources or streams using the `getFD()`
     * fd must use this buffer, or they will corrupt the connection.
     */
    ref<IoBuffer> fromBuf{make_ref<IoBuffer>()};

    /**
     * Returns the file descriptors socket backing this connection. A
     * connection must be backed by a socket, not by a pair of pipes.
     */
    virtual int getFD() const = 0;

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

    virtual ~Connection() = default;

    // wrapper type for remote errors because `Result<std::exception_ptr>`
    // does not work very well and `Result<Result<void>>` is too confusing
    struct [[nodiscard]] RemoteError
    {
        std::exception_ptr e;
    };

    kj::Promise<Result<RemoteError>> processStderr(AsyncFdIoStream & stream);
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

    ConnectionHandle(Pool<RemoteStore::Connection>::Handle && handle) : handle(std::move(handle)) {}

    RemoteStore::Connection & operator * () { return *handle; }
    RemoteStore::Connection * operator -> () { return &*handle; }

    kj::Promise<Result<void>> processStderr(AsyncFdIoStream & stream);

    kj::Promise<Result<void>> withFramedStream(
        AsyncFdIoStream & stream,
        std::function<kj::Promise<Result<void>>(AsyncOutputStream & stream)> fun
    );

    template<typename R = void, typename... Args>
    kj::Promise<Result<R>> sendCommandUninterruptible(Args &&... args)
    try {
        constexpr auto LastArgIdx = sizeof...(Args) - 1;
        using AllArgsT = std::tuple<Args &&...>;
        using LastArgT = std::tuple_element_t<LastArgIdx, AllArgsT>;

        // invalidate this connection if we're cancelled early, e.g. by a user ^C.
        // regular exceptions must be handled elsewhere due the subframe requests.
        // this also invalidates connections if a request was sent while unwinding
        // the stack, but that's sufficiently suspect to warrant being as careful.
        auto invalidateOnCancel = kj::defer([&] {
            if (std::uncaught_exceptions() == 0) {
                handle.markBad();
            }
        });

        AsyncFdIoStream stream{AsyncFdIoStream::shared_fd{}, handle->getFD()};

        // if the last argument can be serialized normally we will serialize *all*
        // arguments at once and hand off to the remote. if the last argument does
        // not have a serializer we assume it's a callback for a subframe protocol
        // and serialize all *preceding* arguments normally before handing over to
        // the subframing layer (which is then responsible for any error handling)
        if constexpr (requires(StringSink s) { s << std::declval<LastArgT>(); }) {
            try {
                StringSink msg;
                ((msg << std::forward<Args>(args)), ...);
                TRY_AWAIT(stream.writeFull(msg.s.data(), msg.s.size()));
            } catch (...) {
                handle.markBad();
                throw;
            }
            LIX_TRY_AWAIT(processStderr(stream));
        } else {
            using ImmediateArgsIdxs = std::make_index_sequence<sizeof...(Args) - 1>;
            AllArgsT allArgs(std::forward<Args>(args)...);

                try {
                    StringSink msg;
                    [&]<size_t... Ids>(std::integer_sequence<size_t, Ids...>) {
                        ((msg << std::forward<std::tuple_element_t<Ids, AllArgsT>>(
                              std::get<Ids>(allArgs)
                          )),
                         ...);
                    }(ImmediateArgsIdxs{});
                    TRY_AWAIT(stream.writeFull(msg.s.data(), msg.s.size()));
                } catch (...) {
                    handle.markBad();
                    throw;
                }

                LIX_TRY_AWAIT(withFramedStream(stream, std::get<LastArgIdx>(allArgs)));
        }

        if constexpr (std::is_void_v<R>) {
            invalidateOnCancel.cancel();
            co_return result::success();
        } else {
            try {
                AsyncBufferedInputStream from{stream, handle->fromBuf};
                auto result = LIX_TRY_AWAIT(WorkerProto::readAsync(
                    from, *handle->store, handle->daemonVersion, WorkerProto::Serialise<R>::read
                ));
                invalidateOnCancel.cancel();
                co_return result;
            } catch (...) {
                handle.markBad();
                throw;
            }
        }
    } catch (...) {
        co_return result::current_exception();
    }

    template<typename R = void, typename... Args>
    kj::Promise<Result<R>> sendCommand(Args &&... args)
    {
        return makeInterruptible(sendCommandUninterruptible<R>(std::forward<Args>(args)...));
    }
};

}
