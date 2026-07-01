#include "lix/libstore/curlfiletransfer.hh"
#include "lix/libstore/curlmulti.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libutil/ref.hh"

#include <optional>

#include <fcntl.h>
#include <unistd.h>

#if ENABLE_S3
#include <aws/core/client/ClientConfiguration.h>
#endif

#include <curl/curl.h>
#include <kj/async.h>
#include <kj/encoding.h>
#include <kj/time.h>

namespace nix {

FileTransferSettings fileTransferSettings;

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
