#include "lix/libstore/curlmulti.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libstore/curlfiletransfer.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libstore/transferitem.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/c-calls.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/logging.hh"
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

#if ENABLE_DTRACE
#include "trace-probes.gen.hh"
#endif

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <regex>
#include <thread>
#include <unistd.h>

#if ENABLE_S3
#include <aws/core/client/ClientConfiguration.h>
#endif

#include <curl/curl.h>
#include <kj/async.h>
#include <kj/encoding.h>
#include <kj/time.h>

using namespace std::string_literals;

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
