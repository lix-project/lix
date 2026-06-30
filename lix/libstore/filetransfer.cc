#include "lix/libstore/filetransfer.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libstore/transferitem.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/c-calls.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/namespaces.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/s3.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/thread-name.hh"
#include "lix/libutil/tracepoint.hh"
#include "lix/libutil/backoff.hh"

#if ENABLE_DTRACE
#include "trace-probes.gen.hh"
#endif

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <regex>
#include <thread>
#include <unistd.h>

#if ENABLE_S3
#include <aws/core/client/ClientConfiguration.h>
#endif

#include <curl/curl.h>
#include <kj/async.h>
#include <kj/encoding.h>
#include <kj/time.h>

using namespace std::string_literals;

namespace nix {

FileTransferSettings fileTransferSettings;

struct CurlMulti
{
    std::unique_ptr<CURLM, decltype([](auto * m) { curl_multi_cleanup(m); })> curlm;

    const unsigned int baseRetryTimeMs;

    void unpause(const std::shared_ptr<TransferItem> & transfer)
    {
        auto lock = state_.lock();
        lock->unpause.push_back(transfer);
        wakeup();
    }

    void cancel(const std::shared_ptr<TransferItem> & transfer)
    {
        std::promise<void> promise;
        auto wait = promise.get_future();
        {
            auto lock = state_.lock();
            if (lock->quit) {
                return;
            }
            lock->cancel[transfer] = std::move(promise);
        }
        wakeup();
        wait.get();
    }

    struct State
    {
        bool quit = false;
        std::vector<std::shared_ptr<TransferItem>> incoming;
        std::vector<std::shared_ptr<TransferItem>> unpause;
        std::map<std::shared_ptr<TransferItem>, std::promise<void>> cancel;
    };

    Sync<State> state_;

    std::thread workerThread;

