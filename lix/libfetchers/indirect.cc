#include "lix/libfetchers/fetchers.hh"
#include "lix/libfetchers/builtin-fetchers.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/url-parts.hh"
#include "lix/libstore/path.hh"

namespace nix::fetchers {

std::regex flakeRegex = regex::parse("[a-zA-Z][a-zA-Z0-9_-]*", std::regex::ECMAScript);

static const std::set<std::string> allowedIndirectAttrs = {
    "id",
    "ref",
    "rev",
};

struct IndirectInputScheme : InputScheme
{
    std::string schemeType() const override { return "indirect"; }

    const std::set<std::string> & allowedAttrs() const override {
        return allowedIndirectAttrs;
    }

    std::optional<Input> inputFromURL(const ParsedURL & url, bool requireTree) const override
    {
        if (url.scheme != "flake") return {};

        auto path = tokenizeString<std::vector<std::string>>(url.path, "/");

        std::optional<Hash> rev;
        std::optional<std::string> ref;

        Attrs attrs;

        if (path.size() == 1) {
        } else if (path.size() == 2) {
            if (std::regex_match(path[1], revRegex))
                rev = Hash::parseAny(path[1], HashType::SHA1);
            else if (std::regex_match(path[1], refRegex))
                ref = path[1];
            else
                throw BadURL("in flake URL '%s', '%s' is not a commit hash or branch/tag name", url.url, path[1]);
        } else if (path.size() == 3) {
            ref = path[1];
            rev = Hash::parseAny(path[2], HashType::SHA1);
        } else
            throw BadURL("GitHub URL '%s' is invalid", url.url);

        std::string id = path[0];

        attrs.emplace("type", "indirect");
        attrs.emplace("id", id);
        if (rev) attrs.emplace("rev", rev->gitRev());
        if (ref) attrs.emplace("ref", *ref);

        emplaceURLQueryIntoAttrs(url, attrs, {}, {});

        return inputFromAttrs(attrs);
    }

    Attrs preprocessAttrs(const Attrs & attrs) const override {
        auto id = getStrAttr(attrs, "id");
        if (!std::regex_match(id, flakeRegex))
            throw BadURL("'%s' is not a valid flake ID", id);

        // TODO come up with a nicer error message for those two.
        if (auto rev = maybeGetStrAttr(attrs, "rev")) {
            if (!std::regex_match(*rev, revRegex)) {
                throw BadURL("in flake '%s', '%s' is not a commit hash", id, *rev);
            }
        }
        if (auto ref = maybeGetStrAttr(attrs, "ref")) {
            if (!std::regex_match(*ref, refRegex)) {
                throw BadURL("in flake '%s', '%s' is not a valid branch/tag name", id, *ref);
            }
        }

        return attrs;
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) const override
    {
        std::optional<Input> input = InputScheme::inputFromAttrs(attrs);

        if (input) {
            input->direct = false;
        }

        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        ParsedURL url;
        url.scheme = "flake";
        url.path = getStrAttr(input.attrs, "id");
        if (auto ref = input.getRef()) { url.path += '/'; url.path += *ref; };
        if (auto rev = input.getRev()) { url.path += '/'; url.path += rev->gitRev(); };
        return url;
    }

    bool isLockedByRev() const override { return false; }

    bool hasAllInfo(const Input & input) const override
    {
        return false;
    }

    Input applyOverrides(
        const Input & _input,
        std::optional<std::string> ref,
        std::optional<Hash> rev) const override
    {
        auto input(_input);
        if (rev) input.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref) input.attrs.insert_or_assign("ref", *ref);
        return input;
    }

    kj::Promise<Result<std::pair<StorePath, Input>>>
    fetch(ref<Store> store, const Input & input) override
    try {
        throw Error("indirect input '%s' cannot be fetched directly", input.to_string());
    } catch (...) {
        return {result::current_exception()};
    }
};

std::unique_ptr<InputScheme> makeIndirectInputScheme()
{
    return std::make_unique<IndirectInputScheme>();
}

}
