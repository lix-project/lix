#include "filetransfer.hh"
#include "namespaces.hh"
#include "globals.hh"
#include "store-api.hh"
#include "s3.hh"
#include "signals.hh"
#include "compression.hh"
#include "strings.hh"
#include <cstddef>

#if ENABLE_S3
#include <aws/core/client/ClientConfiguration.h>
#endif

#include <unistd.h>
#include <fcntl.h>

#include <curl/curl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>
#include <random>
#include <thread>
#include <regex>

using namespace std::string_literals;

namespace nix {

FileTransferSettings fileTransferSettings;

static GlobalConfig::Register rFileTransferSettings(&fileTransferSettings);

struct curlFileTransfer : public FileTransfer
{
    CURLM * curlm = 0;

    std::random_device rd;
    std::mt19937 mt19937;
    const unsigned int baseRetryTimeMs;

    struct TransferItem : public std::enable_shared_from_this<TransferItem>
    {
        curlFileTransfer & fileTransfer;
        FileTransferRequest request;
        FileTransferResult result;
        Activity act;
        std::optional<std::string> uploadData;
        bool done = false; // whether either the success or failure function has been called
        std::packaged_task<FileTransferResult(std::exception_ptr, FileTransferResult)> callback;
        std::function<void(TransferItem &, std::string_view data)> dataCallback;
        CURL * req = 0;
        bool active = false; // whether the handle has been added to the multi object
        std::string statusMsg;

        unsigned int attempt = 0;
        const size_t tries = fileTransferSettings.tries;

        /* Don't start this download until the specified time point
           has been reached. */
        std::chrono::steady_clock::time_point embargo;

        struct curl_slist * requestHeaders = 0;

        std::string encoding;

        bool acceptRanges = false;

        curl_off_t writtenToSink = 0;

        inline static const std::set<long> successfulStatuses {200, 201, 204, 206, 304, 0 /* other protocol */};
        /* Get the HTTP status code, or 0 for other protocols. */
        long getHTTPStatus()
        {
            long httpStatus = 0;
            long protocol = 0;
            curl_easy_getinfo(req, CURLINFO_PROTOCOL, &protocol);
            if (protocol == CURLPROTO_HTTP || protocol == CURLPROTO_HTTPS)
                curl_easy_getinfo(req, CURLINFO_RESPONSE_CODE, &httpStatus);
            return httpStatus;
        }

        std::string verb() const
        {
            return uploadData ? "upload" : "download";
        }

        TransferItem(curlFileTransfer & fileTransfer,
            const FileTransferRequest & request,
            ActivityId parentAct,
            std::invocable<std::exception_ptr> auto callback,
            std::function<void(TransferItem &, std::string_view data)> dataCallback,
            std::optional<std::string> uploadData
        )
            : fileTransfer(fileTransfer)
            , request(request)
            , act(*logger, lvlTalkative, actFileTransfer,
                fmt(uploadData ? "uploading '%s'" : "downloading '%s'", request.uri),
                {request.uri}, parentAct)
            , uploadData(std::move(uploadData))
            , callback([cb{std::move(callback)}] (std::exception_ptr ex, FileTransferResult r) {
                cb(ex);
                return r;
            })
            , dataCallback(std::move(dataCallback))
        {
            requestHeaders = curl_slist_append(requestHeaders, "Accept-Encoding: zstd, br, gzip, deflate, bzip2, xz");
            if (!request.expectedETag.empty())
                requestHeaders = curl_slist_append(requestHeaders, ("If-None-Match: " + request.expectedETag).c_str());
            for (auto it = request.headers.begin(); it != request.headers.end(); ++it){
                requestHeaders = curl_slist_append(requestHeaders, fmt("%s: %s", it->first, it->second).c_str());
            }
        }

        ~TransferItem()
        {
            if (req) {
                if (active)
                    curl_multi_remove_handle(fileTransfer.curlm, req);
                curl_easy_cleanup(req);
            }
            if (requestHeaders) curl_slist_free_all(requestHeaders);
            try {
                if (!done)
                    fail(FileTransferError(Interrupted, {}, "download of '%s' was interrupted", request.uri));
            } catch (...) {
                ignoreExceptionInDestructor();
            }
        }

