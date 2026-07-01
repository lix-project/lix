#include <cstddef>
#include <new>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include <curl/urlapi.h>
#include <kj/async.h>
#include <kj/common.h>
#include <kj/encoding.h>
#include <sys/stat.h>
#include <unistd.h>

#if ENABLE_S3
#include <aws/core/client/ClientConfiguration.h>
#endif

#include "lix/libstore/curlfiletransfer.hh"
#include "lix/libstore/curlmulti.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libstore/s3.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/c-calls.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/tracepoint.hh"
#include "lix/libutil/types.hh"

#if ENABLE_DTRACE
#include "trace-probes.gen.hh"
#endif

namespace nix {

curlFileTransfer::curlFileTransfer(unsigned int baseRetryTimeMs)
    : multi(std::make_shared<CurlMulti>(baseRetryTimeMs))
{
}

curlFileTransfer::~curlFileTransfer()
{
    multi->stopWorkerThread();
}

#if ENABLE_S3
std::tuple<std::string, std::string, StoreConfig::Params> curlFileTransfer::parseS3Uri(std::string uri)
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

kj::Promise<Result<void>> curlFileTransfer::upload(
    const std::string & uri,
    std::string data,
    FileTransferOptions options,
    const Activity * context
)
try {
    TRY_AWAIT(enqueueFileTransfer(uri, std::move(options), std::move(data), false, context));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<std::pair<FileTransferResult, box_ptr<AsyncInputStream>>>>>
curlFileTransfer::tryEagerTransfers(
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
curlFileTransfer::enqueueFileTransfer(
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

curlFileTransfer::TransferStream::TransferStream(
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

curlFileTransfer::TransferStream::~TransferStream()
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

kj::Promise<Result<void>> curlFileTransfer::TransferStream::init()
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

kj::Promise<Result<FileTransferResult>> curlFileTransfer::TransferStream::startTransfer(
    const std::string & uri,
    const std::chrono::milliseconds & timeout,
    curl_off_t offset
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

kj::Promise<Result<void>> curlFileTransfer::TransferStream::prepareRetry(
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

kj::Promise<Result<void>> curlFileTransfer::TransferStream::restartTransfer(const std::chrono::milliseconds & timeout)
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

kj::Promise<Result<bool>> curlFileTransfer::TransferStream::waitForData()
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
            chunk = std::exchange(state->data, {});
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

kj::Promise<Result<bool>> curlFileTransfer::TransferStream::restartAndWaitForData(const std::chrono::milliseconds & timeout)
try {
    TRY_AWAIT(restartTransfer(timeout));
    co_return TRY_AWAIT(waitForData());
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<bool>> curlFileTransfer::TransferStream::awaitData()
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

kj::Promise<Result<std::optional<size_t>>> curlFileTransfer::TransferStream::read(void * buffer, size_t len)
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

kj::Promise<Result<bool>>
curlFileTransfer::exists(const std::string & uri, FileTransferOptions options, const Activity * context)
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

kj::Promise<Result<std::pair<FileTransferResult, box_ptr<AsyncInputStream>>>> curlFileTransfer::download(
    const std::string & uri, FileTransferOptions options, const Activity * context
)
{
    return enqueueFileTransfer(uri, std::move(options), std::nullopt, false, context);
}

}
