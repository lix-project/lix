#include "fetchers.hh"
#include "cache.hh"
#include "filetransfer.hh"
#include "globals.hh"
#include "store-api.hh"
#include "archive.hh"
#include "tarfile.hh"
#include "types.hh"
#include "split.hh"

namespace nix::fetchers {

DownloadFileResult downloadFile(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool locked,
    const Headers & headers)
{
    // FIXME: check store

    Attrs inAttrs({
        {"type", "file"},
        {"url", url},
        {"name", name},
    });

    auto cached = getCache()->lookupExpired(store, inAttrs);

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
        return useCached();

    FileTransferRequest request(url);
    request.headers = headers;
    if (cached)
        request.expectedETag = getStrAttr(cached->infoAttrs, "etag");
    FileTransferResult res;
    try {
        res = getFileTransfer()->transfer(request);
    } catch (FileTransferError & e) {
        if (cached) {
            warn("%s; using cached version", e.msg());
            return useCached();
        } else
            throw;
    }

    // FIXME: write to temporary file.
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
        dumpString(res.data, sink);
        auto hash = hashString(htSHA256, res.data);
        ValidPathInfo info {
            *store,
            name,
            FixedOutputInfo {
                .method = FileIngestionMethod::Flat,
                .hash = hash,
                .references = {},
            },
            hashString(htSHA256, sink.s),
        };
        info.narSize = sink.s.size();
        auto source = StringSource { sink.s };
        store->addToStore(info, source, NoRepair, NoCheckSigs);
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

    return {
        .storePath = std::move(*storePath),
        .etag = res.etag,
        .effectiveUrl = res.effectiveUri,
        .immutableUrl = res.immutableUrl,
    };
}

DownloadTarballResult downloadTarball(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool locked,
    const Headers & headers)
{
    Attrs inAttrs({
        {"type", "tarball"},
        {"url", url},
        {"name", name},
    });

    auto cached = getCache()->lookupExpired(store, inAttrs);

    if (cached && !cached->expired)
        return {
            .tree = Tree { .actualPath = store->toRealPath(cached->storePath), .storePath = std::move(cached->storePath) },
            .lastModified = (time_t) getIntAttr(cached->infoAttrs, "lastModified"),
            .immutableUrl = maybeGetStrAttr(cached->infoAttrs, "immutableUrl"),
        };

    auto res = downloadFile(store, url, name, locked, headers);

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
        unpackedStorePath = store->addToStore(name, topDir, FileIngestionMethod::Recursive, htSHA256, defaultPathFilter, NoRepair);
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

    return {
        .tree = Tree { .actualPath = store->toRealPath(*unpackedStorePath), .storePath = std::move(*unpackedStorePath) },
        .lastModified = lastModified,
        .immutableUrl = res.immutableUrl,
    };
}

// An input scheme corresponding to a curl-downloadable resource.
struct CurlInputScheme : InputScheme
{
    virtual const std::string inputType() const = 0;
    const std::set<std::string> transportUrlSchemes = {"file", "http", "https"};

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
        attrs.emplace("type", inputType());

        url.scheme = parseUrlScheme(url.scheme).transport;

        emplaceURLQueryIntoAttrs(url, attrs, {"revCount"}, {});

        attrs.emplace("url", url.to_string());
        return inputFromAttrs(attrs);
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) const override
    {
        auto type = maybeGetStrAttr(attrs, "type");
        if (type != inputType()) return {};

        // FIXME: some of these only apply to TarballInputScheme.
        std::set<std::string> allowedNames = {"type", "url", "narHash", "name", "unpack", "rev", "revCount", "lastModified"};
        for (auto & [name, value] : attrs)
            if (!allowedNames.count(name))
                throw Error("unsupported %s input attribute '%s'. If you wanted to fetch a tarball with a query parameter, please use '{ type = \"tarball\"; url = \"...\"; }'", *type, name);

        Input input;
        input.attrs = attrs;

        //input.locked = (bool) maybeGetStrAttr(input.attrs, "hash");
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        // NAR hashes are preferred over file hashes since tar/zip
        // files don't have a canonical representation.
        if (auto narHash = input.getNarHash())
            url.query.insert_or_assign("narHash", narHash->to_string(SRI, true));
        return url;
    }

    bool hasAllInfo(const Input & input) const override
    {
        return true;
    }

};

struct FileInputScheme : CurlInputScheme
{
    const std::string inputType() const override { return "file"; }

    bool isValidURL(const ParsedURL & url, bool requireTree) const override
    {
        auto parsedUrlScheme = parseUrlScheme(url.scheme);
        return transportUrlSchemes.count(std::string(parsedUrlScheme.transport))
            && (parsedUrlScheme.application
                ? parsedUrlScheme.application.value() == inputType()
                : (!requireTree && !hasTarballExtension(url.path)));
    }

    std::pair<StorePath, Input> fetch(ref<Store> store, const Input & input) override
    {
        auto file = downloadFile(store, getStrAttr(input.attrs, "url"), input.getName(), false);
        return {std::move(file.storePath), input};
    }
};

struct TarballInputScheme : CurlInputScheme
{
    const std::string inputType() const override { return "tarball"; }

    bool isValidURL(const ParsedURL & url, bool requireTree) const override
    {
        auto parsedUrlScheme = parseUrlScheme(url.scheme);

        return transportUrlSchemes.count(std::string(parsedUrlScheme.transport))
            && (parsedUrlScheme.application
                ? parsedUrlScheme.application.value() == inputType()
                : (requireTree || hasTarballExtension(url.path)));
    }

    std::pair<StorePath, Input> fetch(ref<Store> store, const Input & _input) override
    {
        Input input(_input);
        auto url = getStrAttr(input.attrs, "url");
        auto result = downloadTarball(store, url, input.getName(), false);

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

        return {result.tree.storePath, std::move(input)};
    }
};

static auto rTarballInputScheme = OnStartup([] { registerInputScheme(std::make_unique<TarballInputScheme>()); });
static auto rFileInputScheme = OnStartup([] { registerInputScheme(std::make_unique<FileInputScheme>()); });

}
