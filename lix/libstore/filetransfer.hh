#pragma once
///@file

#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/ref.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/config.hh"

#include <string>
#include <future>

namespace nix {

struct FileTransferSettings : Config
{
    #include "file-transfer-settings.gen.inc"
};

extern FileTransferSettings fileTransferSettings;

struct FileTransferResult
{
    bool cached = false;
    std::string etag;
    std::string effectiveUri;
    /* An "immutable" URL for this resource (i.e. one whose contents
       will never change), as returned by the `Link: <url>;
       rel="immutable"` header. */
    std::optional<std::string> immutableUrl;
};

class Store;

struct FileTransfer
{
    virtual ~FileTransfer() { }

    /**
     * Upload some data. May throw a FileTransferError exception.
     */
    virtual void
    upload(const std::string & uri, std::string data, const Headers & headers = {}) = 0;

    /**
     * Checks whether the given URI exists. For historical reasons this function
     * treats HTTP 403 responses like HTTP 404 responses and returns `false` for
     * both. This was originally done to handle unlistable S3 buckets, which may
     * return 403 (not 404) if the reuqested object doesn't exist in the bucket.
     *
     * ## Bugs
     *
     * S3 objects are downloaded completely to answer this request.
     */
    virtual bool exists(const std::string & uri, const Headers & headers = {}) = 0;

    /**
     * Download a file, returning its contents through a source. Will not return
     * before the transfer has fully started, ensuring that any errors thrown by
     * the setup phase (e.g. HTTP 404 or similar errors) are not postponed to be
     * thrown by the returned source. The source will only throw errors detected
     * during the transfer itself (decompression errors, connection drops, etc).
     */
    virtual std::pair<FileTransferResult, box_ptr<Source>>
    download(const std::string & uri, const Headers & headers = {}) = 0;

    enum Error { NotFound, Forbidden, Misc, Transient, Interrupted };
};

/**
 * @return a shared FileTransfer object.
 *
 * Using this object is preferred because it enables connection reuse
 * and HTTP/2 multiplexing.
 */
ref<FileTransfer> getFileTransfer();

/**
 * @return a new FileTransfer object
 *
 * Prefer getFileTransfer() to this; see its docs for why.
 */
ref<FileTransfer> makeFileTransfer(std::optional<unsigned int> baseRetryTimeMs = {});

class FileTransferError : public Error
{
public:
    FileTransfer::Error error;
    /// intentionally optional
    std::optional<std::string> response;

    template<typename... Args>
    FileTransferError(FileTransfer::Error error, std::optional<std::string> response, const Args & ... args);
};

}
