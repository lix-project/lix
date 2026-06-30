#pragma once
///@file

#include "lix/libstore/filetransfer.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/sync.hh"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/system.h>
#include <kj/async.h>
#include <kj/memory.h>

namespace nix {

struct TransferItem
{
    // Types.
public:
    template<typename T>
    using Async = kj::Promise<Result<T>>;

    struct DownloadState
    {
        // Fields.
    public:
        bool done = false;
        std::exception_ptr exc;
        std::string data;
        std::optional<kj::Own<kj::CrossThreadPromiseFulfiller<void>>> downloadEvent;


        // Actual API.
    public:
        kj::Promise<void> wait();

        void signal();
    };

    // Fields.
public:
    std::string uri;
    FileTransferResult result;
    Activity act;
    std::unique_ptr<FILE, decltype([](FILE * f) { fclose(f); })> uploadData;
    Sync<DownloadState> downloadState;
    bool headersDone = false;
    bool metadataReturned = false;
    kj::Own<kj::CrossThreadPromiseFulfiller<Result<FileTransferResult>>> metadataPromise;
    std::string statusMsg;

    uint64_t bodySize = 0;

    std::unique_ptr<curl_slist, decltype([](auto * s) { curl_slist_free_all(s); })> requestHeaders;
    std::unique_ptr<CURL, decltype([](auto * c) { curl_easy_cleanup(c); })> req;
    // buffer to accompany the `req` above
    char errbuf[CURL_ERROR_SIZE];

    inline static std::set<long> successfulStatuses{
        200, 201, 204, 206, 304, 0 /* other protocol */
    };

    std::optional<long> httpStatusCode;

    std::exception_ptr callbackException;

    // Specials.
public:
    TransferItem(
        std::string const & uri,
        FileTransferOptions && options,
        Activity const * parentAct,
        std::optional<std::string_view> uploadData,
        bool noBody,
        curl_off_t writtenToSink,
        kj::Own<kj::CrossThreadPromiseFulfiller<Result<FileTransferResult>>> metadataPromise,
        std::chrono::milliseconds const & connectTimeout
    );

    // Actual API.
public:
    std::optional<std::string> getCurlScheme();

    long getHTTPStatus();

    std::string verb() const;

    bool acceptsRanges();

    void failEx(std::exception_ptr ex);

    template<typename T>
    void fail(T && e)
    {
        failEx(std::make_exception_ptr(std::forward<T>(e)));
    }

    void maybeFinishSetup();

    size_t writeCallback(void * contents, size_t size, size_t nmemb);

    static size_t writeCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp);

    size_t headerCallback(void * contents, size_t size, size_t nmemb);

    static size_t headerCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp);

    int progressCallback(curl_off_t dltotal, curl_off_t dlnow);

    static int progressCallbackWrapper(
        void * userp,
        curl_off_t dltotal,
        curl_off_t dlnow,
        curl_off_t ultotal,
        curl_off_t ulnow
    );

    static int debugCallback(CURL * handle, curl_infotype type, char * data, size_t size, void * userptr);

    void finish(CURLcode code);
};

}
