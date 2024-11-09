#include "filetransfer.hh"
#include "namespaces.hh"
#include "globals.hh"
#include "store-api.hh"
#include "s3.hh"
#include "signals.hh"
#include "strings.hh"
#include <cstddef>

#include <kj/encoding.h>

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
        std::string uri;
        FileTransferResult result;
        Activity act;
        std::optional<std::string> uploadData;
        std::string downloadData;
        bool noBody = false; // \equiv HTTP HEAD, don't download data
        enum {
            /// nothing has been transferred yet
            initialSetup,
            /// at least some metadata has already been transferred,
            /// but the transfer did not succeed and is now retrying
            retrySetup,
            /// data transfer in progress
            transferring,
            /// transfer complete, result or failure reported
            transferComplete,
        } phase = initialSetup;
        std::promise<FileTransferResult> metadataPromise;
        std::packaged_task<void(std::exception_ptr)> doneCallback;
        // return false from dataCallback to pause the transfer without consuming data
        std::function<bool(std::string_view data)> dataCallback;
        CURL * req; // must never be nullptr
        std::string statusMsg;

        unsigned int attempt = 0;
        const size_t tries = fileTransferSettings.tries;
        uint64_t bodySize = 0;

        /* Don't start this download until the specified time point
           has been reached. */
        std::chrono::steady_clock::time_point embargo;

        struct curl_slist * requestHeaders = 0;

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
            const std::string & uri,
            const Headers & headers,
            ActivityId parentAct,
            std::invocable<std::exception_ptr> auto doneCallback,
            std::function<bool(std::string_view data)> dataCallback,
            std::optional<std::string> uploadData,
            bool noBody
        )
            : fileTransfer(fileTransfer)
            , uri(uri)
            , act(*logger, lvlTalkative, actFileTransfer,
                fmt(uploadData ? "uploading '%s'" : "downloading '%s'", uri),
                {uri}, parentAct)
            , uploadData(std::move(uploadData))
            , noBody(noBody)
            , doneCallback([cb{std::move(doneCallback)}] (std::exception_ptr ex) {
                cb(ex);
            })
            , dataCallback(std::move(dataCallback))
            , req(curl_easy_init())
        {
            if (req == nullptr) {
                throw FileTransferError(Misc, {}, "could not allocate curl handle");
            }
            for (auto it = headers.begin(); it != headers.end(); ++it){
                requestHeaders = curl_slist_append(requestHeaders, fmt("%s: %s", it->first, it->second).c_str());
            }
        }

        ~TransferItem()
        {
            curl_multi_remove_handle(fileTransfer.curlm, req);
            curl_easy_cleanup(req);
            if (requestHeaders) curl_slist_free_all(requestHeaders);
            try {
                if (phase != transferComplete)
                    fail(FileTransferError(Interrupted, {}, "download of '%s' was interrupted", uri));
            } catch (...) {
                ignoreExceptionInDestructor();
            }
        }

        void failEx(std::exception_ptr ex)
        {
            assert(phase != transferComplete);
            if (phase == initialSetup) {
                metadataPromise.set_exception(ex);
            }
            phase = transferComplete;
            doneCallback(ex);
        }

        template<class T>
        void fail(T && e)
        {
            failEx(std::make_exception_ptr(std::forward<T>(e)));
        }

        [[noreturn]]
        void throwChangedTarget(std::string_view what, std::string_view from, std::string_view to)
        {
            throw FileTransferError(
                Misc, {}, "uri %s changed %s from %s to %s during transfer", uri, what, from, to
            );
        }

        void maybeFinishSetup()
        {
            if (phase > retrySetup) {
                return;
            }

            char * effectiveUriCStr = nullptr;
            curl_easy_getinfo(req, CURLINFO_EFFECTIVE_URL, &effectiveUriCStr);
            if (effectiveUriCStr) {
                if (!result.effectiveUri.empty() && result.effectiveUri != effectiveUriCStr) {
                    throwChangedTarget("final destination", result.effectiveUri, effectiveUriCStr);
                }
                result.effectiveUri = effectiveUriCStr;
            }

            result.cached = getHTTPStatus() == 304;

            if (phase == initialSetup) {
                metadataPromise.set_value(result);
            }
            phase = transferring;
        }

        std::exception_ptr callbackException;

        size_t writeCallback(void * contents, size_t size, size_t nmemb)
        {
            const size_t realSize = size * nmemb;

            try {
                maybeFinishSetup();

                if (successfulStatuses.count(getHTTPStatus()) && this->dataCallback) {
                    if (!dataCallback({static_cast<const char *>(contents), realSize})) {
                        return CURL_WRITEFUNC_PAUSE;
                    }
                    writtenToSink += realSize;
                } else {
                    this->downloadData.append(static_cast<const char *>(contents), realSize);
                }

                bodySize += realSize;
                return realSize;
            } catch (...) {
                callbackException = std::current_exception();
                return CURL_WRITEFUNC_ERROR;
            }
        }

        static size_t writeCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp)
        {
            return static_cast<TransferItem *>(userp)->writeCallback(contents, size, nmemb);
        }

        size_t headerCallback(void * contents, size_t size, size_t nmemb)
        try {
            size_t realSize = size * nmemb;
            std::string line(static_cast<char *>(contents), realSize);
            printMsg(lvlVomit, "got header for '%s': %s", uri, trim(line));

            static std::regex statusLine("HTTP/[^ ]+ +[0-9]+(.*)", std::regex::extended | std::regex::icase);
            if (std::smatch match; std::regex_match(line, match, statusLine)) {
                downloadData.clear();
                bodySize = 0;
                statusMsg = trim(match.str(1));
                acceptRanges = false;
            } else {
                auto i = line.find(':');
                if (i != std::string::npos) {
                    std::string name = toLower(trim(line.substr(0, i)));

                    if (name == "etag") {
                        // NOTE we don't check that the etag hasn't gone *missing*. technically
                        // this is not an error as long as we get the same data from the remote.
                        auto etag = trim(line.substr(i + 1));
                        if (!result.etag.empty() && result.etag != etag) {
                            throwChangedTarget("ETag", result.etag, etag);
                        }
                        result.etag = std::move(etag);
                    }

                    else if (name == "accept-ranges" && toLower(trim(line.substr(i + 1))) == "bytes")
                        acceptRanges = true;

                    else if (name == "link" || name == "x-amz-meta-link") {
                        auto value = trim(line.substr(i + 1));
                        static std::regex linkRegex("<([^>]*)>; rel=\"immutable\"", std::regex::extended | std::regex::icase);
                        if (std::smatch match; std::regex_match(value, match, linkRegex)) {
                            if (result.immutableUrl && result.immutableUrl != match.str(1)) {
                                throwChangedTarget("immutable url", *result.immutableUrl, match.str(1));
                            }
                            result.immutableUrl = match.str(1);
                        } else
                            debug("got invalid link header '%s'", value);
                    }
                }
            }
            return realSize;
        } catch (...) {
            callbackException = std::current_exception();
            return CURL_WRITEFUNC_ERROR;
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
            if (phase > initialSetup) {
                phase = retrySetup;
            }

            curl_easy_reset(req);

            if (verbosity >= lvlVomit) {
                curl_easy_setopt(req, CURLOPT_VERBOSE, 1);
                curl_easy_setopt(req, CURLOPT_DEBUGFUNCTION, TransferItem::debugCallback);
            }

            // use the effective URI of the previous transfer for retries. this avoids
            // some silent corruption if a redirect changes between starting and retry.
            const auto & uri = result.effectiveUri.empty() ? this->uri : result.effectiveUri;

            curl_easy_setopt(req, CURLOPT_URL, uri.c_str());
            curl_easy_setopt(req, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(req, CURLOPT_ACCEPT_ENCODING, ""); // all of them!
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

            curl_easy_setopt(req, CURLOPT_PROTOCOLS_STR, "http,https,ftp,ftps");

            curl_easy_setopt(req, CURLOPT_HTTPHEADER, requestHeaders);

            if (settings.downloadSpeed.get() > 0)
                curl_easy_setopt(req, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t) (settings.downloadSpeed.get() * 1024));

            if (noBody)
                curl_easy_setopt(req, CURLOPT_NOBODY, 1);

            if (uploadData) {
                curl_easy_setopt(req, CURLOPT_UPLOAD, 1L);
                curl_easy_setopt(req, CURLOPT_READFUNCTION, readCallbackWrapper);
                curl_easy_setopt(req, CURLOPT_READDATA, this);
                curl_easy_setopt(req, CURLOPT_INFILESIZE_LARGE, (curl_off_t) uploadData->length());
            }

            if (settings.caFile != "")
                curl_easy_setopt(req, CURLOPT_CAINFO, settings.caFile.get().c_str());

            curl_easy_setopt(req, CURLOPT_CONNECTTIMEOUT, fileTransferSettings.connectTimeout.get());

            curl_easy_setopt(req, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(req, CURLOPT_LOW_SPEED_TIME, fileTransferSettings.stalledDownloadTimeout.get());

            /* If no file exist in the specified path, curl continues to work
               anyway as if netrc support was disabled. */
            curl_easy_setopt(req, CURLOPT_NETRC_FILE, settings.netrcFile.get().c_str());
            curl_easy_setopt(req, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);

            if (writtenToSink)
                curl_easy_setopt(req, CURLOPT_RESUME_FROM_LARGE, writtenToSink);

            downloadData.clear();
            bodySize = 0;
        }

        void finish(CURLcode code)
        {
            auto httpStatus = getHTTPStatus();

            maybeFinishSetup();

            debug("finished %s of '%s'; curl status = %d, HTTP status = %d, body = %d bytes",
                verb(), uri, code, httpStatus, bodySize);

            if (callbackException)
                failEx(callbackException);

            else if (code == CURLE_OK && successfulStatuses.count(httpStatus))
            {
                act.progress(bodySize, bodySize);
                phase = transferComplete;
                doneCallback(nullptr);
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
                    response = std::move(downloadData);
                auto exc =
                    code == CURLE_ABORTED_BY_CALLBACK && _isInterrupted
                    ? FileTransferError(Interrupted, std::move(response), "%s of '%s' was interrupted", verb(), uri)
                    : httpStatus != 0
                    ? FileTransferError(err,
                        std::move(response),
                        "unable to %s '%s': HTTP error %d (%s)%s",
                        verb(), uri, httpStatus, statusMsg,
                        code == CURLE_OK ? "" : fmt(" (curl error: %s)", curl_easy_strerror(code)))
                    : FileTransferError(err,
                        std::move(response),
                        "unable to %s '%s': %s (%d)",
                        verb(), uri, curl_easy_strerror(code), code);

                /* If this is a transient error, then maybe retry the
                   download after a while. If we're writing to a
                   sink, we can only retry if the server supports
                   ranged requests. */
                if (err == Transient
                    && attempt < tries
                    && (!this->dataCallback
                        || writtenToSink == 0
                        || acceptRanges))
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

        void unpause()
        {
            auto lock = fileTransfer.state_.lock();
            lock->unpause.push_back(shared_from_this());
            fileTransfer.wakeup();
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
        std::vector<std::shared_ptr<TransferItem>> unpause;
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

        // NOTE: we will need to use CURLMOPT_TIMERFUNCTION to integrate this
        // loop with kj. until then curl will handle its timeouts internally.
        int64_t timeoutMs = INT64_MAX;

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
                    items.erase(i);
                }
            }

            /* Wait for activity, including wakeup events. */
            mc = curl_multi_poll(curlm, nullptr, 0, std::min<int64_t>(timeoutMs, INT_MAX), nullptr);
            if (mc != CURLM_OK)
                throw nix::Error("unexpected error from curl_multi_poll(): %s", curl_multi_strerror(mc));

            /* Add new curl requests from the incoming requests queue,
               except for requests that are embargoed (waiting for a
               retry timeout to expire). */

            std::vector<std::shared_ptr<TransferItem>> incoming;
            auto now = std::chrono::steady_clock::now();

            timeoutMs = INT64_MAX;

            {
                auto unpause = [&] { return std::move(state_.lock()->unpause); }();
                for (auto & item : unpause) {
                    curl_easy_pause(item->req, CURLPAUSE_CONT);
                }
            }

            {
                auto state(state_.lock());
                while (!state->incoming.empty()) {
                    auto item = state->incoming.top();
                    if (item->embargo <= now) {
                        incoming.push_back(item);
                        state->incoming.pop();
                    } else {
                        using namespace std::chrono;
                        auto wait = duration_cast<milliseconds>(item->embargo - now);
                        timeoutMs = std::min<int64_t>(timeoutMs, wait.count());
                        break;
                    }
                }
                quit = state->quit;
            }

            for (auto & item : incoming) {
                debug("starting %s of %s", item->verb(), item->uri);
                item->init();
                curl_multi_add_handle(curlm, item->req);
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
            && !item->uri.starts_with("http://")
            && !item->uri.starts_with("https://"))
            throw nix::Error("uploading to '%s' is not supported", item->uri);

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

    void upload(const std::string & uri, std::string data, const Headers & headers) override
    {
        enqueueFileTransfer(uri, headers, std::move(data), false);
    }

    std::pair<FileTransferResult, box_ptr<Source>> enqueueFileTransfer(
        const std::string & uri,
        const Headers & headers,
        std::optional<std::string> data,
        bool noBody
    )
    {
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
            curl_url_set(url, CURLUPART_URL, uri.c_str(), 0);
            char * path = nullptr;
            curl_url_get(url, CURLUPART_PATH, &path, 0);
            auto decoded = kj::decodeUriComponent(kj::arrayPtr(path, path + strlen(path)));
            if (!decoded.hadErrors && decoded.findFirst(0) == nullptr) {
                Path fsPath(decoded.cStr(), decoded.size());
                FileTransferResult metadata{.effectiveUri = std::string("file://") + path};
                struct stat st;
                AutoCloseFD fd(open(fsPath.c_str(), O_RDONLY));
                if (!fd || fstat(fd.get(), &st) != 0) {
                    throw FileTransferError(
                        NotFound, std::nullopt, "%s: file not found (%s)", fsPath, strerror(errno)
                    );
                }
                if (S_ISDIR(st.st_mode)) {
                    return {std::move(metadata), make_box_ptr<StringSource>("")};
                }
                struct OwningFdSource : FdSource
                {
                    AutoCloseFD fd;
                    OwningFdSource(AutoCloseFD fd) : FdSource(fd.get()), fd(std::move(fd)) {}
                };
                return {std::move(metadata), make_box_ptr<OwningFdSource>(std::move(fd))};
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
            auto s3Res = s3Helper.getObject(bucketName, key);
            FileTransferResult res;
            if (!s3Res.data)
                throw FileTransferError(NotFound, "S3 object '%s' does not exist", uri);
            return {res, make_box_ptr<StringSource>(std::move(*s3Res.data))};
#else
            throw nix::Error(
                "cannot download '%s' because Lix is not built with S3 support", uri
            );
#endif
        }

        struct State {
            bool done = false, failed = false;
            std::exception_ptr exc;
            std::string data;
            std::condition_variable avail;
        };

        auto _state = std::make_shared<Sync<State>>();

        auto item = enqueueItem(std::make_shared<TransferItem>(
            *this,
            uri,
            headers,
            getCurActivity(),
            [_state](std::exception_ptr ex) {
                auto state(_state->lock());
                state->done = true;
                state->exc = ex;
                state->avail.notify_one();
            },
            [_state](std::string_view data) {
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
                    return false;
                }

                /* Append data to the buffer and wake up the calling
                thread. */
                state->data.append(data);
                state->avail.notify_one();
                return true;
            },
            std::move(data),
            noBody
        ));

        struct TransferSource : Source
        {
            const std::shared_ptr<Sync<State>> _state;
            std::shared_ptr<TransferItem> transfer;
            std::string chunk;
            std::string_view buffered;

            explicit TransferSource(
                const std::shared_ptr<Sync<State>> & state, std::shared_ptr<TransferItem> transfer
            )
                : _state(state)
                , transfer(std::move(transfer))
            {
            }

            ~TransferSource()
            {
                // wake up the download thread if it's still going and have it abort
                auto state(_state->lock());
                state->failed |= !state->done;
                try {
                    transfer->unpause();
                } catch (...) {
                    ignoreExceptionInDestructor();
                }
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

                        transfer->unpause();
                        state.wait(state->avail);
                    }

                    chunk = std::move(state->data);
                    buffered = chunk;
                    transfer->unpause();
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
                    throw EndOfFile("transfer finished");
                }

                return total;
            }
        };

        auto metadata = item->metadataPromise.get_future().get();
        auto source = make_box_ptr<TransferSource>(_state, item);
        auto lock(_state->lock());
        source->awaitData(lock);
        return {std::move(metadata), std::move(source)};
    }

    bool exists(const std::string & uri, const Headers & headers) override
    {
        try {
            enqueueFileTransfer(uri, headers, std::nullopt, true);
            return true;
        } catch (FileTransferError & e) {
            /* S3 buckets return 403 if a file doesn't exist and the
                bucket is unlistable, so treat 403 as 404. */
            if (e.error == FileTransfer::NotFound || e.error == FileTransfer::Forbidden)
                return false;
            throw;
        }
    }

    std::pair<FileTransferResult, box_ptr<Source>>
    download(const std::string & uri, const Headers & headers) override
    {
        return enqueueFileTransfer(uri, headers, std::nullopt, false);
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