    CurlMulti(unsigned int baseRetryTimeMs)
        : curlm(curl_multi_init())
        , baseRetryTimeMs(baseRetryTimeMs)
    {
        if (curlm == nullptr) {
            throw FileTransferError(FileTransfer::Misc, {}, "could not allocate curl handle");
        }

        static std::once_flag globalInit;
        std::call_once(globalInit, curl_global_init, CURL_GLOBAL_ALL);

        curl_multi_setopt(curlm.get(), CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
        curl_multi_setopt(curlm.get(), CURLMOPT_MAX_TOTAL_CONNECTIONS,
            fileTransferSettings.httpConnections.get());

        workerThread = std::thread([&]() {
            setCurrentThreadName("curlFileTransfer worker");
            workerThreadEntry();
        });
    }

    ~CurlMulti()
    {
        try {
            stopWorkerThread();
        } catch (nix::Error & e) {
            // This can only fail if a socket to our own process cannot be
            // written to, so it is always a bug in the program if it fails.
            //
            // Joining the thread would probably only cause a deadlock if this
            // happened, so just die on purpose.
            printError("failed to join curl file transfer worker thread: %1%", e.what());
            std::terminate();
        }
        workerThread.join();
    }

    void wakeup()
    {
        if (auto mc = curl_multi_wakeup(curlm.get()))
            throw nix::Error("unexpected error from curl_multi_wakeup(): %s", curl_multi_strerror(mc));
    }

    void stopWorkerThread()
    {
        /* Signal the worker thread to exit. */
        {
            auto state(state_.lock());
            state->quit = true;
        }
        wakeup();
    }

    void workerThreadMain()
    {
        /* Cause this thread to be notified on SIGINT. */
        auto callback = createInterruptCallback([&]() {
            stopWorkerThread();
        });

        unshareFilesystem();

        std::map<CURL *, std::shared_ptr<TransferItem>> items;

        // clear all current transfers in case of an early exit, as can happen
        // via Interrupted if the interruption occured right before a log call
        KJ_DEFER({
            for (auto & [_, item] : items) {
                item->finish(CURLE_ABORTED_BY_CALLBACK);
            }

            // make a note that we're dying and acknowledge all pending cancel
            // requests by individual transfers. not doing this can cause bugs
            // like #1218 in which the process deadlocks waiting for transfers
            // to cancel with no download thread to make this happen; this was
            // likely caused by a transfer requesting a cancellation *exactly*
            // before a signal was received, causing the curl thread to die in
            // a hurry without processing cancellations. the transfer is stuck
            // from that point on, and since this happened in a destructor the
            // entire process locked up solid. curl exceptions could have also
            // caused this; we set the `quit` flag just in case to avoid this.
            auto lock = state_.lock();
            lock->quit = true;
            for (auto & [item, promise] : lock->cancel) {
                promise.set_value();
            }
        });

        bool quit = false;

        // NOTE: we will need to use CURLMOPT_TIMERFUNCTION to integrate this
        // loop with kj. until then curl will handle its timeouts internally.
        int64_t timeoutMs = INT64_MAX;

        while (true) {
            {
                auto cancel = [&] { return std::move(state_.lock()->cancel); }();
                for (auto & [item, promise] : cancel) {
                    curl_multi_remove_handle(curlm.get(), item->req.get());
                    items.erase(item->req.get());
                    promise.set_value();
                }
            }

            /* Let curl do its thing. */
            int running;
            CURLMcode mc = curl_multi_perform(curlm.get(), &running);
            if (mc != CURLM_OK)
                throw nix::Error("unexpected error from curl_multi_perform(): %s", curl_multi_strerror(mc));

            /* Set the promises of any finished requests. */
            CURLMsg * msg;
            int left;
            while ((msg = curl_multi_info_read(curlm.get(), &left))) {
                if (msg->msg == CURLMSG_DONE) {
                    auto i = items.find(msg->easy_handle);
                    assert(i != items.end());
                    i->second->finish(msg->data.result);
                    curl_multi_remove_handle(curlm.get(), i->second->req.get());
                    items.erase(i);
                }
            }

            // exit immediately and abort all running transfers. waiting for transfers to finish
            // before exiting this loop may hang the shutdown procedure forever, e.g. if blocked
            // transfers would be destroyed (thus aborted) after the curl thread for any reason.
            if (quit) {
                break;
            }

            /* Wait for activity, including wakeup events. */
            mc = curl_multi_poll(curlm.get(), nullptr, 0, std::min<int64_t>(timeoutMs, INT_MAX), nullptr);
            if (mc != CURLM_OK)
                throw nix::Error("unexpected error from curl_multi_poll(): %s", curl_multi_strerror(mc));

            /* Add new curl requests from the incoming requests queue,
               except for requests that are embargoed (waiting for a
               retry timeout to expire). */

            std::vector<std::shared_ptr<TransferItem>> incoming;

            timeoutMs = INT64_MAX;

            {
                auto unpause = [&] { return std::move(state_.lock()->unpause); }();
                for (auto & item : unpause) {
                    curl_easy_pause(item->req.get(), CURLPAUSE_CONT);
                }
            }

            {
                auto state(state_.lock());
                incoming = std::move(state->incoming);
                quit = state->quit;
            }

            for (auto & item : incoming) {
                debug("starting %s of %s", item->verb(), item->uri);
                curl_multi_add_handle(curlm.get(), item->req.get());
                items[item->req.get()] = item;
            }
        }

        debug("download thread shutting down");
    }

    void workerThreadEntry()
    {
        try {
            workerThreadMain();
        } catch (nix::Interrupted & e) {
        } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
            printError("unexpected error in download thread: %s", e.what());
        } catch (...) {
            printError("unexpected error in download thread");
        }

        {
            auto state(state_.lock());
            for (auto & item : state->incoming) {
                item->finish(CURLE_ABORTED_BY_CALLBACK);
            }
            state->incoming.clear();
            state->quit = true;
        }
    }

