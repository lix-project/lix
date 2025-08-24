#include "lix/libstore/filetransfer.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/error.hh"
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

#include <cstddef>
#include <cstdio>
#include <exception>
#include <kj/async.h>
#include <kj/encoding.h>
#include <kj/time.h>

#if ENABLE_DTRACE
#include "trace-probes.gen.hh"
#endif

#if ENABLE_S3
#include <aws/core/client/ClientConfiguration.h>
#endif

#include <unistd.h>
#include <fcntl.h>

#include <curl/curl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <regex>

using namespace std::string_literals;

namespace nix {

FileTransferSettings fileTransferSettings;

static GlobalConfig::Register rFileTransferSettings(&fileTransferSettings);

struct curlFileTransfer : public FileTransfer
{
    std::unique_ptr<CURLM, decltype([](auto * m) { curl_multi_cleanup(m); })> curlm;

    const unsigned int baseRetryTimeMs;

    struct TransferItem
    {
        struct DownloadState
        {
            bool done = false;
            std::exception_ptr exc;
            std::string data;
            std::optional<kj::Own<kj::CrossThreadPromiseFulfiller<void>>> downloadEvent;

            auto wait()
            {
                auto pfp = kj::newPromiseAndCrossThreadFulfiller<void>();
                downloadEvent = std::move(pfp.fulfiller);
                return std::move(pfp.promise);
            }

            void signal()
            {
                if (downloadEvent) {
                    (*downloadEvent)->fulfill();
                    downloadEvent.reset();
                }
            }
        };

        std::string uri;
        FileTransferResult result;
        Activity act;
        std::unique_ptr<FILE, decltype([](FILE * f) { fclose(f); })> uploadData;
        Sync<DownloadState> downloadState;
        bool headersDone = false, metadataReturned = false;
        kj::Own<kj::CrossThreadPromiseFulfiller<Result<FileTransferResult>>> metadataPromise;
        std::string statusMsg;

        uint64_t bodySize = 0;

        std::unique_ptr<curl_slist, decltype([](auto * s) { curl_slist_free_all(s); })> requestHeaders;
        std::unique_ptr<CURL, decltype([](auto * c) { curl_easy_cleanup(c); })> req;
        // buffer to accompany the `req` above
        char errbuf[CURL_ERROR_SIZE];

        inline static const std::set<long> successfulStatuses {200, 201, 204, 206, 304, 0 /* other protocol */};

        std::optional<long> httpStatusCode;

        /* Get the scheme for the current curl handle, or none if curl returns NULL.
         * Ensures the scheme is always casefolded to lowercase */
        std::optional<std::string> getCurlScheme() {
            char *scheme_raw = nullptr;
            if (curl_easy_getinfo(req.get(), CURLINFO_SCHEME, &scheme_raw) != CURLE_OK) {
                throw nix::Error("could not get scheme used from curl handle");
            }
            if (scheme_raw) {
                return toLower(std::string(scheme_raw));
            } else {
                return {};
            }
        }

        /* Get the HTTP status code, or 0 for other protocols. */
        long getHTTPStatus()
        {
            if (httpStatusCode) {
                return *httpStatusCode;
            }

            long statusCode = 0;

            std::optional<std::string> scheme = getCurlScheme();
            if (scheme == "http" || scheme == "https") {
                if (curl_easy_getinfo(req.get(), CURLINFO_RESPONSE_CODE, &statusCode) != CURLE_OK) {
                    throw nix::Error("could not get response code from curl handle");
                }
            }

            httpStatusCode = statusCode;
            return statusCode;
        }

        std::string verb() const
        {
            return uploadData ? "upload" : "download";
        }

