#pragma once
///@file

#if ENABLE_S3

#include "lix/libutil/types.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/ref.hh"

#include <kj/async.h>
#include <optional>
#include <string>

namespace Aws { namespace Client { class ClientConfiguration; } }
namespace Aws { namespace S3 { class S3Client; } }

namespace nix {

struct S3Helper
{
    ref<Aws::Client::ClientConfiguration> config;
    ref<Aws::S3::S3Client> client;

    S3Helper(
        const std::string & profile,
        const std::string & region,
        const std::string & scheme,
        const std::string & endpoint,

        /* Exception names that can be retried even though the AWS SDK does not specify as such.
         * Required for compatibility with non-AWS S3 implementations.
         */
        const Strings & retryableExceptionNames = {}
    );

    ref<Aws::Client::ClientConfiguration> makeConfig(
        const std::string & region,
        const std::string & scheme,
        const std::string & endpoint,
        const Strings & retryableExceptionNames = {}
    );

    struct FileTransferResult
    {
        std::optional<std::string> data;
        unsigned int durationMs;
    };

    kj::Promise<Result<FileTransferResult>>
    getObject(const std::string & bucketName, const std::string & key);
};

}

#endif
