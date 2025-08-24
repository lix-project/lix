#include "lix/libfetchers/fetchers.hh"
#include "lix/libfetchers/cache.hh"
#include "lix/libstore/content-address.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libstore/globals.hh"
#include "lix/libfetchers/builtin-fetchers.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/tarfile.hh"
#include "lix/libstore/temporary-dir.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/split.hh"

namespace nix::fetchers {

/* Note [Recursive hashing of file inputs]:
 * We recursively hash `file` inputs to be consistent with the way that we look
 * up the paths in binary caches (which is assumed all over the place in
 * flakes, like in `nix flake archive`).
 *
 * It would also be easier to always hash downloadFile outputs as recursive,
 * but there are a *whole bunch* of random usages of downloadFile for stuff
 * from channel tarballs to GitHub API that I haven't fully figured out what
 * they are doing or what the implications are for changing them. It seems that
 * for things that are not directly *themselves* flake inputs, the intent is
 * that they are flat-hashed, but flake inputs are always assumed
 * recursive-hashed.
 *
 * So we make the flake usage of it do the thing that's right for flakes, and
 * everything else we leave as before.
 *
 * Quoth Dr. Eelco Dolstra: https://github.com/NixOS/nix/pull/6548#discussion_r877921756
 * > A problem with the use of downloadFile() is that it uses
 * > FileIngestionMethod::Flat instead of FileIngestionMethod::Recursive.
 * > Currently it's assumed that all flake inputs use recursive+sha256 with a
 * > name of "source". This allows inputs to be substituted using the narHash
 * > attribute in the lock file (see Input::computeStorePath()). However, the
 * > lazy trees branch will probably remove the ability to substitute inputs
 * > anyway...
 *
 * In other words, the feature was supposed to have been implemented consistent
 * with other flake input types as recursive-hashed, but it was forgotten
 * before merging it into CppNix, and was later fixed by some version between
 * 2.19 and 2.24, which we are now consistent with.
 *
 * See: https://github.com/edolstra/flake-compat/pull/44
 */

kj::Promise<Result<DownloadFileResult>> downloadFile(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool locked,
    Headers headers,
    FileIngestionMethod ingestionMethod)
try {
    // FIXME: check store

    Attrs inAttrs({
        {"type", "file"},
        {"url", url},
        {"name", name},
        {"ingestionMethod", uint64_t(ingestionMethod)},
    });

    auto cached = TRY_AWAIT(getCache()->lookupExpired(store, inAttrs));

    auto useCached = [&]() -> DownloadFileResult
    {
        return {
            .storePath = std::move(cached->storePath),
            .etag = getStrAttr(cached->infoAttrs, "etag"),
            .effectiveUrl = getStrAttr(cached->infoAttrs, "url"),
            .immutableUrl = maybeGetStrAttr(cached->infoAttrs, "immutableUrl"),
        };
    };

    if (cached && !cached->expired)
        co_return useCached();

    if (cached)
        headers.emplace_back("If-None-Match", getStrAttr(cached->infoAttrs, "etag"));
    FileTransferResult res;
    std::string data;
    try {
        auto [meta, content] = TRY_AWAIT(getFileTransfer()->download(url, {headers}));
        res = std::move(meta);
        data = TRY_AWAIT(content->drain());
    } catch (FileTransferError & e) {
        if (cached) {
            printTaggedWarning("%s; using cached version", Uncolored(e.msg()));
            co_return useCached();
        } else
            throw;
    }

    // FIXME: write to temporary file. or stream it. or like. anything but load
    // the entire thing in memory.
    Attrs infoAttrs({
        {"etag", res.etag},
        {"url", res.effectiveUri},
    });

    if (res.immutableUrl)
        infoAttrs.emplace("immutableUrl", *res.immutableUrl);

    std::optional<StorePath> storePath;

    if (res.cached) {
        assert(cached);
        storePath = std::move(cached->storePath);
    } else {
        StringSink sink;
        sink << dumpString(data);
        auto flatHash = [&]() { return hashString(HashType::SHA256, data); };
        // See Note [Recursive hashing of file inputs].
        auto narHash = hashString(HashType::SHA256, sink.s);
        ValidPathInfo info {
            *store,
            name,
            FixedOutputInfo {
                .method = ingestionMethod,
                .hash = ingestionMethod == FileIngestionMethod::Flat ? flatHash() : narHash,
                .references = {},
            },
            narHash,
        };
        info.narSize = sink.s.size();
        auto source = AsyncStringInputStream { sink.s };
        TRY_AWAIT(store->addToStore(info, source, NoRepair, NoCheckSigs));
        storePath = std::move(info.path);
    }

    getCache()->add(
        store,
        inAttrs,
        infoAttrs,
        *storePath,
        locked);

    if (url != res.effectiveUri)
        getCache()->add(
            store,
            {
                {"type", "file"},
                {"url", res.effectiveUri},
                {"name", name},
            },
            infoAttrs,
            *storePath,
            locked);

    co_return DownloadFileResult{
        .storePath = std::move(*storePath),
        .etag = res.etag,
        .effectiveUrl = res.effectiveUri,
        .immutableUrl = res.immutableUrl,
    };
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<DownloadTarballResult>> downloadTarball(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool locked,
    const Headers & headers)
try {
    Attrs inAttrs({
        {"type", "tarball"},
        {"url", url},
        {"name", name},
    });

    auto cached = TRY_AWAIT(getCache()->lookupExpired(store, inAttrs));

    if (cached && !cached->expired)
        co_return DownloadTarballResult{
            .tree = Tree { .actualPath = store->toRealPath(cached->storePath), .storePath = std::move(cached->storePath) },
            .lastModified = (time_t) getIntAttr(cached->infoAttrs, "lastModified"),
            .immutableUrl = maybeGetStrAttr(cached->infoAttrs, "immutableUrl"),
        };

    auto res = TRY_AWAIT(downloadFile(store, url, name, locked, headers));

    std::optional<StorePath> unpackedStorePath;
    time_t lastModified;

    if (cached && res.etag != "" && getStrAttr(cached->infoAttrs, "etag") == res.etag) {
        unpackedStorePath = std::move(cached->storePath);
        lastModified = getIntAttr(cached->infoAttrs, "lastModified");
    } else {
        Path tmpDir = createTempDir();
        AutoDelete autoDelete(tmpDir, true);
        unpackTarfile(store->toRealPath(res.storePath), tmpDir);
        auto members = readDirectory(tmpDir);
        if (members.size() != 1)
            throw nix::Error("tarball '%s' contains an unexpected number of top-level files", url);
        auto topDir = tmpDir + "/" + members.begin()->name;
        lastModified = lstat(topDir).st_mtime;
        unpackedStorePath = TRY_AWAIT(
            store->addToStoreRecursive(name, *prepareDump(topDir), HashType::SHA256, NoRepair)
        );
    }

    Attrs infoAttrs({
        {"lastModified", uint64_t(lastModified)},
        {"etag", res.etag},
    });

    if (res.immutableUrl)
        infoAttrs.emplace("immutableUrl", *res.immutableUrl);

    getCache()->add(
        store,
        inAttrs,
        infoAttrs,
        *unpackedStorePath,
        locked);

    co_return DownloadTarballResult{
        .tree = Tree { .actualPath = store->toRealPath(*unpackedStorePath), .storePath = std::move(*unpackedStorePath) },
        .lastModified = lastModified,
        .immutableUrl = res.immutableUrl,
    };
} catch (...) {
    co_return result::current_exception();
}

// FIXME: some of these only apply to TarballInputScheme.
static const std::set<std::string> allowedCurlAttrs = {
    "lastModified",
    "name",
    "rev",
    "revCount",
    "unpack",
    "url",
};

// An input scheme corresponding to a curl-downloadable resource.
struct CurlInputScheme : InputScheme
{
    const std::set<std::string> transportUrlSchemes = {"file", "http", "https"};

    const std::set<std::string> & allowedAttrs() const override {
        return allowedCurlAttrs;
    }

    bool hasTarballExtension(std::string_view path) const
    {
        return path.ends_with(".zip") || path.ends_with(".tar")
            || path.ends_with(".tgz") || path.ends_with(".tar.gz")
            || path.ends_with(".tar.xz") || path.ends_with(".tar.bz2")
            || path.ends_with(".tar.zst");
    }

    virtual bool isValidURL(const ParsedURL & url, bool requireTree) const = 0;

    std::optional<Input> inputFromURL(const ParsedURL & _url, bool requireTree) const override
    {
        if (!isValidURL(_url, requireTree))
            return std::nullopt;

        auto url = _url;

        Attrs attrs;
        attrs.emplace("type", schemeType());

        url.scheme = parseUrlScheme(url.scheme).transport;

        emplaceURLQueryIntoAttrs(url, attrs, {"revCount", "lastModified"}, {});

        attrs.emplace("url", url.to_string());
        return inputFromAttrs(attrs);
    }

    Attrs preprocessAttrs(const Attrs & attrs) const override
    {
        for (auto & [name, value] : attrs)
            if (name != "type" && name != "narHash" && !allowedAttrs().contains(name))
                throw UnsupportedAttributeError("unsupported tarball input attribute '%s'. If you wanted to fetch a tarball with a query parameter, please use '{ type = \"tarball\"; url = \"...\"; }'", name);

        return attrs;
    }


    ParsedURL toURL(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        // NAR hashes are preferred over file hashes since tar/zip
        // files don't have a canonical representation.
        if (auto narHash = input.getNarHash())
            url.query.insert_or_assign("narHash", narHash->to_string(Base::SRI, true));
        return url;
    }

    bool isLockedByRev() const override { return false; }

    bool hasAllInfo(const Input & input) const override
    {
        return true;
    }

};

struct FileInputScheme : CurlInputScheme
{
    std::string schemeType() const override { return "file"; }

    bool isValidURL(const ParsedURL & url, bool requireTree) const override
    {
        auto parsedUrlScheme = parseUrlScheme(url.scheme);
        return transportUrlSchemes.count(std::string(parsedUrlScheme.transport))
            && (parsedUrlScheme.application
                ? parsedUrlScheme.application.value() == schemeType()
                : (!requireTree && !hasTarballExtension(url.path)));
    }

    kj::Promise<Result<std::pair<StorePath, Input>>>
    fetch(ref<Store> store, const Input & input) override
    try {
        auto file =
            // See Note [Recursive hashing of file inputs].
            TRY_AWAIT(downloadFile(store, getStrAttr(input.attrs, "url"), input.getName(), false, {}, FileIngestionMethod::Recursive));
        co_return {std::move(file.storePath), input};
    } catch (...) {
        co_return result::current_exception();
    }
};

struct TarballInputScheme : CurlInputScheme
{
    std::string schemeType() const override { return "tarball"; }

    bool isValidURL(const ParsedURL & url, bool requireTree) const override
    {
        auto parsedUrlScheme = parseUrlScheme(url.scheme);

        return transportUrlSchemes.count(std::string(parsedUrlScheme.transport))
            && (parsedUrlScheme.application
                ? parsedUrlScheme.application.value() == schemeType()
                : (requireTree || hasTarballExtension(url.path)));
    }

    kj::Promise<Result<std::pair<StorePath, Input>>>
    fetch(ref<Store> store, const Input & _input) override
    try {
        Input input(_input);
        auto url = getStrAttr(input.attrs, "url");
        auto result = TRY_AWAIT(downloadTarball(store, url, input.getName(), false));

        if (result.immutableUrl) {
            auto immutableInput = Input::fromURL(*result.immutableUrl);
            // FIXME: would be nice to support arbitrary flakerefs
            // here, e.g. git flakes.
            if (immutableInput.getType() != "tarball")
                throw Error("tarball 'Link' headers that redirect to non-tarball URLs are not supported");
            input = immutableInput;
        }

        if (result.lastModified && !input.attrs.contains("lastModified"))
            input.attrs.insert_or_assign("lastModified", uint64_t(result.lastModified));

        co_return {result.tree.storePath, std::move(input)};
    } catch (...) {
        co_return result::current_exception();
    }
};

std::unique_ptr<InputScheme> makeFileInputScheme()
{
    return std::make_unique<FileInputScheme>();
}

std::unique_ptr<InputScheme> makeTarballInputScheme()
{
    return std::make_unique<TarballInputScheme>();
}

}
