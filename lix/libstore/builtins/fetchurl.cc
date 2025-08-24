#include "lix/libstore/builtins.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/compression.hh"
#include "lix/libutil/strings.hh"

namespace nix {

void builtinFetchurl(const BasicDerivation & drv, const std::string & netrcData, const std::string & caFileData)
{
    /* Make the host's netrc data available. Too bad curl requires
       this to be stored in a file. It would be nice if we could just
       pass a pointer to the data. */
    if (netrcData != "") {
        settings.netrcFile.override("netrc");
        writeFile(settings.netrcFile, netrcData, 0600);
    }

    settings.caFile.override("ca-certificates.crt");
    writeFile(settings.caFile, caFileData, 0600);

    auto getAttr = [&](const std::string & name) {
        auto i = drv.env.find(name);
        if (i == drv.env.end()) throw Error("attribute '%s' missing", name);
        return i->second;
    };

    Path storePath = getAttr("out");
    auto mainUrl = getAttr("url");
    bool unpack = getOr(drv.env, "unpack", "") == "1";

    /* Note: have to use a fresh fileTransfer here because we're in
       a forked process. */
    auto fileTransfer = makeFileTransfer();

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

        auto executable = drv.env.find("executable");
        if (executable != drv.env.end() && executable->second == "1") {
            if (chmod(storePath.c_str(), 0755) == -1)
                throw SysError("making '%1%' executable", storePath);
        }
    };

    std::async(std::launch::async, [&] {
        AsyncIoRoot aio;

        /* Try the hashed mirrors first. */
        if (getAttr("outputHashMode") == "flat") {
            for (auto hashedMirror : settings.hashedMirrors.get()) {
                try {
                    if (!hashedMirror.ends_with("/")) {
                        hashedMirror += '/';
                    }
                    std::optional<HashType> ht = parseHashTypeOpt(getAttr("outputHashAlgo"));
                    Hash h = newHashAllowEmpty(getAttr("outputHash"), ht);
                    fetch(
                        aio,
                        hashedMirror + printHashType(h.type) + "/"
                            + h.to_string(Base::Base16, false)
                    );
                    return;
                } catch (Error & e) {
                    debug("%1%", Uncolored(e.what()));
                }
            }
        }

        /* Otherwise try the specified URL. */
        fetch(aio, mainUrl);
    }).get();
}

}
