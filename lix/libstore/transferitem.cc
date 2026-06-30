#include <chrono>
#include <exception>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>

#include <curl/curl.h>
#include <curl/header.h>
#include <curl/system.h>
#include <kj/async.h>
#include <kj/memory.h>

#include "lix/libstore/filetransfer.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/transferitem.hh"
#include "lix/libutil/c-calls.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/fmt.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/strings.hh"

namespace nix {

kj::Promise<void> TransferItem::DownloadState::wait()
{
    auto pfp = kj::newPromiseAndCrossThreadFulfiller<void>();
    downloadEvent = std::move(pfp.fulfiller);
    return std::move(pfp.promise);
}

void TransferItem::DownloadState::signal()
{
    if (downloadEvent) {
        (*downloadEvent)->fulfill();
        downloadEvent.reset();
    }
}

TransferItem::TransferItem(
    std::string const & uri,
    FileTransferOptions && options,
    Activity const * parentAct,
    std::optional<std::string_view> uploadData,
    bool noBody,
    curl_off_t writtenToSink,
    kj::Own<kj::CrossThreadPromiseFulfiller<Result<FileTransferResult>>> metadataPromise,
    std::chrono::milliseconds const & connectTimeout
)
    : uri(uri)
    , act(logger->startActivity(
        lvlTalkative,
        actFileTransfer,
        fmt(uploadData ? "uploading '%s'" : "downloading '%s'", uri),
        {uri},
        parentAct
    ))
    , metadataPromise(std::move(metadataPromise))
    , req(curl_easy_init())
{
    if (req == nullptr) {
        throw FileTransferError(FileTransfer::Misc, {}, "could not allocate curl handle");
    }

    for (auto it = options.headers.begin(); it != options.headers.end(); ++it) {
        if (auto next = curl_slist_append(
                requestHeaders.get(), requireCString(fmt("%s: %s", it->first, it->second))
            );
            next != nullptr)
        {
            (void) requestHeaders.release(); // next now owns this pointer
            requestHeaders.reset(next);
        } else {
            throw FileTransferError(
                FileTransfer::Misc, {}, "could not allocate curl request headers"
            );
        }
    }

    if (getVerbosity() >= lvlVomit) {
        curl_easy_setopt(req.get(), CURLOPT_VERBOSE, 1);
        curl_easy_setopt(req.get(), CURLOPT_DEBUGFUNCTION, TransferItem::debugCallback);
    }

    curl_easy_setopt(req.get(), CURLOPT_URL, uri.c_str());
    curl_easy_setopt(req.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(req.get(), CURLOPT_ACCEPT_ENCODING, ""); // all of them!
    curl_easy_setopt(req.get(), CURLOPT_MAXREDIRS, 10);
    curl_easy_setopt(req.get(), CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(
        req.get(),
        CURLOPT_USERAGENT,
        ("curl/" LIBCURL_VERSION " Lix/" + nixVersion
         + (fileTransferSettings.userAgentSuffix != ""
                ? " " + fileTransferSettings.userAgentSuffix.get()
                : ""))
            .c_str()
    );
    curl_easy_setopt(req.get(), CURLOPT_PIPEWAIT, 1);
    if (fileTransferSettings.enableHttp3) {
        if (!fileTransferSettings.enableHttp2) {
            static std::once_flag warningPrinted;
            std::call_once(warningPrinted, [] {
                printTaggedWarning("http3 implies http2; ignoring explicit http2 setting.");
            });
        }
        curl_easy_setopt(req.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3);
    } else if (fileTransferSettings.enableHttp2) {
        curl_easy_setopt(req.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    } else {
        curl_easy_setopt(req.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    }
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

    if (settings.downloadSpeed.get() > 0) {
        curl_easy_setopt(
            req.get(),
            CURLOPT_MAX_RECV_SPEED_LARGE,
            (curl_off_t) (settings.downloadSpeed.get() * 1024)
        );
    }

    if (noBody) {
        curl_easy_setopt(req.get(), CURLOPT_NOBODY, 1);
    }

    if (uploadData) {
        this->uploadData.reset(
            fmemopen(const_cast<char *>(uploadData->data()), uploadData->size(), "r")
        );
        curl_easy_setopt(req.get(), CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(req.get(), CURLOPT_READDATA, this->uploadData.get());
        curl_easy_setopt(
            req.get(), CURLOPT_INFILESIZE_LARGE, (curl_off_t) uploadData->length()
        );
    }

    if (settings.caFile != "") {
        /* check if the caFile that has been specified actually exists
           NOTE: since nixpkgs sets this to "/no-cert-file.crt" by default this needs to be ignored too */
        if (settings.caFile.get() != "/no-cert-file.crt" && !pathExists(settings.caFile.get())) {
            throw Error("ca file does not exist at specified location '%s'", settings.caFile.get());
        }
        curl_easy_setopt(req.get(), CURLOPT_CAINFO, settings.caFile.get().c_str());
    }

    curl_easy_setopt(req.get(), CURLOPT_CONNECTTIMEOUT_MS, connectTimeout.count());

    curl_easy_setopt(req.get(), CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(
        req.get(), CURLOPT_LOW_SPEED_TIME, fileTransferSettings.stalledDownloadTimeout.get()
    );

    /* If the netrc path has been changed from its default value
       set it to required otherwise keep it optional and throw an error if it is missing */
    if (settings.netrcFile.isChanged() && !pathExists(settings.netrcFile.get())) {
        throw Error("netrc file does not exist at specified location '%s'", settings.netrcFile.get());
    }
    curl_easy_setopt(req.get(), CURLOPT_NETRC_FILE, settings.netrcFile.get().c_str());
    curl_easy_setopt(
        req.get(),
        CURLOPT_NETRC,
        settings.netrcFile.isChanged() ? CURL_NETRC_OPTIONAL : CURL_NETRC_OPTIONAL
    );

    if (writtenToSink) {
        curl_easy_setopt(req.get(), CURLOPT_RESUME_FROM_LARGE, writtenToSink);
    }

    if (options.extraSetup) {
        options.extraSetup(req.get());
    }
}

/* Get the scheme for the current curl handle, or none if curl returns NULL.
 * Ensures the scheme is always casefolded to lowercase */
std::optional<std::string> TransferItem::getCurlScheme()
{
    char * scheme_raw = nullptr;
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
long TransferItem::getHTTPStatus()
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

std::string TransferItem::verb() const
{
    return uploadData ? "upload" : "download";
}

bool TransferItem::acceptsRanges()
{
    curl_header * h;
    if (curl_easy_header(req.get(), "accept-ranges", 0, CURLH_HEADER, -1, &h)) {
        // treat any error as the remote not accepting range requests. the only
        // interesting local error is out-of-memory, which we can't even handle
        return false;
    }

    return toLower(trim(h->value)) == "bytes";
}

void TransferItem::failEx(std::exception_ptr ex)
{
    auto state = downloadState.lock();
    assert(!state->done && !state->exc);
    if (!metadataReturned) {
        metadataPromise->fulfill(ex);
    }
    state->exc = ex;
    state->signal();
}

void TransferItem::maybeFinishSetup()
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

size_t TransferItem::writeCallback(void * contents, size_t size, size_t nmemb)
{
    const size_t realSize = size * nmemb;

    try {
        maybeFinishSetup();

        auto state = downloadState.lock();

        // when the buffer is full (as determined by a historical magic value) we
        // pause the transfer and wait for the receiver to unpause it when ready.
        if (successfulStatuses.count(getHTTPStatus()) && state->data.size() > 1024ul * 1024) {
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

size_t TransferItem::writeCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp)
{
    return static_cast<TransferItem *>(userp)->writeCallback(contents, size, nmemb);
}


size_t TransferItem::headerCallback(void * contents, size_t size, size_t nmemb)
try {
    size_t realSize = size * nmemb;
    std::string line(static_cast<char *>(contents), realSize);
    printMsg(lvlVomit, "got header for '%s': %s", uri, trim(line));

    static std::regex statusLine =
        regex::parse("HTTP/[^ ]+ +[0-9]+(.*)", std::regex::extended | std::regex::icase);
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

            else if (name == "link" || name == "x-amz-meta-link")
            {
                auto value = trim(line.substr(i + 1));
                static std::regex linkRegex = regex::parse(
                    "<([^>]*)>; rel=\"immutable\"", std::regex::extended | std::regex::icase
                );
                if (std::smatch match; std::regex_match(value, match, linkRegex)) {
                    result.immutableUrl = match.str(1);
                } else {
                    debug("got invalid link header '%s'", value);
                }
            }
        }
    }
    return realSize;
} catch (...) {
    callbackException = std::current_exception();
    return CURL_WRITEFUNC_ERROR;
}

size_t TransferItem::headerCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp)
{
    return static_cast<TransferItem *>(userp)->headerCallback(contents, size, nmemb);
}

int TransferItem::progressCallback(curl_off_t dltotal, curl_off_t dlnow)
{
    try {
        if (act.progress(dlnow, dltotal) == Logger::BufferState::NeedsFlush) {
            act.getLogger().waitForSpace(); // NOLINT(lix-never-async)
        }
    } catch (nix::Interrupted &) {
    }
    return isInterrupted();
}

int TransferItem::progressCallbackWrapper(
    void * userp,
    curl_off_t dltotal,
    curl_off_t dlnow,
    curl_off_t ultotal,
    curl_off_t ulnow
)
{
    return static_cast<TransferItem *>(userp)->progressCallback(dltotal, dlnow);
}

int TransferItem::debugCallback(
    CURL * handle,
    curl_infotype type,
    char * data,
    size_t size,
    void * userptr
)
{
    if (type == CURLINFO_TEXT) {
        vomit("curl: %s", chomp(std::string(data, size)));
    }
    return 0;
}

void TransferItem::finish(CURLcode code)
{
    auto httpStatus = getHTTPStatus();

    maybeFinishSetup();

    debug(
        "finished %s of '%s'; curl status = %d, HTTP status = %d, body = %d bytes",
        verb(),
        uri,
        code,
        httpStatus,
        bodySize
    );

    if (callbackException) {
        failEx(callbackException);
    }

    else if (code == CURLE_OK && successfulStatuses.count(httpStatus))
    {
        if (act.progress(bodySize, bodySize) == Logger::BufferState::NeedsFlush) {
            act.getLogger().waitForSpace(); // NOLINT(lix-never-async)
        }
        auto state = downloadState.lock();
        state->done = true;
        state->signal();
    }

    else
    {
        // We treat most errors as transient, but won't retry when hopeless
        auto err = FileTransfer::Transient;

        if (httpStatus == 404 || httpStatus == 410 || code == CURLE_FILE_COULDNT_READ_FILE) {
            // The file is definitely not there
            err = FileTransfer::NotFound;
        } else if (httpStatus == 401 || httpStatus == 403 || httpStatus == 407) {
            // Don't retry on authentication/authorization failures
            err = FileTransfer::Forbidden;
        } else if (httpStatus >= 400 && httpStatus < 500 && httpStatus != 408
                   && httpStatus != 429)
        {
            // Most 4xx errors are client errors and are probably not worth retrying:
            //   * 408 means the server timed out waiting for us, so we try again
            //   * 429 means too many requests, so we retry (with a delay)
            err = FileTransfer::Misc;
        } else if (httpStatus == 501 || httpStatus == 505 || httpStatus == 511) {
            // Let's treat most 5xx (server) errors as transient, except for a handful:
            //   * 501 not implemented
            //   * 505 http version not supported
            //   * 511 we're behind a captive portal
            err = FileTransfer::Misc;
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
                err = FileTransfer::Misc;
                break;
            default: // Shut up warnings
                break;
            }
#pragma GCC diagnostic pop
        }

        std::optional<std::string> response;
        if (!successfulStatuses.count(httpStatus)) {
            response = std::move(downloadState.lock()->data);
        }

        auto textualError = [](const char * errbuf, CURLcode code) -> const char * {
            if (errbuf && errbuf[0]) {
                return errbuf;
            } else {
                return curl_easy_strerror(code);
            }
        };
        auto exc = code == CURLE_ABORTED_BY_CALLBACK && isInterrupted()
            ? FileTransferError(
                  FileTransfer::Interrupted,
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

} // namespace nix
