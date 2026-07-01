#pragma once
///@file

#include "lix/libstore/filetransfer.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/transferitem.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/backoff.hh"
#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/result.hh"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <kj/async.h>

namespace nix {

struct CurlMulti;

struct curlFileTransfer : public FileTransfer
{
    // Types.
public:
    template<typename T>
    using Async = kj::Promise<Result<T>>;

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
        );

        ~TransferStream();

        kj::Promise<Result<void>> init();

        inline auto withRetries(auto && initial, auto && retry) -> decltype(initial())
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
        );

        kj::Promise<Result<void>> prepareRetry(
            const std::string & context,
            const std::chrono::milliseconds & waitTime,
            unsigned int attempt
        );

        kj::Promise<Result<void>> restartTransfer(const std::chrono::milliseconds & timeout);

        kj::Promise<Result<bool>> waitForData();

        kj::Promise<Result<bool>> restartAndWaitForData(const std::chrono::milliseconds & timeout);

        kj::Promise<Result<bool>> awaitData();

        kj::Promise<Result<std::optional<size_t>>> read(void * buffer, size_t len) override;
    };

    // Fields.
public:
    std::shared_ptr<CurlMulti> multi;

    // Specials.
public:
    curlFileTransfer(unsigned int baseRetryTimeMs);
    ~curlFileTransfer();

#if ENABLE_S3
    using S3Uri = std::tuple<std::string, std::string, StoreConfig::Params>;
    static auto parseS3Uri(std::string url) -> S3Uri;
#endif

    // Overrides.
public:
    auto upload(
        std::string const & uri,
        std::string data,
        FileTransferOptions options,
        Activity const * context
    ) -> Async<void> override;

    auto exists(
        std::string const & uri,
        FileTransferOptions options,
        Activity const * context
    ) -> Async<bool> override;

    auto download(
        std::string const & uri,
        FileTransferOptions options,
        Activity const * context = nullptr
    ) -> Async<std::pair<FileTransferResult, box_ptr<AsyncInputStream>>> override;

    // Actual API.
public:
    auto tryEagerTransfers(
        std::string const & url,
        FileTransferOptions const & options,
        std::optional<std::string> const & data,
        bool noBody
    ) -> Async<std::optional<std::pair<FileTransferResult, box_ptr<AsyncInputStream>>>>;

    auto enqueueFileTransfer(
        std::string const & uri,
        FileTransferOptions && options,
        std::optional<std::string> data,
        bool noBody,
        Activity const * context
    ) -> Async<std::pair<FileTransferResult, box_ptr<AsyncInputStream>>>;
};

}
