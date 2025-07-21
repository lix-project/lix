#if ENABLE_S3

#include "lix/libstore/s3.hh"
#include "lix/libstore/s3-binary-cache-store.hh"
#include "lix/libstore/nar-info.hh"
#include "lix/libstore/nar-info-disk-cache.hh"
#include "lix/libstore/globals.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/compression.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/strings.hh"

#include <kj/async.h>
#include <memory>

#include <aws/core/Aws.h>
#include <aws/core/VersionConfig.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/core/client/AsyncCallerContext.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/client/DefaultRetryStrategy.h>
#include <aws/core/utils/logging/FormattedLogSystem.h>
#include <aws/core/utils/logging/LogMacros.h>
#include <aws/core/utils/threading/Executor.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/ListObjectsRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/transfer/TransferHandle.h>
#include <aws/transfer/TransferManager.h>

using namespace Aws::Transfer;

namespace nix {

struct S3Error : public Error
{
    Aws::S3::S3Errors err;

    template<typename... Args>
    S3Error(Aws::S3::S3Errors err, const Args & ... args)
        : Error(args...), err(err) { };
};

/* Helper: given an Outcome<R, E>, return R in case of success, or
   throw an exception in case of an error. */
template<typename R, typename E>
R && checkAws(std::string_view s, Aws::Utils::Outcome<R, E> && outcome)
{
    if (!outcome.IsSuccess())
        throw S3Error(
            outcome.GetError().GetErrorType(),
            s + ": " + outcome.GetError().GetMessage());
    return outcome.GetResultWithOwnership();
}

class AwsLogger : public Aws::Utils::Logging::FormattedLogSystem
{
    using Aws::Utils::Logging::FormattedLogSystem::FormattedLogSystem;

    void ProcessFormattedStatement(Aws::String && statement) override
    {
        // FIXME: workaround for truly excessive log spam in debug level: https://github.com/aws/aws-sdk-cpp/pull/3003
        if ((statement.find("(SSLDataIn)") != std::string::npos || statement.find("(SSLDataOut)") != std::string::npos) && verbosity <= lvlDebug) {
            return;
        }
        debug("AWS: %s", chomp(statement));
    }

