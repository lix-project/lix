#include "lix/libcmd/command.hh"
#include "lix/libcmd/cmd-profiles.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libstore/names.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/result.hh"
#include "diff-closures.hh"

#include <regex>

namespace nix {

static constexpr std::string_view CLOSURE_DIFF_SCHEMA_VERSION = "lix-closure-diff-v1";

struct Info
{
    std::string outputName;
};

struct DiffInfoForPackage
{
    int64_t sizeDelta;
    std::set<std::string> addedVersions;
    std::set<std::string> removedVersions;
};

// name -> version -> store paths
typedef std::map<std::string, std::map<std::string, std::map<StorePath, Info>>> GroupedPaths;

typedef std::map<std::string, DiffInfoForPackage> DiffInfo;

JSON toJSON(const DiffInfo & diff)
{
    JSON res = JSON::object();
    JSON content = JSON::object();

    for (auto & [name, item] : diff) {
        auto packageContent = JSON::object();

        if (!item.removedVersions.empty() || !item.addedVersions.empty()) {
            packageContent["versionsBefore"] = item.removedVersions;
            packageContent["versionsAfter"] = item.addedVersions;
        }
        packageContent["sizeDelta"] = item.sizeDelta;

        content[name] = std::move(packageContent);
    }

    res["packages"] = std::move(content);
    res["schema"] = CLOSURE_DIFF_SCHEMA_VERSION;

    return res;
}

static kj::Promise<Result<GroupedPaths>>
getClosureInfo(ref<Store> store, const StorePath & toplevel)
try {
    StorePathSet closure;
    TRY_AWAIT(store->computeFSClosure({toplevel}, closure));

    GroupedPaths groupedPaths;

    for (auto const & path : closure) {
        /* Strip the output name. Unfortunately this is ambiguous (we
           can't distinguish between output names like "bin" and
           version suffixes like "unstable"). */
        static std::regex regex = regex::parse("(.*)-([a-z]+|lib32|lib64)");
        std::cmatch match;
        std::string name{path.name()};
        std::string_view const origName = path.name();
        std::string outputName;

        if (std::regex_match(origName.begin(), origName.end(), match, regex)) {
            name = match[1];
            outputName = match[2];
        }

        DrvName drvName(name);
        groupedPaths[drvName.name][drvName.version].emplace(path, Info { .outputName = outputName });
    }

    co_return groupedPaths;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<DiffInfo>> getDiffInfo(
    ref<Store> store,
    const StorePath & beforePath,
    const StorePath & afterPath)
try {
    auto beforeClosure = TRY_AWAIT(getClosureInfo(store, beforePath));
    auto afterClosure = TRY_AWAIT(getClosureInfo(store, afterPath));

    std::set<std::string> allNames;
    for (auto & [name, _] : beforeClosure) allNames.insert(name);
    for (auto & [name, _] : afterClosure) allNames.insert(name);

    DiffInfo itemsToPrint;

    for (auto & name : allNames) {
        auto & beforeVersions = beforeClosure[name];
        auto & afterVersions = afterClosure[name];

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        auto totalSize = [&](const std::map<std::string, std::map<StorePath, Info>> & versions
                         ) -> kj::Promise<Result<uint64_t>> {
            try {
                uint64_t sum = 0;
                for (auto & [_, paths] : versions)
                    for (auto & [path, _] : paths)
                        sum += TRY_AWAIT(store->queryPathInfo(path))->narSize;
                co_return sum;
            } catch (...) {
                co_return result::current_exception();
            }
        };

        auto beforeSize = TRY_AWAIT(totalSize(beforeVersions));
        auto afterSize = TRY_AWAIT(totalSize(afterVersions));
        auto sizeDelta = (int64_t) afterSize - (int64_t) beforeSize;

        std::set<std::string> removed, unchanged;
        for (auto & [version, _] : beforeVersions)
            if (!afterVersions.count(version)) removed.insert(version); else unchanged.insert(version);

        std::set<std::string> added;
        for (auto & [version, _] : afterVersions)
            if (!beforeVersions.count(version)) added.insert(version);

        if (!removed.empty() || !added.empty()) {
            auto info = DiffInfoForPackage {
                .sizeDelta = sizeDelta,
                .addedVersions = added,
                .removedVersions = removed
            };

            itemsToPrint[name] = std::move(info);
        }
    }

    co_return itemsToPrint;
} catch (...) {
    co_return result::current_exception();
}

void renderDiffInfo(
    DiffInfo diff,
    const std::string_view indent)
{
    for (auto & [name, item] : diff) {
        auto showDelta = std::abs(item.sizeDelta) >= 8 * 1024;

        std::vector<std::string> line;
        if (!item.removedVersions.empty() || !item.addedVersions.empty())
            line.push_back(fmt("%s → %s", showVersions(item.removedVersions), showVersions(item.addedVersions)));
        if (showDelta)
            line.push_back(fmt("%s%+.1f KiB" ANSI_NORMAL, item.sizeDelta > 0 ? ANSI_RED : ANSI_GREEN, item.sizeDelta / 1024.0));
        logger->cout("%s%s: %s", indent, name, concatStringsSep(", ", line));
    }
}

kj::Promise<Result<void>> printClosureDiff(
    ref<Store> store,
    const StorePath & beforePath,
    const StorePath & afterPath,
    const bool json,
    const std::string_view indent)
try {
    DiffInfo diff = TRY_AWAIT(getDiffInfo(store, beforePath, afterPath));

    if (json) {
        logger->cout(toJSON(diff).dump());
    } else {
        renderDiffInfo(diff, indent);
    }

    co_return result::success();
} catch (...) {

    co_return result::current_exception();
}

}

namespace nix {

struct CmdDiffClosures : SourceExprCommand, MixJSON, MixOperateOnOptions
{
    std::string _before, _after;

    CmdDiffClosures()
    {
        expectArg("before", &_before);
        expectArg("after", &_after);
    }

    std::string description() override
    {
        return "show what packages and versions were added and removed between two closures";
    }

    std::string doc() override
    {
        return
          #include "diff-closures.md"
          ;
    }

    void run(ref<Store> store) override
    {
        auto state = getEvaluator()->begin(aio());
        auto before = parseInstallable(*state, store, _before);
        auto beforePath = Installable::toStorePath(*state, getEvalStore(), store, Realise::Outputs, operateOn, before);
        auto after = parseInstallable(*state, store, _after);
        auto afterPath = Installable::toStorePath(*state, getEvalStore(), store, Realise::Outputs, operateOn, after);
        aio().blockOn(printClosureDiff(store, beforePath, afterPath, json, ""));
    }
};

void registerNixStoreDiffClosures() {
    registerCommand2<CmdDiffClosures>({"store", "diff-closures"});
}

}