    void enqueueItem(std::shared_ptr<TransferItem> item)
    {
        if (item->uploadData
            && !item->uri.starts_with("http://")
            && !item->uri.starts_with("https://"))
            throw nix::Error("uploading to '%s' is not supported", item->uri);

        {
            auto state(state_.lock());
            if (state->quit)
                throw nix::Error("cannot enqueue download request because the download thread is shutting down");
            state->incoming.push_back(item);
        }
        wakeup();
    }
};

struct curlFileTransfer : public FileTransfer
{
    std::shared_ptr<CurlMulti> multi;

    curlFileTransfer(unsigned int baseRetryTimeMs)
        : multi(std::make_shared<CurlMulti>(baseRetryTimeMs))
    {
    }

    ~curlFileTransfer()
    {
        multi->stopWorkerThread();
    }

#if ENABLE_S3
    static std::tuple<std::string, std::string, StoreConfig::Params> parseS3Uri(std::string uri)
    {
        auto [path, params] = splitUriAndParams(uri);

        auto slash = path.find('/', 5); // 5 is the length of "s3://" prefix
            if (slash == std::string::npos)
                throw nix::Error("bad S3 URI '%s'", path);

        std::string bucketName(path, 5, slash - 5);
        std::string key(path, slash + 1);

        return {bucketName, key, params};
    }
#endif