        void failEx(std::exception_ptr ex)
        {
            assert(!done);
            done = true;
            callback(ex, std::move(result));
        }

        template<class T>
        void fail(T && e)
        {
            failEx(std::make_exception_ptr(std::forward<T>(e)));
        }

        std::exception_ptr writeException;

        size_t writeCallback(void * contents, size_t size, size_t nmemb)
        {
            const size_t realSize = size * nmemb;

            try {
                result.bodySize += realSize;

                if (successfulStatuses.count(getHTTPStatus()) && this->dataCallback) {
                    writtenToSink += realSize;
                    dataCallback(*this, {static_cast<const char *>(contents), realSize});
                } else {
                    this->result.data.append(static_cast<const char *>(contents), realSize);
                }

                return realSize;
            } catch (...) {
                writeException = std::current_exception();
                return CURL_WRITEFUNC_ERROR;
            }
        }

        static size_t writeCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp)
        {
            return static_cast<TransferItem *>(userp)->writeCallback(contents, size, nmemb);
        }

        size_t headerCallback(void * contents, size_t size, size_t nmemb)
        {
            size_t realSize = size * nmemb;
            std::string line(static_cast<char *>(contents), realSize);
            printMsg(lvlVomit, "got header for '%s': %s", request.uri, trim(line));

            static std::regex statusLine("HTTP/[^ ]+ +[0-9]+(.*)", std::regex::extended | std::regex::icase);
            if (std::smatch match; std::regex_match(line, match, statusLine)) {
                result.etag = "";
                result.data.clear();
                result.bodySize = 0;
                statusMsg = trim(match.str(1));
                acceptRanges = false;
                encoding = "";
            } else {
                auto i = line.find(':');
                if (i != std::string::npos) {
                    std::string name = toLower(trim(line.substr(0, i)));

                    if (name == "etag") {
                        result.etag = trim(line.substr(i + 1));
                    }

                    else if (name == "content-encoding")
                        encoding = trim(line.substr(i + 1));

                    else if (name == "accept-ranges" && toLower(trim(line.substr(i + 1))) == "bytes")
                        acceptRanges = true;

                    else if (name == "link" || name == "x-amz-meta-link") {
                        auto value = trim(line.substr(i + 1));
                        static std::regex linkRegex("<([^>]*)>; rel=\"immutable\"", std::regex::extended | std::regex::icase);
                        if (std::smatch match; std::regex_match(value, match, linkRegex))
                            result.immutableUrl = match.str(1);
                        else
                            debug("got invalid link header '%s'", value);
                    }
                }
            }
            return realSize;
        }

        static size_t headerCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp)
        {
            return static_cast<TransferItem *>(userp)->headerCallback(contents, size, nmemb);
        }

        int progressCallback(double dltotal, double dlnow)
        {
            try {
              act.progress(dlnow, dltotal);
            } catch (nix::Interrupted &) {
              assert(_isInterrupted);
            }
            return _isInterrupted;
        }

        static int progressCallbackWrapper(void * userp, double dltotal, double dlnow, double ultotal, double ulnow)
        {
            return static_cast<TransferItem *>(userp)->progressCallback(dltotal, dlnow);
        }

        static int debugCallback(CURL * handle, curl_infotype type, char * data, size_t size, void * userptr)
        {
            if (type == CURLINFO_TEXT)
                vomit("curl: %s", chomp(std::string(data, size)));
            return 0;
        }

        size_t readOffset = 0;
        size_t readCallback(char *buffer, size_t size, size_t nitems)
        {
            if (readOffset == uploadData->length())
                return 0;
            auto count = std::min(size * nitems, uploadData->length() - readOffset);
            assert(count);
            // Lint: this is turning a string into a byte array to hand to
            // curl, which is fine.
            // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
            memcpy(buffer, uploadData->data() + readOffset, count);
            readOffset += count;
            return count;
        }

        static size_t readCallbackWrapper(char *buffer, size_t size, size_t nitems, void * userp)
        {
            return static_cast<TransferItem *>(userp)->readCallback(buffer, size, nitems);
        }