    void Flush() override {}
};

static void initAWS()
{
    static std::once_flag flag;
    std::call_once(flag, []() {
        Aws::SDKOptions options;

        /* We install our own OpenSSL locking function (see
           shared.cc), so don't let aws-sdk-cpp override it. */
        options.cryptoOptions.initAndCleanupOpenSSL = false;

        if (verbosity >= lvlDebug) {
            options.loggingOptions.logLevel =
                verbosity == lvlDebug
                ? Aws::Utils::Logging::LogLevel::Debug
                : Aws::Utils::Logging::LogLevel::Trace;
            options.loggingOptions.logger_create_fn = [options]() {
                return std::make_shared<AwsLogger>(options.loggingOptions.logLevel);
            };
        }

        Aws::InitAPI(options);
    });
}

S3Helper::S3Helper(
    const std::string & profile,
    const std::string & region,
    const std::string & scheme,
    const std::string & endpoint)
    : config(makeConfig(region, scheme, endpoint))
    , client(make_ref<Aws::S3::S3Client>(
            profile == ""
            ? std::dynamic_pointer_cast<Aws::Auth::AWSCredentialsProvider>(
                std::make_shared<Aws::Auth::DefaultAWSCredentialsProviderChain>())
            : std::dynamic_pointer_cast<Aws::Auth::AWSCredentialsProvider>(
                std::make_shared<Aws::Auth::ProfileConfigFileAWSCredentialsProvider>(profile.c_str())),
            *config,
            Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never,
            endpoint.empty()))
{
}

/* Log AWS retries. */
class RetryStrategy : public Aws::Client::DefaultRetryStrategy
{
    bool ShouldRetry(const Aws::Client::AWSError<Aws::Client::CoreErrors>& error, long attemptedRetries) const override
    {
        auto retry = Aws::Client::DefaultRetryStrategy::ShouldRetry(error, attemptedRetries);
        if (retry)
            printError("AWS error '%s' (%s), will retry in %d ms",
                error.GetExceptionName(),
                error.GetMessage(),
                CalculateDelayBeforeNextRetry(error, attemptedRetries));
        return retry;
    }
};

ref<Aws::Client::ClientConfiguration> S3Helper::makeConfig(
    const std::string & region,
    const std::string & scheme,
    const std::string & endpoint)
{
    initAWS();
    auto res = make_ref<Aws::Client::ClientConfiguration>();
    res->region = region;
    if (!scheme.empty()) {
        res->scheme = Aws::Http::SchemeMapper::FromString(scheme.c_str());
    }
    if (!endpoint.empty()) {
        res->endpointOverride = endpoint;
    }
    res->requestTimeoutMs = 600 * 1000;
    res->connectTimeoutMs = 5 * 1000;
    res->retryStrategy = std::make_shared<RetryStrategy>();
    res->caFile = settings.caFile;
    // Use the system proxy env-vars in curl for s3, which is off by default for some reason
    res->allowSystemProxy = true;
    return res;
}

kj::Promise<Result<S3Helper::FileTransferResult>>
S3Helper::getObject(const std::string & bucketName, const std::string & key)
try {
    debug("fetching 's3://%s/%s'...", bucketName, key);

    auto request =
        Aws::S3::Model::GetObjectRequest()
        .WithBucket(bucketName)
        .WithKey(key);

    request.SetResponseStreamFactory([&]() {
        return Aws::New<std::stringstream>("STRINGSTREAM");
    });

    FileTransferResult res;

    auto now1 = std::chrono::steady_clock::now();

    try {

        auto pfp = kj::newPromiseAndCrossThreadFulfiller<Aws::S3::Model::GetObjectOutcome>();
        client->GetObjectAsync(
            request,
            [&](const Aws::S3::S3Client *,
                const Aws::S3::Model::GetObjectRequest &,
                Aws::S3::Model::GetObjectOutcome res,
                const std::shared_ptr<const Aws::Client::AsyncCallerContext> &) {
                pfp.fulfiller->fulfill(std::move(res));
            }
        );
        auto result = checkAws(fmt("AWS error fetching '%s'", key), co_await pfp.promise);

        res.data = decompress(result.GetContentEncoding(),
            dynamic_cast<std::stringstream &>(result.GetBody()).str());

    } catch (S3Error & e) {
        if ((e.err != Aws::S3::S3Errors::NO_SUCH_KEY) &&
            (e.err != Aws::S3::S3Errors::ACCESS_DENIED)) throw;
    }

    auto now2 = std::chrono::steady_clock::now();

    res.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();

    co_return res;
} catch (...) {
    co_return result::current_exception();
}

struct S3BinaryCacheStoreConfig final : BinaryCacheStoreConfig
{
    using BinaryCacheStoreConfig::BinaryCacheStoreConfig;

    const Setting<std::string> profile{this, "", "profile",
        R"(
          The name of the AWS configuration profile to use. By default
          Lix will use the `default` profile.
        )"};

    const Setting<std::string> region{this, Aws::Region::US_EAST_1, "region",
        R"(
          The region of the S3 bucket. If your bucket is not in
          `us–east-1`, you should always explicitly specify the region
          parameter.
        )"};

    const Setting<std::string> scheme{this, "", "scheme",
        R"(
          The scheme used for S3 requests, `https` (default) or `http`. This
          option allows you to disable HTTPS for binary caches which don't
          support it.

          > **Note**
          >
          > HTTPS should be used if the cache might contain sensitive
          > information.
        )"};

    const Setting<std::string> endpoint{this, "", "endpoint",
        R"(
          The URL of the endpoint of an S3-compatible service such as MinIO.
          Do not specify this setting if you're using Amazon S3.

          > **Note**
          >
          > This endpoint must support HTTPS and will use path-based
          > addressing instead of virtual host based addressing.
        )"};

    const Setting<std::string> narinfoCompression{this, "", "narinfo-compression",
        "Compression method for `.narinfo` files."};

