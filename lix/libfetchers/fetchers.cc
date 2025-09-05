#include "lix/libfetchers/fetchers.hh"
#include "lix/libfetchers/builtin-fetchers.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/source-path.hh"
#include "lix/libfetchers/fetch-to-store.hh"

namespace nix::fetchers {

static std::vector<std::shared_ptr<InputScheme>> inputSchemes;

void registerInputScheme(std::shared_ptr<InputScheme> && inputScheme)
{
    inputSchemes.push_back(std::move(inputScheme));
}

void initLibFetchers()
{
    registerInputScheme(makeIndirectInputScheme());
    registerInputScheme(makePathInputScheme());
    registerInputScheme(makeTarballInputScheme());
    registerInputScheme(makeFileInputScheme());
    registerInputScheme(makeGitInputScheme());
    registerInputScheme(makeGitLockedInputScheme());
    registerInputScheme(makeMercurialInputScheme());
    registerInputScheme(makeGitHubInputScheme());
    registerInputScheme(makeGitLabInputScheme());
    registerInputScheme(makeSourceHutInputScheme());
}

Input Input::fromURL(const std::string & url, bool requireTree)
{
    return fromURL(parseURL(url), requireTree);
}

static void fixupInput(Input & input)
{
    // Check common attributes.
    input.getType();
    input.getRef();
    if (input.scheme && input.scheme->isLockedByRev() && input.getRev())
        input.locked = true;
    input.getRevCount();
    input.getLastModified();
    if (input.getNarHash())
        input.locked = true;
}

Input Input::fromURL(const ParsedURL & url, bool requireTree)
{
    for (auto & inputScheme : inputSchemes) {
        auto res = inputScheme->inputFromURL(url, requireTree);
        if (res) {
            res->scheme = inputScheme;
            fixupInput(*res);
            return std::move(*res);
        }
    }

    throw Error("input '%s' is unsupported", url.url);
}

Input Input::fromAttrs(Attrs && attrs)
{
    for (auto & inputScheme : inputSchemes) {
        auto res = inputScheme->inputFromAttrs(attrs);
        if (res) {
            res->scheme = inputScheme;
            fixupInput(*res);
            return std::move(*res);
        }
    }

    Input input;
    input.attrs = attrs;
    fixupInput(input);
    return input;
}

ParsedURL Input::toURL() const
{
    if (!scheme)
        throw Error("cannot show unsupported input '%s'", attrsToJSON(attrs));
    return scheme->toURL(*this);
}

std::string Input::toURLString(const std::map<std::string, std::string> & extraQuery) const
{
    auto url = toURL();
    for (auto & attr : extraQuery)
        url.query.insert(attr);
    return url.to_string();
}

std::string Input::to_string() const
{
    return toURL().to_string();
}

Attrs Input::toAttrs() const
{
    return attrs;
}

bool Input::hasAllInfo() const
{
    return getNarHash() && scheme && scheme->hasAllInfo(*this);
}

bool Input::operator ==(const Input & other) const
{
    return attrs == other.attrs;
}

bool Input::contains(const Input & other) const
{
    if (*this == other) return true;
    auto other2(other);
    other2.attrs.erase("ref");
    other2.attrs.erase("rev");
    if (*this == other2) return true;
    return false;
}

kj::Promise<Result<std::pair<Tree, Input>>> Input::fetch(ref<Store> store) const
try {
    if (!scheme)
        throw Error("cannot fetch unsupported input '%s'", attrsToJSON(toAttrs()));

    /* The tree may already be in the Nix store, or it could be
       substituted (which is often faster than fetching from the
       original source). So check that. */
    if (hasAllInfo()) {
        try {
            auto storePath = computeStorePath(*store);

            TRY_AWAIT(store->ensurePath(storePath));

            debug("using substituted/cached input '%s' in '%s'",
                to_string(), store->printStorePath(storePath));

            co_return {
                Tree{.actualPath = store->toRealPath(storePath), .storePath = std::move(storePath)},
                *this
            };
        } catch (Error & e) {
            debug("substitution of input '%s' failed: %s", to_string(), e.what());
        }
    }

    auto [storePath, input] = TRY_AWAIT(
        [](const Input & self,
           ref<Store> store) -> kj::Promise<Result<std::pair<StorePath, Input>>> {
            try {
                // *sighs*, we print the URL without query params, rather than the full URL
                // because the Nixpkgs fileset lib tests assume that fetching shallow and
                // non-shallow prints exactly the same stderr...
                ParsedURL withoutParams = self.toURL();
                withoutParams.query.clear();
                printInfo("fetching %s input '%s'", self.getType(), withoutParams.to_string());
                try {
                    co_return TRY_AWAIT(self.scheme->fetch(store, self));
                } catch (Error & e) {
                    e.addTrace({}, "while fetching the input '%s'", self.to_string());
                    throw;
                }
            } catch (...) {
                co_return result::current_exception();
            }
        }(*this, store)
    );

    Tree tree {
        .actualPath = store->toRealPath(storePath),
        .storePath = storePath,
    };

    auto narHash = TRY_AWAIT(store->queryPathInfo(tree.storePath))->narHash;
    input.attrs.insert_or_assign("narHash", narHash.to_string(Base::SRI, true));

    if (auto prevNarHash = getNarHash()) {
        if (narHash != *prevNarHash)
            throw Error((unsigned int) 102, "NAR hash mismatch in input '%s' (%s), expected '%s', got '%s'",
                to_string(), tree.actualPath, prevNarHash->to_string(Base::SRI, true), narHash.to_string(Base::SRI, true));
    }

    if (auto prevLastModified = getLastModified()) {
        if (input.getLastModified() != prevLastModified)
            throw Error("'lastModified' attribute mismatch in input '%s', expected %d",
                input.to_string(), *prevLastModified);
    }

    if (auto prevRev = getRev()) {
        if (input.getRev() != prevRev)
            throw Error("'rev' attribute mismatch in input '%s', expected %s",
                input.to_string(), prevRev->gitRev());
    }

    if (auto prevRevCount = getRevCount()) {
        if (input.getRevCount() != prevRevCount)
            throw Error("'revCount' attribute mismatch in input '%s', expected %d",
                input.to_string(), *prevRevCount);
    }

    input.locked = true;

    assert(input.hasAllInfo());

    co_return {std::move(tree), input};
} catch (...) {
    co_return result::current_exception();
}

Input Input::applyOverrides(
    std::optional<std::string> ref,
    std::optional<Hash> rev) const
{
    if (!scheme) return *this;
    return scheme->applyOverrides(*this, ref, rev);
}

kj::Promise<Result<void>> Input::clone(const Path & destDir) const
try {
    assert(scheme);
    TRY_AWAIT(scheme->clone(*this, destDir));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

std::optional<Path> Input::getSourcePath() const
{
    assert(scheme);
    return scheme->getSourcePath(*this);
}

kj::Promise<Result<void>> Input::putFile(
    const CanonPath & path, std::string_view contents, std::optional<std::string> commitMsg
) const
try {
    assert(scheme);
    TRY_AWAIT(scheme->putFile(*this, path, contents, commitMsg));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

std::string Input::getName() const
{
    return maybeGetStrAttr(attrs, "name").value_or("source");
}

StorePath Input::computeStorePath(Store & store) const
{
    auto narHash = getNarHash();
    if (!narHash)
        throw Error("cannot compute store path for unlocked input '%s'", to_string());
    return store.makeFixedOutputPath(getName(), FixedOutputInfo {
        .method = FileIngestionMethod::Recursive,
        .hash = *narHash,
        .references = {},
    });
}

std::string Input::getType() const
{
    return getStrAttr(attrs, "type");
}

std::optional<Hash> Input::getNarHash() const
{
    if (auto s = maybeGetStrAttr(attrs, "narHash")) {
        auto hash = s->empty() ? Hash(HashType::SHA256) : Hash::parseSRI(*s);
        if (hash.type != HashType::SHA256)
            throw UsageError("narHash must use SHA-256");
        return hash;
    }
    return {};
}

std::optional<std::string> Input::getRef() const
{
    return maybeGetStrAttr(attrs, "ref");
}

std::optional<Hash> Input::getRev() const
{
    std::optional<Hash> hash = {};

    if (auto s = maybeGetStrAttr(attrs, "rev")) {
        try {
            hash = Hash::parseAnyPrefixed(*s);
        } catch (BadHash &e) {
            // Default to sha1 for backwards compatibility with existing flakes
            hash = Hash::parseAny(*s, HashType::SHA1);
        }
    }

    return hash;
}

std::optional<uint64_t> Input::getRevCount() const
{
    return maybeGetIntAttr(attrs, "revCount");
}

std::optional<time_t> Input::getLastModified() const
{
    return maybeGetIntAttr(attrs, "lastModified");
}

std::optional<Input> InputScheme::inputFromAttrs(const Attrs & attrs) const
{
    if (maybeGetStrAttr(attrs, "type") != schemeType()) return {};

    Attrs finalAttrs = preprocessAttrs(attrs);

    for (auto & [name, value] : finalAttrs)
        // All attrs need to accept a `type` and `narHash` key, the rest is scheme-specific
        if (name != "type" && name != "narHash" && !allowedAttrs().contains(name))
            throw UnsupportedAttributeError("unsupported input attribute '%s' for the '%s' scheme", name, schemeType());

    Input input;
    input.attrs = finalAttrs;
    return input;
}



ParsedURL InputScheme::toURL(const Input & input) const
{
    throw Error("don't know how to convert input '%s' to a URL", attrsToJSON(input.attrs));
}

Input InputScheme::applyOverrides(
    const Input & input,
    std::optional<std::string> ref,
    std::optional<Hash> rev) const
{
    if (ref)
        throw Error("don't know how to set branch/tag name of input '%s' to '%s'", input.to_string(), *ref);
    if (rev)
        throw Error("don't know how to set revision of input '%s' to '%s'", input.to_string(), rev->gitRev());
    return input;
}

std::optional<Path> InputScheme::getSourcePath(const Input & input) const
{
    return {};
}

kj::Promise<Result<void>> InputScheme::putFile(
    const Input & input,
    const CanonPath & path,
    std::string_view contents,
    std::optional<std::string> commitMsg
) const
try {
    throw Error("input '%s' does not support modifying file '%s'", input.to_string(), path);
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> InputScheme::clone(const Input & input, const Path & destDir) const
try {
    throw Error("do not know how to clone input '%s'", input.to_string());
} catch (...) {
    co_return result::current_exception();
}
}
