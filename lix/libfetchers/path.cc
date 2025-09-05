#include "lix/libfetchers/fetchers.hh"
#include "lix/libfetchers/builtin-fetchers.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/result.hh"

namespace nix::fetchers {

/* Allow the user to pass in "fake" tree info
   attributes. This is useful for making a pinned tree
   work the same as the repository from which is exported
   (e.g. path:/nix/store/...-source?lastModified=1585388205&rev=b0c285...). */
static const std::set<std::string> allowedPathAttrs = {
    "lastModified",
    "path",
    "rev",
    "revCount",
};

struct PathInputScheme : InputScheme
{
    std::string schemeType() const override { return "path"; }

    const std::set<std::string> & allowedAttrs() const override {
        return allowedPathAttrs;
    }

    std::optional<Input> inputFromURL(const ParsedURL & url, bool requireTree) const override
    {
        if (url.scheme != "path") return {};

        if (url.authority && *url.authority != "")
            throw Error("path URL '%s' should not have an authority ('%s')", url.url, *url.authority);

        Input input;
        input.attrs.insert_or_assign("type", "path");
        input.attrs.insert_or_assign("path", url.path);

        for (auto & [name, value] : url.query)
            if (name == "rev" || name == "narHash")
                input.attrs.insert_or_assign(name, value);
            else if (name == "revCount" || name == "lastModified") {
                if (auto n = string2Int<uint64_t>(value))
                    input.attrs.insert_or_assign(name, *n);
                else
                    throw Error("path URL '%s' has invalid parameter '%s'", url.to_string(), name);
            }
            else
                throw Error("path URL '%s' has unsupported parameter '%s'", url.to_string(), name);

        return input;
    }

    Attrs preprocessAttrs(const Attrs & attrs) const override
    {
        getStrAttr(attrs, "path");

        return attrs;
    }

    bool isLockedByRev() const override { return false; }

    ParsedURL toURL(const Input & input) const override
    {
        auto query = attrsToQuery(input.attrs);
        query.erase("path");
        query.erase("type");
        return ParsedURL {
            .scheme = "path",
            .path = getStrAttr(input.attrs, "path"),
            .query = query,
        };
    }

    bool hasAllInfo(const Input & input) const override
    {
        return true;
    }

    std::optional<Path> getSourcePath(const Input & input) const override
    {
        return getStrAttr(input.attrs, "path");
    }

    kj::Promise<Result<void>> putFile(
        const Input & input,
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg
    ) const override
    try {
        writeFile((CanonPath(getAbsPath(input)) + path).abs(), contents);
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    CanonPath getAbsPath(const Input & input) const
    {
        auto path = getStrAttr(input.attrs, "path");

        if (path[0] == '/')
            return CanonPath(path);

        throw Error("cannot fetch input '%s' because it uses a relative path", input.to_string());
    }

    kj::Promise<Result<std::pair<StorePath, Input>>>
    fetch(ref<Store> store, const Input & _input) override
    try {
        Input input(_input);
        std::string absPath;
        auto path = getStrAttr(input.attrs, "path");

        if (path[0] != '/') {
            if (!input.parent)
                throw Error("cannot fetch input '%s' because it uses a relative path", input.to_string());

            auto parent = canonPath(*input.parent);

            // the path isn't relative, prefix it
            absPath = nix::absPath(path, parent);

            // for security, ensure that if the parent is a store path, it's inside it
            if (store->isInStore(parent)) {
                auto storePath = store->printStorePath(store->toStorePath(parent).first);
                if (!isDirOrInDir(absPath, storePath))
                    throw BadStorePath("relative path '%s' points outside of its parent's store path '%s'", path, storePath);
            }
        } else
            absPath = path;

        Activity act(*logger, lvlTalkative, actUnknown, fmt("copying '%s'", absPath));

        // FIXME: check whether access to 'path' is allowed.
        auto storePath = store->maybeParseStorePath(absPath);

        if (storePath)
            TRY_AWAIT(store->addTempRoot(*storePath));

        time_t mtime = 0;
        if (!storePath || storePath->name() != "source"
            || !TRY_AWAIT(store->isValidPath(*storePath)))
        {
            // FIXME: try to substitute storePath.
            auto src = AsyncGeneratorInputStream{dumpPathAndGetMtime(absPath, mtime)};
            storePath = TRY_AWAIT(store->addToStoreFromDump(src, "source"));
        }
        input.attrs.insert_or_assign("lastModified", uint64_t(mtime));

        co_return {std::move(*storePath), input};
    } catch (...) {
        co_return result::current_exception();
    }
};

std::unique_ptr<InputScheme> makePathInputScheme()
{
    return std::make_unique<PathInputScheme>();
}

}