    const Setting<std::string> lsCompression{this, "", "ls-compression",
        "Compression method for `.ls` files."};

    const Setting<std::string> logCompression{this, "", "log-compression",
        R"(
          Compression method for `log/*` files. It is recommended to
          use a compression method supported by most web browsers
          (e.g. `brotli`).
        )"};

    const Setting<bool> multipartUpload{
        this, false, "multipart-upload",
        "Whether to use multi-part uploads."};

    const Setting<uint64_t> bufferSize{
        this, 5 * 1024 * 1024, "buffer-size",
        "Size (in bytes) of each part in multi-part uploads."};

    const std::string name() override { return "S3 Binary Cache Store"; }

    std::string doc() override
    {
        return
          #include "s3-binary-cache-store.md"
          ;
    }
};

struct S3BinaryCacheStoreImpl : public S3BinaryCacheStore
{
    S3BinaryCacheStoreConfig config_;

    S3BinaryCacheStoreConfig & config() override { return config_; }
    const S3BinaryCacheStoreConfig & config() const override { return config_; }

    std::string bucketName;

    Stats stats;

    S3Helper s3Helper;

    S3BinaryCacheStoreImpl(
        const std::string & uriScheme,
        const std::string & bucketName,
        S3BinaryCacheStoreConfig config)
        : Store(config)
        , BinaryCacheStore(config)
        , S3BinaryCacheStore(config)
        , config_(std::move(config))
        , bucketName(bucketName)
        , s3Helper(config_.profile, config_.region, config_.scheme, config_.endpoint)
    {
        diskCache = getNarInfoDiskCache();
    }

    std::string getUri() override
    {
        return "s3://" + bucketName;
    }

