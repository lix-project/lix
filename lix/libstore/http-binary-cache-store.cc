#include "lix/libstore/http-binary-cache-store.hh"
#include "lix/libstore/binary-cache-store.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/nar-info-disk-cache.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/serialise.hh"

namespace nix {

MakeError(UploadToHTTP, Error);

std::string HttpBinaryCacheStoreConfig::doc()
{
    return
#include "http-binary-cache-store.md"
        ;
}

HttpBinaryCacheStore::HttpBinaryCacheStore(
    const std::string & scheme, const Path & _cacheUri, HttpBinaryCacheStoreConfig config
)
    : Store(config)
    , BinaryCacheStore(config)
    , config_(std::move(config))
    , cacheUri(scheme + "://" + _cacheUri)
{
    if (cacheUri.back() == '/') {
        cacheUri.pop_back();
    }

    diskCache = getNarInfoDiskCache();
}

kj::Promise<Result<void>> HttpBinaryCacheStore::init()
try {
    // FIXME: do this lazily?
    if (auto cacheInfo = diskCache->upToDateCacheExists(cacheUri)) {
        config_.wantMassQuery.setDefault(cacheInfo->wantMassQuery);
        config_.priority.setDefault(cacheInfo->priority);
    } else {
        try {
            TRY_AWAIT(BinaryCacheStore::init());
        } catch (UploadToHTTP &) {
            throw Error("'%s' does not appear to be a binary cache", cacheUri);
        }
        diskCache->createCache(cacheUri, config_.storeDir, config_.wantMassQuery, config_.priority);
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

FileTransferOptions HttpBinaryCacheStore::makeOptions(Headers && headers)
{
    return {.headers = std::move(headers)};
}

void HttpBinaryCacheStore::maybeDisable()
{
    auto state(_state.lock());
    if (state->enabled && settings.tryFallback) {
        int t = 60;
        printError("disabling binary cache '%s' for %s seconds", getUri(), t);
        state->enabled = false;
        state->disabledUntil = std::chrono::steady_clock::now() + std::chrono::seconds(t);
    }
}

void HttpBinaryCacheStore::checkEnabled()
{
    auto state(_state.lock());
    if (state->enabled) {
        return;
    }
    if (std::chrono::steady_clock::now() > state->disabledUntil) {
        state->enabled = true;
        debug("re-enabling binary cache '%s'", getUri());
        return;
    }
    throw SubstituterDisabled("substituter '%s' is disabled", getUri());
}

kj::Promise<Result<bool>>
HttpBinaryCacheStore::fileExists(const std::string & path, const Activity * context)
try {
    checkEnabled();

    try {
        co_return TRY_AWAIT(getFileTransfer()->exists(makeURI(path), makeOptions(), context));
    } catch (FileTransferError & e) {
        maybeDisable();
        throw;
    }
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> HttpBinaryCacheStore::upsertFile(
    const std::string & path,
    std::shared_ptr<std::basic_iostream<char>> istream,
    const std::string & mimeType,
        const Activity * context
)
try {
    auto data = StreamToSourceAdapter(istream).drain();
    try {
        TRY_AWAIT(getFileTransfer()->upload(
            makeURI(path), std::move(data), makeOptions({{"Content-Type", mimeType}}), context
        ));
    } catch (FileTransferError & e) {
        throw UploadToHTTP("while uploading to HTTP binary cache at '%s': %s", cacheUri, e.msg());
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<box_ptr<AsyncInputStream>>>
HttpBinaryCacheStore::getFile(const std::string & path, const Activity * context)
try {
    checkEnabled();
    try {
        co_return TRY_AWAIT(getFileTransfer()->download(makeURI(path), makeOptions(), context))
            .second;
    } catch (FileTransferError & e) {
        if (e.error == FileTransfer::NotFound || e.error == FileTransfer::Forbidden) {
            throw NoSuchBinaryCacheFile(
                "file '%s' does not exist in binary cache '%s'", path, getUri()
            );
        }
        maybeDisable();
        throw;
    }

} catch (...) {
    co_return result::current_exception();
}

void registerHttpBinaryCacheStore() {
    StoreImplementations::add<HttpBinaryCacheStore, HttpBinaryCacheStoreConfig>();
}

}
