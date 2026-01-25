#include "lix/libstore/builtins.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/c-calls.hh"
#include "lix/libutil/compression.hh"
#include "lix/libutil/strings.hh"

namespace nix {

void BuiltinFetchurl::run(AsyncIoRoot & aio)
{
    auto fileTransfer = getFileTransfer();

    // we also have to run the remainder of this function in a fresh thread so
    // we can have an aio root. the existing root on the current thread is not
    // safe to use because that would badly interfere with the parent process.
    auto fetch = [&](AsyncIoRoot & aio, const std::string & url) {
        auto raw = aio.blockOn(fileTransfer->download(url)).second;
        auto decompressor = makeDecompressionStream(
            unpack && mainUrl.ends_with(".xz") ? "xz" : "none", std::move(raw)
        );

        if (unpack)
            aio.blockOn(restorePath(storePath, *decompressor));
        else
            aio.blockOn(writeFile(storePath, *decompressor));

        if (executable) {
            if (sys::chmod(storePath, 0755) == -1) {
                throw SysError("making '%1%' executable", storePath);
            }
        }
    };

    /* Try the hashed mirrors first. */
    if (hash) {
        for (auto hashedMirror : settings.hashedMirrors.get()) {
            try {
                if (!hashedMirror.ends_with("/")) {
                    hashedMirror += '/';
                }
                fetch(
                    aio,
                    hashedMirror + printHashType(hash->type) + "/"
                        + hash->to_string(HashFormat::Base16, false)
                );
                return;
            } catch (Error & e) {
                debug("%1%", Uncolored(e.what()));
            }
        }
    }

    /* Otherwise try the specified URL. */
    fetch(aio, mainUrl);
}

}
