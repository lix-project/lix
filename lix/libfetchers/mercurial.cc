#include "lix/libfetchers/fetchers.hh"
#include "lix/libfetchers/cache.hh"
#include "lix/libfetchers/builtin-fetchers.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/processes.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/temporary-dir.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/url-parts.hh"
#include "lix/libutil/users.hh"

#include "lix/libfetchers/fetch-settings.hh"

#include <sys/time.h>

using namespace std::string_literals;

namespace nix::fetchers {

static RunOptions hgOptions(const Strings & args)
{
    auto env = getEnv();
    // Set HGPLAIN: this means we get consistent output from hg and avoids leakage from a user or system .hgrc.
    env["HGPLAIN"] = "";

    return {
        .program = "hg",
        .searchPath = true,
        .args = args,
        .environment = env
    };
}

// runProgram wrapper that uses hgOptions instead of stock RunOptions.
static kj::Promise<Result<std::string>> runHg(const Strings & args)
try {
    RunOptions opts = hgOptions(args);

    auto res = TRY_AWAIT(runProgram(std::move(opts)));

    if (!statusOk(res.first))
        throw ExecError(res.first, "hg %1%", statusToString(res.first));

    co_return res.second;
} catch (...) {
    co_return result::current_exception();
}

static const std::set<std::string> allowedMercurialAttrs = {
    "name",
    "ref",
    "rev",
    "revCount",
    "url",
};

struct MercurialInputScheme : InputScheme
{
    std::string schemeType() const override { return "hg"; }

    const std::set<std::string> & allowedAttrs() const override {
        return allowedMercurialAttrs;
    }

    std::optional<Input> inputFromURL(const ParsedURL & url, bool requireTree) const override
    {
        if (url.scheme != "hg+http" &&
            url.scheme != "hg+https" &&
            url.scheme != "hg+ssh" &&
            url.scheme != "hg+file") return {};

        auto url2(url);
        url2.scheme = std::string(url2.scheme, 3);
        url2.query.clear();

        Attrs attrs;
        attrs.emplace("type", "hg");

        emplaceURLQueryIntoAttrs(url, attrs, {"revCount"}, {});

        attrs.emplace("url", url2.to_string());

        return inputFromAttrs(attrs);
    }

    Attrs preprocessAttrs(const Attrs & attrs) const override
    {
        parseURL(getStrAttr(attrs, "url"));

        if (auto ref = maybeGetStrAttr(attrs, "ref")) {
            if (!std::regex_match(*ref, refRegex))
                throw BadURL("invalid Mercurial branch/tag name '%s'", *ref);
        }

        return attrs;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        url.scheme = "hg+" + url.scheme;
        if (auto rev = input.getRev()) url.query.insert_or_assign("rev", rev->gitRev());
        if (auto ref = input.getRef()) url.query.insert_or_assign("ref", *ref);
        return url;
    }

    bool hasAllInfo(const Input & input) const override
    {
        // FIXME: ugly, need to distinguish between dirty and clean
        // default trees.
        return input.getRef() == "default" || maybeGetIntAttr(input.attrs, "revCount");
    }

    Input applyOverrides(
        const Input & input,
        std::optional<std::string> ref,
        std::optional<Hash> rev) const override
    {
        auto res(input);
        if (rev) res.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref) res.attrs.insert_or_assign("ref", *ref);
        return res;
    }

    std::optional<Path> getSourcePath(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        if (url.scheme == "file" && !input.getRef() && !input.getRev())
            return url.path;
        return {};
    }

    kj::Promise<Result<void>> putFile(
        const Input & input,
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg
    ) const override
    try {
        auto [isLocal, repoPath] = getActualUrl(input);
        if (!isLocal)
            throw Error("cannot commit '%s' to Mercurial repository '%s' because it's not a working tree", path, input.to_string());

        auto absPath = CanonPath(repoPath) + path;

        writeFile(absPath.abs(), contents);

        // FIXME: shut up if file is already tracked.
        TRY_AWAIT(runHg({"add", absPath.abs()}));

        if (commitMsg) {
            TRY_AWAIT(runHg({"commit", absPath.abs(), "-m", *commitMsg}));
        }
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    std::pair<bool, std::string> getActualUrl(const Input & input) const
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        bool isLocal = url.scheme == "file";
        return {isLocal, isLocal ? url.path : url.base};
    }