    kj::Promise<Result<void>> upload(
        const std::string & uri,
        std::string data,
        FileTransferOptions options,
        const Activity * context
    ) override
    try {
        TRY_AWAIT(enqueueFileTransfer(uri, std::move(options), std::move(data), false, context));
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<std::optional<std::pair<FileTransferResult, box_ptr<AsyncInputStream>>>>>
    tryEagerTransfers(
        const std::string & uri,
        const FileTransferOptions & options,
        const std::optional<std::string> & data,
        bool noBody
    )
    try {
        // curl transfers using file:// urls cannot be paused, and are a bit unruly
        // in other ways too. since their metadata is trivial and we already have a
        // backend for simple file system reads we can use that instead. we'll pass
        // uploads to files to curl even so, those will fail in enqueueItem anyway.
        // on all other decoding failures we also let curl fail for us a bit later.
        //
        // note that we use kj to decode the url, not curl. curl uses only the path
        // component of the url to determine the file name, but it does note expose
        // the decoding method it uses for this. for file:// transfers curl forbids
        // only \0 characters in the urldecoded path, not all control characters as
        // it does in the public curl_url_get(CURLUPART_PATH, CURLU_URLDECODE) api.
        //
        // also note: everything weird you see here is for compatibility with curl.
        // we can't even fix it because nix-channel relies on this. even reading of
        // directories being allowed and returning something (though hopefully it's
        // enough to return anything instead of a directory listing like curl does)
        if (uri.starts_with("file://") && !data.has_value()) {
            if (!uri.starts_with("file:///")) {
                throw FileTransferError(NotFound, std::nullopt, "file not found");
            }
            auto url = curl_url();
            if (!url) {
                throw std::bad_alloc();
            }
            KJ_DEFER(curl_url_cleanup(url));
            curl_url_set(url, CURLUPART_URL, requireCString(uri), 0);
            char * path = nullptr;
            curl_url_get(url, CURLUPART_PATH, &path, 0);
            auto decoded = kj::decodeUriComponent(kj::arrayPtr(path, path + strlen(path)));
            if (!decoded.hadErrors && decoded.findFirst(0) == nullptr) {
                Path fsPath(decoded.cStr(), decoded.size());
                FileTransferResult metadata{.effectiveUri = std::string("file://") + path};
                struct stat st;
                AutoCloseFD fd(sys::open(fsPath, O_RDONLY));
                if (!fd || fstat(fd.get(), &st) != 0) {
                    throw FileTransferError(
                        NotFound, std::nullopt, "%s: file not found (%s)", fsPath, strerror(errno)
                    );
                }
                if (S_ISDIR(st.st_mode)) {
                    co_return std::pair{
                        std::move(metadata), make_box_ptr<AsyncStringInputStream>("")
                    };
                }
                struct OwningFdStream : AsyncInputStream
                {
                    AutoCloseFD fd;
                    OwningFdStream(AutoCloseFD fd) : fd(std::move(fd)) {}
                    kj::Promise<Result<std::optional<size_t>>>
                    read(void * buffer, size_t size) override
                    {
                        // NOTE the synchronous implementation used to have a buffer for
                        // file data, but we cannot be bothered to treat this edge case.
                        if (const auto got = ::read(fd.get(), buffer, size); got >= 0) {
                            if (got == 0) {
                                return {result::success(std::nullopt)};
                            } else {
                                return {result::success(got)};
                            }
                        } else {
                            return {result::failure(std::make_exception_ptr(SysError("reading file")
                            ))};
                        }
                    }
                };
                co_return std::pair{
                    std::move(metadata), make_box_ptr<OwningFdStream>(std::move(fd))
                };
            }
        }

        /* Ugly hack to support s3:// URIs. */
        if (uri.starts_with("s3://")) {
            // FIXME: do this on a worker thread
#if ENABLE_S3
            auto [bucketName, key, params] = parseS3Uri(uri);

            std::string profile = getOr(params, "profile", "");
            std::string region = getOr(params, "region", Aws::Region::US_EAST_1);
            std::string scheme = getOr(params, "scheme", "");
            std::string endpoint = getOr(params, "endpoint", "");

            S3Helper s3Helper(profile, region, scheme, endpoint);

            // FIXME: implement ETag
            auto s3Res = TRY_AWAIT(s3Helper.getObject(bucketName, key));
            FileTransferResult res;
            if (!s3Res.data)
                throw FileTransferError(NotFound, "S3 object '%s' does not exist", uri);
            struct OwningStringStream : private std::string, AsyncStringInputStream
            {
                OwningStringStream(std::string data)
                    : std::string(std::move(data))
                    , AsyncStringInputStream(*this)
                {
                }
            };
            co_return std::pair{res, make_box_ptr<OwningStringStream>(std::move(*s3Res.data))};
#else
            throw nix::Error(
                "cannot download '%s' because Lix is not built with S3 support", uri
            );
#endif
        }

        co_return std::nullopt;
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<std::pair<FileTransferResult, box_ptr<AsyncInputStream>>>>
    enqueueFileTransfer(
        const std::string & uri,
        FileTransferOptions && options,
        std::optional<std::string> data,
        bool noBody,
        const Activity * context
    )
    try {
        if (auto eager = TRY_AWAIT(tryEagerTransfers(uri, options, data, noBody))) {
            co_return std::move(*eager);
        }

        auto source = make_box_ptr<TransferStream>(
            *this, uri, std::move(options), std::move(data), noBody, context
        );
        TRY_AWAIT(source->init());
        TRY_AWAIT(source->awaitData());
        co_return {source->metadata, std::move(source)};
    } catch (...) {
        co_return result::current_exception();
    }

    struct TransferStream : AsyncInputStream
    {
        std::shared_ptr<CurlMulti> parent;
        std::string uri;
        FileTransferOptions options;
        std::optional<std::string> data;
        bool noBody;
        const Activity * parentAct;

        std::shared_ptr<TransferItem> transfer;
        FileTransferResult metadata;
        std::string chunk;
        std::string_view buffered;

        const size_t tries = fileTransferSettings.tries;
        curl_off_t totalReceived = 0;

        Generator<BackoffTiming> backoff;

        TransferStream(
            curlFileTransfer & parent,
            const std::string & uri,
            FileTransferOptions && options,
            std::optional<std::string> data,
            bool noBody,
            const Activity * context
        )
            : parent(parent.multi)
            , uri(uri)
            , options(options)
            , data(std::move(data))
            , noBody(noBody)
            , parentAct(context)
            , backoff(backoffTimeouts(
                  fileTransferSettings.tries,
                  std::chrono::seconds(fileTransferSettings.maxConnectTimeout.get()),
                  std::chrono::seconds(fileTransferSettings.initialConnectTimeout.get()),
                  std::chrono::milliseconds(this->parent->baseRetryTimeMs)
              ))
        {
        }

        ~TransferStream()
        {
            // wake up the download thread if it's still going and have it abort
            try {
                if (transfer) {
                    parent->cancel(transfer);
                }
            } catch (...) {
                ignoreExceptionInDestructor();
            }
        }

        kj::Promise<Result<void>> init()
        try {
            metadata = TRY_AWAIT(withRetries(
                [&]() {
                    return startTransfer(
                        uri, std::chrono::seconds(fileTransferSettings.initialConnectTimeout.get())
                    );
                },
                [&](const std::chrono::milliseconds & timeout) {
                    return startTransfer(uri, timeout);
                }
            ));
            co_return result::success();
        } catch (...) {
            co_return result::current_exception();
        }

        auto withRetries(auto && initial, auto && retry) -> decltype(initial())
        try {
            std::optional<std::string> retryContext;
            BackoffTiming timings;
            while (true) {
                try {
                    if (retryContext) {
                        TRY_AWAIT(prepareRetry(*retryContext, timings.waitTime, timings.attempt));
                        co_return TRY_AWAIT(retry(timings.downloadTimeout));
                    } else {
                        co_return TRY_AWAIT(initial());
                    }
                } catch (FileTransferError & e) {
                    auto next = backoff.next();
                    // If this is a transient error, then maybe retry after a while. after any
                    // bytes have been received we require range support to proceed, otherwise
                    // we'd need to start from scratch and discard everything we already have.
                    if (e.error != Transient || data.has_value() || !next.has_value()
                        || (totalReceived > 0 && !transfer->acceptsRanges()))
                    {
                        throw;
                    }
                    retryContext = e.what();
                    timings = *next;
                }
            }
        } catch (...) {
            co_return result::current_exception();
        }

        kj::Promise<Result<FileTransferResult>> startTransfer(
            const std::string & uri,
            const std::chrono::milliseconds & timeout,
            curl_off_t offset = 0
        )
        try {
            auto uploadData = data ? std::optional(std::string_view(*data)) : std::nullopt;
            auto pfp = kj::newPromiseAndCrossThreadFulfiller<Result<FileTransferResult>>();
            transfer = std::make_shared<TransferItem>(
                uri,
                std::move(options),
                parentAct,
                uploadData,
                noBody,
                offset,
                std::move(pfp.fulfiller),
                timeout
            );
            parent->enqueueItem(transfer);
            co_return TRY_AWAIT(pfp.promise);
        } catch (...) {
            co_return result::current_exception();
        }

        kj::Promise<Result<void>> prepareRetry(
            const std::string & context,
            const std::chrono::milliseconds & waitTime,
            unsigned int attempt
        )
        try {
            if (totalReceived) {
                printTaggedWarning(
                    "%s; retrying from offset %d in %d ms (attempt %d/%d)",
                    Uncolored(context),
                    totalReceived,
                    waitTime.count(),
                    Uncolored(attempt),
                    Uncolored(tries)
                );
            } else {
                printTaggedWarning(
                    "%s; retrying in %d ms (attempt %d/%d)",
                    Uncolored(context),
                    waitTime.count(),
                    Uncolored(attempt),
                    Uncolored(tries)
                );
            }

            co_await AIO().provider.getTimer().afterDelay(waitTime.count() * kj::MILLISECONDS);
            co_return result::success();
        } catch (...) {
            co_return result::current_exception();
        }

        kj::Promise<Result<void>> restartTransfer(const std::chrono::milliseconds & timeout)
        try {
            auto onChange =
                [&](std::string_view what, std::string_view from, std::string_view to, bool throw_
                ) -> void {
                if (!from.empty() && from != to) {
                    FileTransferError e = FileTransferError(
                        Misc,
                        {},
                        "uri %s changed %s from %s to %s during transfer",
                        uri,
                        what,
                        from,
                        to
                    );

                    if (throw_) {
                        throw e;
                    }

                    logWarning(e.info());
                }
            };

            // use the effective URI of the previous transfer for retries. this avoids
            // some silent corruption if a redirect changes between starting and retry
            const auto & uri = metadata.effectiveUri.empty() ? this->uri : metadata.effectiveUri;

            auto newMeta = TRY_AWAIT(startTransfer(uri, timeout, totalReceived));

            onChange("final destination", metadata.effectiveUri, newMeta.effectiveUri, false);
            onChange("ETag", metadata.etag, newMeta.etag, true);
            onChange(
                "immutable url",
                metadata.immutableUrl.value_or(""),
                newMeta.immutableUrl.value_or(""),
                true
            );
            co_return result::success();
        } catch (...) {
            co_return result::current_exception();
        }

        kj::Promise<Result<bool>> waitForData()
        try {
            /* Grab data if available, otherwise wait for the download
               thread to wake us up. */
            std::optional<kj::Promise<void>> signal;

            while (buffered.empty()) {
                if (signal) {
                    co_await *signal;
                    signal.reset();
                }

                auto state(transfer->downloadState.lock());

                if (!state->data.empty()) {
                    chunk = std::move(state->data);
                    buffered = chunk;
                    totalReceived += chunk.size();
                    parent->unpause(transfer);
                } else if (state->exc) {
                    std::rethrow_exception(state->exc);
                } else if (state->done) {
                    co_return false;
                } else {
                    parent->unpause(transfer);
                    signal = state->wait();
                }
            }

            co_return true;
        } catch (...) {
            co_return result::current_exception();
        }

        kj::Promise<Result<bool>> restartAndWaitForData(const std::chrono::milliseconds & timeout)
        try {
            TRY_AWAIT(restartTransfer(timeout));
            co_return TRY_AWAIT(waitForData());
        } catch (...) {
            co_return result::current_exception();
        }

        kj::Promise<Result<bool>> awaitData()
        try {
            co_return TRY_AWAIT(withRetries(
                [&] { return waitForData(); },
                [&](const std::chrono::milliseconds & timeout) {
                    return restartAndWaitForData(timeout);
                }
            ));
        } catch (...) {
            co_return result::current_exception();
        }

        kj::Promise<Result<std::optional<size_t>>> read(void * buffer, size_t len) override
        try {
            TRACE(LIX_STORE_FILETRANSFER_READ(uri.c_str(), len));

            size_t total = 0;
            auto data = static_cast<char *>(buffer);
            while (total < len && TRY_AWAIT(awaitData())) {
                const auto available = std::min(len - total, buffered.size());
                memcpy(data + total, buffered.data(), available);
                buffered.remove_prefix(available);
                total += available;
            }

            if (total == 0) {
                co_return std::nullopt;
            } else {
                co_return total;
            }
        } catch (...) {
            co_return result::current_exception();
        }
    };

    kj::Promise<Result<bool>>
    exists(const std::string & uri, FileTransferOptions options, const Activity * context) override
    try {
        try {
            TRY_AWAIT(enqueueFileTransfer(uri, std::move(options), std::nullopt, true, context));
            co_return true;
        } catch (FileTransferError & e) {
            /* S3 buckets return 403 if a file doesn't exist and the
                bucket is unlistable, so treat 403 as 404. */
            if (e.error == FileTransfer::NotFound || e.error == FileTransfer::Forbidden)
                co_return false;
            throw;
        }
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<std::pair<FileTransferResult, box_ptr<AsyncInputStream>>>> download(
        const std::string & uri, FileTransferOptions options, const Activity * context
    ) override
    {
        return enqueueFileTransfer(uri, std::move(options), std::nullopt, false, context);
    }
};

ref<curlFileTransfer> makeCurlFileTransfer(std::optional<unsigned int> baseRetryTimeMs)
{
    return make_ref<curlFileTransfer>(baseRetryTimeMs.value_or(250));
}

ref<FileTransfer> getFileTransfer()
{
    static ref<curlFileTransfer> fileTransfer = makeCurlFileTransfer({});

    if (fileTransfer->multi->state_.lock()->quit) {
        fileTransfer = makeCurlFileTransfer({});
    }

    return fileTransfer;
}

ref<FileTransfer> makeFileTransfer(std::optional<unsigned int> baseRetryTimeMs)
{
    return makeCurlFileTransfer(baseRetryTimeMs);
}

}