        void init()
        {
            if (!req) req = curl_easy_init();

            curl_easy_reset(req);

            if (verbosity >= lvlVomit) {
                curl_easy_setopt(req, CURLOPT_VERBOSE, 1);
                curl_easy_setopt(req, CURLOPT_DEBUGFUNCTION, TransferItem::debugCallback);
            }

            curl_easy_setopt(req, CURLOPT_URL, request.uri.c_str());
            curl_easy_setopt(req, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(req, CURLOPT_MAXREDIRS, 10);
            curl_easy_setopt(req, CURLOPT_NOSIGNAL, 1);
            curl_easy_setopt(req, CURLOPT_USERAGENT,
                ("curl/" LIBCURL_VERSION " Lix/" + nixVersion +
                    (fileTransferSettings.userAgentSuffix != "" ? " " + fileTransferSettings.userAgentSuffix.get() : "")).c_str());
            curl_easy_setopt(req, CURLOPT_PIPEWAIT, 1);
            if (fileTransferSettings.enableHttp2)
                curl_easy_setopt(req, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
            else
                curl_easy_setopt(req, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
            curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, TransferItem::writeCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_WRITEDATA, this);
            curl_easy_setopt(req, CURLOPT_HEADERFUNCTION, TransferItem::headerCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_HEADERDATA, this);

            curl_easy_setopt(req, CURLOPT_PROGRESSFUNCTION, progressCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_PROGRESSDATA, this);
            curl_easy_setopt(req, CURLOPT_NOPROGRESS, 0);

            curl_easy_setopt(req, CURLOPT_PROTOCOLS_STR, "http,https,ftp,ftps,file");

            curl_easy_setopt(req, CURLOPT_HTTPHEADER, requestHeaders);

            if (settings.downloadSpeed.get() > 0)
                curl_easy_setopt(req, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t) (settings.downloadSpeed.get() * 1024));

            if (request.head)
                curl_easy_setopt(req, CURLOPT_NOBODY, 1);

            if (uploadData) {
                curl_easy_setopt(req, CURLOPT_UPLOAD, 1L);
                curl_easy_setopt(req, CURLOPT_READFUNCTION, readCallbackWrapper);
                curl_easy_setopt(req, CURLOPT_READDATA, this);
                curl_easy_setopt(req, CURLOPT_INFILESIZE_LARGE, (curl_off_t) uploadData->length());
            }

            if (request.verifyTLS) {
                if (settings.caFile != "")
                    curl_easy_setopt(req, CURLOPT_CAINFO, settings.caFile.get().c_str());
            } else {
                curl_easy_setopt(req, CURLOPT_SSL_VERIFYPEER, 0);
                curl_easy_setopt(req, CURLOPT_SSL_VERIFYHOST, 0);
            }

            curl_easy_setopt(req, CURLOPT_CONNECTTIMEOUT, fileTransferSettings.connectTimeout.get());

            curl_easy_setopt(req, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(req, CURLOPT_LOW_SPEED_TIME, fileTransferSettings.stalledDownloadTimeout.get());

            /* If no file exist in the specified path, curl continues to work
               anyway as if netrc support was disabled. */
            curl_easy_setopt(req, CURLOPT_NETRC_FILE, settings.netrcFile.get().c_str());
            curl_easy_setopt(req, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);

            if (writtenToSink)
                curl_easy_setopt(req, CURLOPT_RESUME_FROM_LARGE, writtenToSink);

            result.data.clear();
            result.bodySize = 0;
        }

        void finish(CURLcode code)
        {
            auto httpStatus = getHTTPStatus();

            char * effectiveUriCStr = nullptr;
            curl_easy_getinfo(req, CURLINFO_EFFECTIVE_URL, &effectiveUriCStr);
            if (effectiveUriCStr)
                result.effectiveUri = effectiveUriCStr;

            debug("finished %s of '%s'; curl status = %d, HTTP status = %d, body = %d bytes",
                verb(), request.uri, code, httpStatus, result.bodySize);

            // this has to happen here until we can return an actual future.
            // wrapping user `callback`s instead is not possible because the
            // Callback api expects std::functions, and copying Callbacks is
            // not possible due the promises they hold.
            if (code == CURLE_OK && !dataCallback && result.data.length() > 0) {
                result.data = decompress(encoding, result.data);
            }

            if (writeException)
                failEx(writeException);

            else if (code == CURLE_OK && successfulStatuses.count(httpStatus))
            {
                result.cached = httpStatus == 304;
                act.progress(result.bodySize, result.bodySize);
                done = true;
                callback(nullptr, std::move(result));
            }

            else {
                // We treat most errors as transient, but won't retry when hopeless
                Error err = Transient;

                if (httpStatus == 404 || httpStatus == 410 || code == CURLE_FILE_COULDNT_READ_FILE) {
                    // The file is definitely not there
                    err = NotFound;
                } else if (httpStatus == 401 || httpStatus == 403 || httpStatus == 407) {
                    // Don't retry on authentication/authorization failures
                    err = Forbidden;
                } else if (httpStatus >= 400 && httpStatus < 500 && httpStatus != 408 && httpStatus != 429) {
                    // Most 4xx errors are client errors and are probably not worth retrying:
                    //   * 408 means the server timed out waiting for us, so we try again
                    //   * 429 means too many requests, so we retry (with a delay)
                    err = Misc;
                } else if (httpStatus == 501 || httpStatus == 505 || httpStatus == 511) {
                    // Let's treat most 5xx (server) errors as transient, except for a handful:
                    //   * 501 not implemented
                    //   * 505 http version not supported
                    //   * 511 we're behind a captive portal
                    err = Misc;
                } else {
                    // Don't bother retrying on certain cURL errors either

                    // Allow selecting a subset of enum values
                    #pragma GCC diagnostic push
                    #pragma GCC diagnostic ignored "-Wswitch-enum"
                    switch (code) {
                        case CURLE_FAILED_INIT:
                        case CURLE_URL_MALFORMAT:
                        case CURLE_NOT_BUILT_IN:
                        case CURLE_REMOTE_ACCESS_DENIED:
                        case CURLE_FILE_COULDNT_READ_FILE:
                        case CURLE_FUNCTION_NOT_FOUND:
                        case CURLE_ABORTED_BY_CALLBACK:
                        case CURLE_BAD_FUNCTION_ARGUMENT:
                        case CURLE_INTERFACE_FAILED:
                        case CURLE_UNKNOWN_OPTION:
                        case CURLE_SSL_CACERT_BADFILE:
                        case CURLE_TOO_MANY_REDIRECTS:
                        case CURLE_WRITE_ERROR:
                        case CURLE_UNSUPPORTED_PROTOCOL:
                            err = Misc;
                            break;
                        default: // Shut up warnings
                            break;
                    }
                    #pragma GCC diagnostic pop
                }

                attempt++;

                std::optional<std::string> response;
                if (!successfulStatuses.count(httpStatus))
                    response = std::move(result.data);
                auto exc =
                    code == CURLE_ABORTED_BY_CALLBACK && _isInterrupted
                    ? FileTransferError(Interrupted, std::move(response), "%s of '%s' was interrupted", verb(), request.uri)
                    : httpStatus != 0
                    ? FileTransferError(err,
                        std::move(response),
                        "unable to %s '%s': HTTP error %d (%s)%s",
                        verb(), request.uri, httpStatus, statusMsg,
                        code == CURLE_OK ? "" : fmt(" (curl error: %s)", curl_easy_strerror(code)))
                    : FileTransferError(err,
                        std::move(response),
                        "unable to %s '%s': %s (%d)",
                        verb(), request.uri, curl_easy_strerror(code), code);

                /* If this is a transient error, then maybe retry the
                   download after a while. If we're writing to a
                   sink, we can only retry if the server supports
                   ranged requests. */
                if (err == Transient
                    && attempt < tries
                    && (!this->dataCallback
                        || writtenToSink == 0
                        || (acceptRanges && encoding.empty())))
                {
                    int ms = fileTransfer.baseRetryTimeMs * std::pow(2.0f, attempt - 1 + std::uniform_real_distribution<>(0.0, 0.5)(fileTransfer.mt19937));
                    if (writtenToSink)
                        warn("%s; retrying from offset %d in %d ms", exc.what(), writtenToSink, ms);
                    else
                        warn("%s; retrying in %d ms", exc.what(), ms);
                    embargo = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
                    fileTransfer.enqueueItem(shared_from_this());
                }
                else
                    fail(std::move(exc));
            }
        }
    };

    struct State
    {
        struct EmbargoComparator {
            bool operator() (const std::shared_ptr<TransferItem> & i1, const std::shared_ptr<TransferItem> & i2) {
                return i1->embargo > i2->embargo;
            }
        };
        bool quit = false;
        std::priority_queue<std::shared_ptr<TransferItem>, std::vector<std::shared_ptr<TransferItem>>, EmbargoComparator> incoming;
    };

    Sync<State> state_;

    std::thread workerThread;

    curlFileTransfer(unsigned int baseRetryTimeMs)
        : mt19937(rd())
        , baseRetryTimeMs(baseRetryTimeMs)
    {
        static std::once_flag globalInit;
        std::call_once(globalInit, curl_global_init, CURL_GLOBAL_ALL);

        curlm = curl_multi_init();

        curl_multi_setopt(curlm, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
        curl_multi_setopt(curlm, CURLMOPT_MAX_TOTAL_CONNECTIONS,
            fileTransferSettings.httpConnections.get());

        workerThread = std::thread([&]() { workerThreadEntry(); });
    }

    ~curlFileTransfer()
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

        if (curlm) curl_multi_cleanup(curlm);
    }

    void wakeup()
    {
        if (auto mc = curl_multi_wakeup(curlm))
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

        bool quit = false;

        std::chrono::steady_clock::time_point nextWakeup;

        while (!quit) {
            checkInterrupt();

            /* Let curl do its thing. */
            int running;
            CURLMcode mc = curl_multi_perform(curlm, &running);
            if (mc != CURLM_OK)
                throw nix::Error("unexpected error from curl_multi_perform(): %s", curl_multi_strerror(mc));

            /* Set the promises of any finished requests. */
            CURLMsg * msg;
            int left;
            while ((msg = curl_multi_info_read(curlm, &left))) {
                if (msg->msg == CURLMSG_DONE) {
                    auto i = items.find(msg->easy_handle);
                    assert(i != items.end());
                    i->second->finish(msg->data.result);
                    curl_multi_remove_handle(curlm, i->second->req);
                    i->second->active = false;
                    items.erase(i);
                }
            }

            /* Wait for activity, including wakeup events. */
            long maxSleepTimeMs = items.empty() ? 10000 : 100;
            auto sleepTimeMs =
                nextWakeup != std::chrono::steady_clock::time_point()
                ? std::max(0, (int) std::chrono::duration_cast<std::chrono::milliseconds>(nextWakeup - std::chrono::steady_clock::now()).count())
                : maxSleepTimeMs;
            vomit("download thread waiting for %d ms", sleepTimeMs);
            mc = curl_multi_poll(curlm, nullptr, 0, sleepTimeMs, nullptr);
            if (mc != CURLM_OK)
                throw nix::Error("unexpected error from curl_multi_poll(): %s", curl_multi_strerror(mc));

            nextWakeup = std::chrono::steady_clock::time_point();

            /* Add new curl requests from the incoming requests queue,
               except for requests that are embargoed (waiting for a
               retry timeout to expire). */

            std::vector<std::shared_ptr<TransferItem>> incoming;
            auto now = std::chrono::steady_clock::now();

            {
                auto state(state_.lock());
                while (!state->incoming.empty()) {
                    auto item = state->incoming.top();
                    if (item->embargo <= now) {
                        incoming.push_back(item);
                        state->incoming.pop();
                    } else {
                        if (nextWakeup == std::chrono::steady_clock::time_point()
                            || item->embargo < nextWakeup)
                            nextWakeup = item->embargo;
                        break;
                    }
                }
                quit = state->quit;
            }

            for (auto & item : incoming) {
                debug("starting %s of %s", item->verb(), item->request.uri);
                item->init();
                curl_multi_add_handle(curlm, item->req);
                item->active = true;
                items[item->req] = item;
            }
        }

        debug("download thread shutting down");
    }

    void workerThreadEntry()
    {
        try {
            workerThreadMain();
        } catch (nix::Interrupted & e) {
        } catch (std::exception & e) {
            printError("unexpected error in download thread: %s", e.what());
        }

        {
            auto state(state_.lock());
            while (!state->incoming.empty()) state->incoming.pop();
            state->quit = true;
        }
    }

    std::shared_ptr<TransferItem> enqueueItem(std::shared_ptr<TransferItem> item)
    {
        if (item->uploadData
            && !item->request.uri.starts_with("http://")
            && !item->request.uri.starts_with("https://"))
            throw nix::Error("uploading to '%s' is not supported", item->request.uri);

        {
            auto state(state_.lock());
            if (state->quit)
                throw nix::Error("cannot enqueue download request because the download thread is shutting down");
            state->incoming.push(item);
        }
        wakeup();
        return item;
    }

#if ENABLE_S3
    static std::tuple<std::string, std::string, Store::Params> parseS3Uri(std::string uri)
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

    std::future<FileTransferResult> enqueueDownload(const FileTransferRequest & request) override
    {
        return enqueueFileTransfer(request, std::nullopt);
    }

    std::future<FileTransferResult>
    enqueueUpload(const FileTransferRequest & request, std::string data) override
    {
        return enqueueFileTransfer(request, std::move(data));
    }

    std::future<FileTransferResult>
    enqueueFileTransfer(const FileTransferRequest & request, std::optional<std::string> data)
    {
        return enqueueFileTransfer(
            request,
            [](std::exception_ptr ex) {
                if (ex) {
                    std::rethrow_exception(ex);
                }
            },
            {},
            std::move(data)
        );
    }

    std::future<FileTransferResult> enqueueFileTransfer(const FileTransferRequest & request,
        std::invocable<std::exception_ptr> auto callback,
        std::function<void(TransferItem &, std::string_view data)> dataCallback,
        std::optional<std::string> data
    )
    {
        /* Ugly hack to support s3:// URIs. */
        if (request.uri.starts_with("s3://")) {
            // FIXME: do this on a worker thread
            return std::async(std::launch::deferred, [uri{request.uri}]() -> FileTransferResult {
#if ENABLE_S3
                auto [bucketName, key, params] = parseS3Uri(uri);

                std::string profile = getOr(params, "profile", "");
                std::string region = getOr(params, "region", Aws::Region::US_EAST_1);
                std::string scheme = getOr(params, "scheme", "");
                std::string endpoint = getOr(params, "endpoint", "");

                S3Helper s3Helper(profile, region, scheme, endpoint);

                // FIXME: implement ETag
                auto s3Res = s3Helper.getObject(bucketName, key);
                FileTransferResult res;
                if (!s3Res.data)
                    throw FileTransferError(NotFound, "S3 object '%s' does not exist", uri);
                res.data = std::move(*s3Res.data);
                return res;
#else
                throw nix::Error("cannot download '%s' because Lix is not built with S3 support", uri);
#endif
            });
        }

        return enqueueItem(std::make_shared<TransferItem>(
                               *this,
                               request,
                               getCurActivity(),
                               std::move(callback),
                               std::move(dataCallback),
                               std::move(data)
                           ))
            ->callback.get_future();
    }

    box_ptr<Source> download(FileTransferRequest && request) override
    {
        struct State {
            bool done = false, failed = false;
            std::exception_ptr exc;
            std::string data, encoding;
            std::condition_variable avail, request;
        };

        auto _state = std::make_shared<Sync<State>>();

        enqueueFileTransfer(
            request,
            [_state](std::exception_ptr ex) {
                auto state(_state->lock());
                state->done = true;
                state->exc = ex;
                state->avail.notify_one();
                state->request.notify_one();
            },
            [_state](TransferItem & transfer, std::string_view data) {
                auto state(_state->lock());

                if (state->failed) {
                    // actual exception doesn't matter, the other end is already dead
                    throw std::exception{};
                }

                /* If the buffer is full, then go to sleep until the calling
                thread wakes us up (i.e. when it has removed data from the
                buffer). We don't wait forever to prevent stalling the
                download thread. (Hopefully sleeping will throttle the
                sender.) */
                if (state->data.size() > 1024 * 1024) {
                    debug("download buffer is full; going to sleep");
                    state.wait_for(state->request, std::chrono::seconds(10));
                }

                if (state->encoding.empty()) {
                    state->encoding = transfer.encoding;
                }

                /* Append data to the buffer and wake up the calling
                thread. */
                state->data.append(data);
                state->avail.notify_one();
            },
            std::nullopt
        );

        struct InnerSource : Source
        {
            const std::shared_ptr<Sync<State>> _state;
            std::string chunk;
            std::string_view buffered;

            explicit InnerSource(const std::shared_ptr<Sync<State>> & state) : _state(state) {}

            ~InnerSource()
            {
                // wake up the download thread if it's still going and have it abort
                auto state(_state->lock());
                state->failed |= !state->done;
                state->request.notify_one();
            }

            void awaitData(Sync<State>::Lock & state)
            {
                /* Grab data if available, otherwise wait for the download
                   thread to wake us up. */
                while (buffered.empty()) {
                    if (state->data.empty()) {
                        if (state->done) {
                            if (state->exc) {
                                std::rethrow_exception(state->exc);
                            }
                            return;
                        }

                        state.wait(state->avail);
                    }

                    chunk = std::move(state->data);
                    buffered = chunk;
                    state->request.notify_one();
                }
            }

            size_t read(char * data, size_t len) override
            {
                auto readPartial = [this](char * data, size_t len) -> size_t {
                    const auto available = std::min(len, buffered.size());
                    if (available == 0u) return 0u;

                    memcpy(data, buffered.data(), available);
                    buffered.remove_prefix(available);
                    return available;
                };
                size_t total = readPartial(data, len);

                while (total < len) {
                    {
                        auto state(_state->lock());
                        awaitData(state);
                    }
                    const auto current = readPartial(data + total, len - total);
                    total += current;
                    if (total == 0 || current == 0) {
                        break;
                    }
                }

                if (total == 0) {
                    throw EndOfFile("download finished");
                }

                return total;
            }
        };

        struct DownloadSource : Source
        {
            InnerSource inner;
            std::unique_ptr<Source> decompressor;

            explicit DownloadSource(const std::shared_ptr<Sync<State>> & state) : inner(state) {}

            size_t read(char * data, size_t len) override
            {
                checkInterrupt();

                if (!decompressor) {
                    auto state(inner._state->lock());
                    inner.awaitData(state);
                    decompressor = makeDecompressionSource(state->encoding, inner);
                }

                return decompressor->read(data, len);
            }
        };

        auto source = make_box_ptr<DownloadSource>(_state);
        auto lock(_state->lock());
        source->inner.awaitData(lock);
        return source;
    }
};

ref<curlFileTransfer> makeCurlFileTransfer(std::optional<unsigned int> baseRetryTimeMs)
{
    return make_ref<curlFileTransfer>(baseRetryTimeMs.value_or(250));
}

ref<FileTransfer> getFileTransfer()
{
    static ref<curlFileTransfer> fileTransfer = makeCurlFileTransfer({});

    if (fileTransfer->state_.lock()->quit)
        fileTransfer = makeCurlFileTransfer({});

    return fileTransfer;
}

ref<FileTransfer> makeFileTransfer(std::optional<unsigned int> baseRetryTimeMs)
{
    return makeCurlFileTransfer(baseRetryTimeMs);
}

template<typename... Args>
FileTransferError::FileTransferError(FileTransfer::Error error, std::optional<std::string> response, const Args & ... args)
    : Error(args...), error(error), response(response)
{
    const auto hf = HintFmt(args...);
    // FIXME: Due to https://github.com/NixOS/nix/issues/3841 we don't know how
    // to print different messages for different verbosity levels. For now
    // we add some heuristics for detecting when we want to show the response.
    if (response && (response->size() < 1024 || response->find("<html>") != std::string::npos))
        err.msg = HintFmt("%1%\n\nresponse body:\n\n%2%", Uncolored(hf.str()), chomp(*response));
    else
        err.msg = hf;
}

}