    kj::Promise<Result<std::pair<StorePath, Input>>>
    fetch(ref<Store> store, const Input & _input) override
    try {
        Input input(_input);

        auto name = input.getName();

        auto [isLocal, actualUrl_] = getActualUrl(input);
        auto actualUrl = actualUrl_; // work around clang bug

        // FIXME: return lastModified.

        // FIXME: don't clone local repositories.

        if (!input.getRef() && !input.getRev() && isLocal && pathExists(actualUrl + "/.hg")) {

            bool clean =
                TRY_AWAIT(runHg({"status", "-R", actualUrl, "--modified", "--added", "--removed"}))
                == "";

            if (!clean) {

                /* This is an unclean working tree. So copy all tracked
                   files. */

                if (!fetchSettings.allowDirty)
                    throw Error("Mercurial tree '%s' is unclean", actualUrl);

                if (fetchSettings.warnDirty)
                    printTaggedWarning("Mercurial tree '%s' is unclean", actualUrl);

                input.attrs.insert_or_assign(
                    "ref", chomp(TRY_AWAIT(runHg({"branch", "-R", actualUrl})))
                );

                auto files = tokenizeString<std::set<std::string>>(
                    TRY_AWAIT(runHg(
                        {"status",
                         "-R",
                         actualUrl,
                         "--clean",
                         "--modified",
                         "--added",
                         "--no-status",
                         "--print0"}
                    )),
                    "\0"s
                );

                Path actualPath(absPath(actualUrl));

                PathFilter filter = [&](const Path & p) -> bool {
                    assert(p.starts_with(actualPath));
                    std::string file(p, actualPath.size() + 1);

                    auto st = lstat(p);

                    if (S_ISDIR(st.st_mode)) {
                        auto prefix = file + "/";
                        auto i = files.lower_bound(prefix);
                        return i != files.end() && (*i).starts_with(prefix);
                    }

                    return files.count(file);
                };

                auto storePath = TRY_AWAIT(store->addToStoreRecursive(
                    input.getName(), *prepareDump(actualPath, filter), HashType::SHA256
                ));

                co_return {std::move(storePath), input};
            }

            auto tokens = tokenizeString<std::vector<std::string>>(TRY_AWAIT(
                runHg({"identify", "-R", actualUrl, "-r", ".", "--template", "{branch} {node}"})
            ));
            assert(tokens.size() == 2);
            input.attrs.insert_or_assign("ref", tokens[0]);
            input.attrs.insert_or_assign("rev", tokens[1]);
        }

        if (!input.getRef()) input.attrs.insert_or_assign("ref", "default");

        auto checkHashType = [&](const std::optional<Hash> & hash)
        {
            if (hash.has_value() && hash->type != HashType::SHA1)
                throw Error("Hash '%s' is not supported by Mercurial. Only sha1 is supported.", hash->to_string(Base::Base16, true));
        };


        auto getLockedAttrs = [&]()
        {
            checkHashType(input.getRev());

            return Attrs({
                {"type", "hg"},
                {"name", name},
                {"rev", input.getRev()->gitRev()},
            });
        };

        auto makeResult = [&](const Attrs & infoAttrs, StorePath && storePath)
            -> std::pair<StorePath, Input>
        {
            assert(input.getRev());
            assert(!_input.getRev() || _input.getRev() == input.getRev());
            input.attrs.insert_or_assign("revCount", getIntAttr(infoAttrs, "revCount"));
            return {std::move(storePath), input};
        };

        if (input.getRev()) {
            if (auto res = TRY_AWAIT(getCache()->lookup(store, getLockedAttrs())))
                co_return makeResult(res->first, std::move(res->second));
        }

        auto revOrRef = input.getRev() ? fmt("id(%s)", input.getRev()->gitRev()) : *input.getRef();

        Attrs unlockedAttrs({
            {"type", "hg"},
            {"name", name},
            {"url", actualUrl},
            {"ref", *input.getRef()},
        });

        if (auto res = TRY_AWAIT(getCache()->lookup(store, unlockedAttrs))) {
            auto rev2 = Hash::parseAny(getStrAttr(res->first, "rev"), HashType::SHA1);
            if (!input.getRev() || input.getRev() == rev2) {
                input.attrs.insert_or_assign("rev", rev2.gitRev());
                co_return makeResult(res->first, std::move(res->second));
            }
        }

        Path cacheDir = fmt("%s/nix/hg/%s", getCacheDir(), hashString(HashType::SHA256, actualUrl).to_string(Base::Base32, false));

        /* If this is a commit hash that we already have, we don't
           have to pull again. */
        if (!(input.getRev() && pathExists(cacheDir)
              && TRY_AWAIT(runProgram(hgOptions(
                               {"identify", "-R", cacheDir, "-r", revOrRef, "--template", "1"}
                           )))
                      .second
                  == "1"))
        {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Mercurial repository '%s'", actualUrl));

            if (pathExists(cacheDir)) {
                try {
                    TRY_AWAIT(runHg({"pull", "-R", cacheDir, "--", actualUrl}));
                } catch (ExecError & e) {
                    auto transJournal = cacheDir + "/.hg/store/journal";
                    if (pathExists(transJournal)) {
                        // cannot await in catch, rest of exception handled below
                        goto failed;
                    } else {
                        throw ExecError(e.status, "'hg pull' %s", statusToString(e.status));
                    }
                }
                if (false) {
                failed:
                    /* hg throws "abandoned transaction" error only if this file exists */
                    TRY_AWAIT(runHg({"recover", "-R", cacheDir}));
                    TRY_AWAIT(runHg({"pull", "-R", cacheDir, "--", actualUrl}));
                }
            } else {
                createDirs(dirOf(cacheDir));
                TRY_AWAIT(runHg({"clone", "--noupdate", "--", actualUrl, cacheDir}));
            }
        }

        auto tokens = tokenizeString<std::vector<std::string>>(TRY_AWAIT(runHg(
            {"identify",
             "-R",
             cacheDir,
             "-r",
             revOrRef,
             "--template",
             "{node} {count(revset('::{rev}'))} {branch}"}
        )));
        assert(tokens.size() == 3);

        input.attrs.insert_or_assign("rev", Hash::parseAny(tokens[0], HashType::SHA1).gitRev());
        auto revCount = std::stoull(tokens[1]);
        input.attrs.insert_or_assign("ref", tokens[2]);

        if (auto res = TRY_AWAIT(getCache()->lookup(store, getLockedAttrs())))
            co_return makeResult(res->first, std::move(res->second));

        Path tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir, true);

        TRY_AWAIT(runHg(
            {"archive", "-R", cacheDir, "-r", fmt("id(%s)", input.getRev()->gitRev()), tmpDir}
        ));

        deletePath(tmpDir + "/.hg_archival.txt");

        auto storePath = TRY_AWAIT(store->addToStoreRecursive(name, *prepareDump(tmpDir)));

        Attrs infoAttrs({
            {"rev", input.getRev()->gitRev()},
            {"revCount", (uint64_t) revCount},
        });

        if (!_input.getRev())
            getCache()->add(
                store,
                unlockedAttrs,
                infoAttrs,
                storePath,
                false);

        getCache()->add(
            store,
            getLockedAttrs(),
            infoAttrs,
            storePath,
            true);

        co_return makeResult(infoAttrs, std::move(storePath));
    } catch (...) {
        co_return result::current_exception();
    }
};

std::unique_ptr<InputScheme> makeMercurialInputScheme()
{
    return std::make_unique<MercurialInputScheme>();
}

}