    kj::Promise<Result<void>> init() override
    try {
        if (auto cacheInfo = diskCache->upToDateCacheExists(getUri())) {
            config().wantMassQuery.setDefault(cacheInfo->wantMassQuery);
            config().priority.setDefault(cacheInfo->priority);
        } else {
            TRY_AWAIT(BinaryCacheStore::init());
            diskCache->createCache(
                getUri(), config().storeDir, config().wantMassQuery, config().priority
            );
        }
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    const Stats & getS3Stats() override
    {
        return stats;
    }

    /* This is a specialisation of isValidPath() that optimistically
       fetches the .narinfo file, rather than first checking for its
       existence via a HEAD request. Since .narinfos are small, doing
       a GET is unlikely to be slower than HEAD. */
    kj::Promise<Result<bool>>
    isValidPathUncached(const StorePath & storePath, const Activity * context) override
    try {
        TRY_AWAIT(queryPathInfo(storePath, context));
        co_return true;
    } catch (InvalidPath & e) {
        co_return false;
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<bool>>
    fileExists(const std::string & path, const Activity * context) override
    try {
        stats.head++;

        auto pfp = kj::newPromiseAndCrossThreadFulfiller<Aws::S3::Model::HeadObjectOutcome>();
        s3Helper.client->HeadObjectAsync(
            Aws::S3::Model::HeadObjectRequest().WithBucket(bucketName).WithKey(path),
            [&](const Aws::S3::S3Client *,
                const Aws::S3::Model::HeadObjectRequest &,
                const Aws::S3::Model::HeadObjectOutcome & res,
                const std::shared_ptr<const Aws::Client::AsyncCallerContext> &) {
                pfp.fulfiller->fulfill(auto(res));
            }
        );
        auto res = co_await pfp.promise;

        if (!res.IsSuccess()) {
            auto & error = res.GetError();
            if (error.GetErrorType() == Aws::S3::S3Errors::RESOURCE_NOT_FOUND
                || error.GetErrorType() == Aws::S3::S3Errors::NO_SUCH_KEY
                // If bucket listing is disabled, 404s turn into 403s
                || error.GetErrorType() == Aws::S3::S3Errors::ACCESS_DENIED)
                co_return false;
            throw Error("AWS error fetching '%s': %s", path, error.GetMessage());
        }

        co_return true;
    } catch (...) {
        co_return result::current_exception();
    }

    std::shared_ptr<TransferManager> transferManager;
    std::once_flag transferManagerCreated;

    kj::Promise<Result<void>> uploadFile(
        const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType,
        const std::string & contentEncoding
    )
    try {
        istream->seekg(0, istream->end);
        auto size = istream->tellg();
        istream->seekg(0, istream->beg);

        auto maxThreads = std::thread::hardware_concurrency();

        static std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor>
            executor = std::make_shared<Aws::Utils::Threading::PooledThreadExecutor>(maxThreads);

        struct TransferContext : Aws::Client::AsyncCallerContext
        {
            kj::CrossThreadPromiseFulfiller<void> * signal;
            explicit TransferContext(kj::CrossThreadPromiseFulfiller<void> * signal)
                : signal(signal)
            {
            }
        };

        std::call_once(transferManagerCreated, [&]()
        {
            if (config().multipartUpload) {
                TransferManagerConfiguration transferConfig(executor.get());

                transferConfig.s3Client = s3Helper.client;
                transferConfig.bufferSize = config().bufferSize;

                transferConfig.uploadProgressCallback =
                    [](const TransferManager * transferManager,
                       const std::shared_ptr<const TransferHandle> & transferHandle) {
                        // FIXME: find a way to properly abort the multipart upload.
                        debug("upload progress ('%s'): '%d' of '%d' bytes",
                            transferHandle->GetKey(),
                            transferHandle->GetBytesTransferred(),
                            transferHandle->GetBytesTotalSize());

                        if (transferHandle->GetStatus() != TransferStatus::NOT_STARTED
                            && transferHandle->GetStatus() != TransferStatus::IN_PROGRESS)
                        {
                            auto context = std::static_pointer_cast<const TransferContext>(
                                transferHandle->GetContext()
                            );
                            if (context->signal) {
                                context->signal->fulfill();
                            }
                        }
                    };

                transferManager = TransferManager::Create(transferConfig);
            }
        });

        auto now1 = std::chrono::steady_clock::now();

        if (transferManager) {

            if (contentEncoding != "")
                throw Error("setting a content encoding is not supported with S3 multi-part uploads");

            auto pfp = kj::newPromiseAndCrossThreadFulfiller<void>();
            std::shared_ptr<TransferHandle> transferHandle = transferManager->UploadFile(
                istream,
                bucketName,
                path,
                mimeType,
                Aws::Map<Aws::String, Aws::String>(),
                std::make_shared<TransferContext>(pfp.fulfiller.get()) /*, contentEncoding */
            );

            co_await pfp.promise;
            transferHandle->WaitUntilFinished();

            if (transferHandle->GetStatus() == TransferStatus::FAILED)
                throw Error("AWS error: failed to upload 's3://%s/%s': %s",
                    bucketName, path, transferHandle->GetLastError().GetMessage());

            if (transferHandle->GetStatus() != TransferStatus::COMPLETED)
                throw Error("AWS error: transfer status of 's3://%s/%s' in unexpected state",
                    bucketName, path);

        } else {

            auto request =
                Aws::S3::Model::PutObjectRequest()
                .WithBucket(bucketName)
                .WithKey(path);

            request.SetContentType(mimeType);

            if (contentEncoding != "")
                request.SetContentEncoding(contentEncoding);

            request.SetBody(istream);

            auto pfp = kj::newPromiseAndCrossThreadFulfiller<Aws::S3::Model::PutObjectOutcome>();
            s3Helper.client->PutObjectAsync(
                request,
                [&](const Aws::S3::S3Client *,
                    const Aws::S3::Model::PutObjectRequest &,
                    const Aws::S3::Model::PutObjectOutcome & res,
                    const std::shared_ptr<const Aws::Client::AsyncCallerContext> &) {
                    pfp.fulfiller->fulfill(auto(res));
                }
            );
            auto result = checkAws(fmt("AWS error uploading '%s'", path), co_await pfp.promise);
        }

        auto now2 = std::chrono::steady_clock::now();

        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1)
                .count();

        printInfo("uploaded 's3://%s/%s' (%d bytes) in %d ms",
            bucketName, path, size, duration);

        stats.putTimeMs += duration;
        stats.putBytes += std::max(size, (decltype(size)) 0);
        stats.put++;
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<void>> upsertFile(
        const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType,
        const Activity * context
    ) override
    try {
        auto compress = [&](std::string compression)
        {
            auto compressed = nix::compress(compression, StreamToSourceAdapter(istream).drain());
            return std::make_shared<std::stringstream>(std::move(compressed));
        };

        if (config().narinfoCompression != "" && path.ends_with(".narinfo")) {
            TRY_AWAIT(uploadFile(
                path, compress(config().narinfoCompression), mimeType, config().narinfoCompression
            ));
        } else if (config().lsCompression != "" && path.ends_with(".ls")) {
            TRY_AWAIT(
                uploadFile(path, compress(config().lsCompression), mimeType, config().lsCompression)
            );
        } else if (config().logCompression != "" && path.starts_with("log/"))
            TRY_AWAIT(uploadFile(
                path, compress(config().logCompression), mimeType, config().logCompression
            ));
        else
            TRY_AWAIT(uploadFile(path, istream, mimeType, ""));
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<box_ptr<AsyncInputStream>>>
    getFile(const std::string & path, const Activity * context) override
    try {
        stats.get++;

        // FIXME: stream output to sink.
        auto res = TRY_AWAIT(s3Helper.getObject(bucketName, path));

        stats.getBytes += res.data ? res.data->size() : 0;
        stats.getTimeMs += res.durationMs;

        if (res.data) {
            printTalkative("downloaded 's3://%s/%s' (%d bytes) in %d ms",
                bucketName, path, res.data->size(), res.durationMs);

            co_return make_box_ptr<AsyncGeneratorInputStream>(
                [](std::string data) -> Generator<Bytes> {
                    co_yield std::span{data.data(), data.size()};
                }(std::move(*res.data))
            );
        } else
            throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache '%s'", path, getUri());
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<StorePathSet>> queryAllValidPaths() override
    try {
        StorePathSet paths;
        std::string marker;

        do {
            debug("listing bucket 's3://%s' from key '%s'...", bucketName, marker);

            auto pfp = kj::newPromiseAndCrossThreadFulfiller<Aws::S3::Model::ListObjectsOutcome>();
            s3Helper.client->ListObjectsAsync(
                Aws::S3::Model::ListObjectsRequest()
                    .WithBucket(bucketName)
                    .WithDelimiter("/")
                    .WithMarker(marker),
                [&](const Aws::S3::S3Client *,
                    const Aws::S3::Model::ListObjectsRequest &,
                    const Aws::S3::Model::ListObjectsOutcome & res,
                    const std::shared_ptr<const Aws::Client::AsyncCallerContext> &) {
                    pfp.fulfiller->fulfill(auto(res));
                }
            );
            auto res =
                checkAws(fmt("AWS error listing bucket '%s'", bucketName), co_await pfp.promise);

            auto & contents = res.GetContents();

            debug("got %d keys, next marker '%s'",
                contents.size(), res.GetNextMarker());

            for (auto object : contents) {
                auto & key = object.GetKey();
                if (key.size() != 40 || !key.ends_with(".narinfo")) continue;
                paths.insert(parseStorePath(
                    config().storeDir + "/" + key.substr(0, key.size() - 8) + "-" + MissingName
                ));
            }

            marker = res.GetNextMarker();
        } while (!marker.empty());

        co_return paths;
    } catch (...) {
        co_return result::current_exception();
    }

    /**
     * For now, we conservatively say we don't know.
     *
     * \todo try to expose our S3 authentication status.
     */
    kj::Promise<Result<std::optional<TrustedFlag>>> isTrustedClient() override
    {
        return {result::success(std::nullopt)};
    }

    static std::set<std::string> uriSchemes() { return {"s3"}; }

};

void registerS3BinaryCacheStore() {
    StoreImplementations::add<S3BinaryCacheStoreImpl, S3BinaryCacheStoreConfig>();
}

}

#else
namespace nix {
void registerS3BinaryCacheStore() {}
}
#endif