        TransferItem(
            const std::string & uri,
            FileTransferOptions && options,
            ActivityId parentAct,
            std::optional<std::string_view> uploadData,
            bool noBody,
            curl_off_t writtenToSink,
            kj::Own<kj::CrossThreadPromiseFulfiller<Result<FileTransferResult>>> metadataPromise,
            const std::chrono::milliseconds & connectTimeout
        )
            : uri(uri)
            , act(*logger,
                  lvlTalkative,
                  actFileTransfer,
                  fmt(uploadData ? "uploading '%s'" : "downloading '%s'", uri),
                  {uri},
                  parentAct)
            , metadataPromise(std::move(metadataPromise))
            , req(curl_easy_init())
        {
            if (req == nullptr) {
                throw FileTransferError(Misc, {}, "could not allocate curl handle");
            }
            for (auto it = options.headers.begin(); it != options.headers.end(); ++it){
                if (auto next = curl_slist_append(
                        requestHeaders.get(), fmt("%s: %s", it->first, it->second).c_str()
                    );
                    next != nullptr)
                {
                    (void) requestHeaders.release(); // next now owns this pointer
                    requestHeaders.reset(next);
                } else {
                    throw FileTransferError(Misc, {}, "could not allocate curl request headers");
                }
            }

            if (verbosity >= lvlVomit) {
                curl_easy_setopt(req.get(), CURLOPT_VERBOSE, 1);
                curl_easy_setopt(req.get(), CURLOPT_DEBUGFUNCTION, TransferItem::debugCallback);
            }

            curl_easy_setopt(req.get(), CURLOPT_URL, uri.c_str());
            curl_easy_setopt(req.get(), CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(req.get(), CURLOPT_ACCEPT_ENCODING, ""); // all of them!
            curl_easy_setopt(req.get(), CURLOPT_MAXREDIRS, 10);
            curl_easy_setopt(req.get(), CURLOPT_NOSIGNAL, 1);
            curl_easy_setopt(req.get(), CURLOPT_USERAGENT,
                ("curl/" LIBCURL_VERSION " Lix/" + nixVersion +
                    (fileTransferSettings.userAgentSuffix != "" ? " " + fileTransferSettings.userAgentSuffix.get() : "")).c_str());
            curl_easy_setopt(req.get(), CURLOPT_PIPEWAIT, 1);
            if (fileTransferSettings.enableHttp2)
                curl_easy_setopt(req.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
            else
                curl_easy_setopt(req.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
            curl_easy_setopt(req.get(), CURLOPT_WRITEFUNCTION, TransferItem::writeCallbackWrapper);
            curl_easy_setopt(req.get(), CURLOPT_WRITEDATA, this);
            curl_easy_setopt(req.get(), CURLOPT_HEADERFUNCTION, TransferItem::headerCallbackWrapper);
            curl_easy_setopt(req.get(), CURLOPT_HEADERDATA, this);

            curl_easy_setopt(req.get(), CURLOPT_XFERINFOFUNCTION, progressCallbackWrapper);
            curl_easy_setopt(req.get(), CURLOPT_PROGRESSDATA, this);
            curl_easy_setopt(req.get(), CURLOPT_NOPROGRESS, 0);

            curl_easy_setopt(req.get(), CURLOPT_ERRORBUFFER, errbuf);
            errbuf[0] = 0;

            curl_easy_setopt(req.get(), CURLOPT_PROTOCOLS_STR, "http,https,ftp,ftps");

            curl_easy_setopt(req.get(), CURLOPT_HTTPHEADER, requestHeaders.get());

            if (settings.downloadSpeed.get() > 0)
                curl_easy_setopt(req.get(), CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t) (settings.downloadSpeed.get() * 1024));

            if (noBody)
                curl_easy_setopt(req.get(), CURLOPT_NOBODY, 1);

            if (uploadData) {
                this->uploadData.reset(fmemopen(const_cast<char *>(uploadData->data()), uploadData->size(), "r"));
                curl_easy_setopt(req.get(), CURLOPT_UPLOAD, 1L);
                curl_easy_setopt(req.get(), CURLOPT_READDATA, this->uploadData.get());
                curl_easy_setopt(req.get(), CURLOPT_INFILESIZE_LARGE, (curl_off_t) uploadData->length());
            }

            if (settings.caFile != "")
                curl_easy_setopt(req.get(), CURLOPT_CAINFO, settings.caFile.get().c_str());

            curl_easy_setopt(req.get(), CURLOPT_CONNECTTIMEOUT_MS, connectTimeout.count());

            curl_easy_setopt(req.get(), CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(req.get(), CURLOPT_LOW_SPEED_TIME, fileTransferSettings.stalledDownloadTimeout.get());

            /* If no file exist in the specified path, curl continues to work
               anyway as if netrc support was disabled. */
            curl_easy_setopt(req.get(), CURLOPT_NETRC_FILE, settings.netrcFile.get().c_str());
            curl_easy_setopt(req.get(), CURLOPT_NETRC, CURL_NETRC_OPTIONAL);

            if (writtenToSink)
                curl_easy_setopt(req.get(), CURLOPT_RESUME_FROM_LARGE, writtenToSink);

            if (options.extraSetup) {
                options.extraSetup(req.get());
            }
        }

        bool acceptsRanges()
        {
            curl_header * h;
            if (curl_easy_header(req.get(), "accept-ranges", 0, CURLH_HEADER, -1, &h)) {
                // treat any error as the remote not accepting range requests. the only
                // interesting local error is out-of-memory, which we can't even handle
                return false;
            }

            return toLower(trim(h->value)) == "bytes";
        }

        void failEx(std::exception_ptr ex)
        {
            auto state = downloadState.lock();
            assert(!state->done && !state->exc);
            if (!metadataReturned) {
                metadataPromise->fulfill(ex);
            }
            state->exc = ex;
            state->signal();
        }

        template<class T>
        void fail(T && e)
        {
            failEx(std::make_exception_ptr(std::forward<T>(e)));
        }

        void maybeFinishSetup()
        {
            if (headersDone) {
                return;
            }

            auto status = getHTTPStatus();

            char * effectiveUriCStr = nullptr;
            curl_easy_getinfo(req.get(), CURLINFO_EFFECTIVE_URL, &effectiveUriCStr);
            if (effectiveUriCStr) {
                result.effectiveUri = effectiveUriCStr;
            }

            result.cached = status == 304;
            if (successfulStatuses.contains(status)) {
                metadataPromise->fulfill(result);
                metadataReturned = true;
            }

            headersDone = true;
        }

        std::exception_ptr callbackException;

        size_t writeCallback(void * contents, size_t size, size_t nmemb)
        {
            const size_t realSize = size * nmemb;

            try {
                maybeFinishSetup();

                auto state = downloadState.lock();

                // when the buffer is full (as determined by a historical magic value) we
                // pause the transfer and wait for the receiver to unpause it when ready.
                if (successfulStatuses.count(getHTTPStatus()) && state->data.size() > 1024 * 1024) {
                    return CURL_WRITEFUNC_PAUSE;
                }

                state->data.append(static_cast<const char *>(contents), realSize);
                state->signal();
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

            static std::regex statusLine = regex::parse("HTTP/[^ ]+ +[0-9]+(.*)", std::regex::extended | std::regex::icase);
            if (std::smatch match; std::regex_match(line, match, statusLine)) {
                statusMsg = trim(match.str(1));
            } else {
                auto i = line.find(':');
                if (i != std::string::npos) {
                    std::string name = toLower(trim(line.substr(0, i)));

                    if (name == "etag") {
                        // NOTE we don't check that the etag hasn't gone *missing*. technically
                        // this is not an error as long as we get the same data from the remote.
                        auto etag = trim(line.substr(i + 1));
                        result.etag = std::move(etag);
                    }

                    else if (name == "link" || name == "x-amz-meta-link") {
                        auto value = trim(line.substr(i + 1));
                        static std::regex linkRegex = regex::parse("<([^>]*)>; rel=\"immutable\"", std::regex::extended | std::regex::icase);
                        if (std::smatch match; std::regex_match(value, match, linkRegex)) {
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

        int progressCallback(curl_off_t dltotal, curl_off_t dlnow)
        {
            try {
                act.progress(dlnow, dltotal);
            } catch (nix::Interrupted &) {
            }
            return isInterrupted();
        }

        static int progressCallbackWrapper(void * userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
        {
            return static_cast<TransferItem *>(userp)->progressCallback(dltotal, dlnow);
        }

        static int debugCallback(CURL * handle, curl_infotype type, char * data, size_t size, void * userptr)
        {
            if (type == CURLINFO_TEXT)
                vomit("curl: %s", chomp(std::string(data, size)));
            return 0;
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
                auto state = downloadState.lock();
                state->done = true;
                state->signal();
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

                std::optional<std::string> response;
                if (!successfulStatuses.count(httpStatus))
                    response = std::move(downloadState.lock()->data);

                auto textualError = [](const char * errbuf, CURLcode code) -> const char * {
                    if (errbuf && errbuf[0]) {
                        return errbuf;
                    } else {
                        return curl_easy_strerror(code);
                    }
                };
                auto exc = code == CURLE_ABORTED_BY_CALLBACK && isInterrupted()
                    ? FileTransferError(
                          Interrupted,
                          std::move(response),
                          "%s of '%s' was interrupted",
                          verb(),
                          uri
                      )
                    : httpStatus != 0
                    ? FileTransferError(
                          err,
                          std::move(response),
                          "unable to %s '%s': HTTP error %d (%s)%s",
                          verb(),
                          uri,
                          httpStatus,
                          statusMsg,
                          code == CURLE_OK
                              ? ""
                              : fmt(" (curl error code=%d: %s)", code, textualError(errbuf, code))
                      )
                    : FileTransferError(
                          err,
                          std::move(response),
                          "unable to %s '%s': %s (curl error code=%d)",
                          verb(),
                          uri,
                          textualError(errbuf, code),
                          code
                      );

                fail(std::move(exc));
            }
        }
    };

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

    curlFileTransfer(unsigned int baseRetryTimeMs)
        : curlm(curl_multi_init())
        , baseRetryTimeMs(baseRetryTimeMs)
    {
        if (curlm == nullptr) {
            throw FileTransferError(Misc, {}, "could not allocate curl handle");
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

            // only exit when all transfers are done (which will happen through the
            // progress callback issuing an abort in the case of user interruption)
            if (items.empty() && quit) {
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
        curlFileTransfer & parent;
        std::string uri;
        FileTransferOptions options;
        std::optional<std::string> data;
        bool noBody;
        ActivityId parentAct;

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
            : parent(parent)
            , uri(uri)
            , options(options)
            , data(std::move(data))
            , noBody(noBody)
            , parentAct(context ? context->id : 0)
            , backoff(backoffTimeouts(
                  fileTransferSettings.tries,
                  std::chrono::seconds(fileTransferSettings.maxConnectTimeout.get()),
                  std::chrono::seconds(fileTransferSettings.initialConnectTimeout.get()),
                  std::chrono::milliseconds(parent.baseRetryTimeMs)
              ))
        {
        }

        ~TransferStream()
        {
            // wake up the download thread if it's still going and have it abort
            try {
                if (transfer) {
                    parent.cancel(transfer);
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
            parent.enqueueItem(transfer);
            co_return TRY_AWAIT(pfp.promise);
        } catch (...) {
            co_return result::current_exception();
        }

        void throwChangedTarget(std::string_view what, std::string_view from, std::string_view to)
        {
            if (!from.empty() && from != to) {
                throw FileTransferError(
                    Misc, {}, "uri %s changed %s from %s to %s during transfer", uri, what, from, to
                );
            }
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
                    waitTime,
                    Uncolored(attempt),
                    Uncolored(tries)
                );
            } else {
                printTaggedWarning(
                    "%s; retrying in %d ms (attempt %d/%d)",
                    Uncolored(context),
                    waitTime,
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
            // use the effective URI of the previous transfer for retries. this avoids
            // some silent corruption if a redirect changes between starting and retry
            const auto & uri = metadata.effectiveUri.empty() ? this->uri : metadata.effectiveUri;

            auto newMeta = TRY_AWAIT(startTransfer(uri, timeout, totalReceived));
            throwChangedTarget("final destination", metadata.effectiveUri, newMeta.effectiveUri);
            throwChangedTarget("ETag", metadata.etag, newMeta.etag);
            throwChangedTarget(
                "immutable url",
                metadata.immutableUrl.value_or(""),
                newMeta.immutableUrl.value_or("")
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
                    parent.unpause(transfer);
                } else if (state->exc) {
                    std::rethrow_exception(state->exc);
                } else if (state->done) {
                    co_return false;
                } else {
                    parent.unpause(transfer);
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
