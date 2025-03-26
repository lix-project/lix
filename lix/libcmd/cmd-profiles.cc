#include <set>

#include "lix/libcmd/cmd-profiles.hh"
#include "lix/libcmd/built-path.hh"
#include "lix/libstore/builtins/buildenv.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/logging.hh"
#include "lix/libstore/names.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/temporary-dir.hh"
#include "lix/libutil/url-name.hh"

namespace nix
{

DrvInfos queryInstalled(EvalState & state, const Path & userEnv)
{
    DrvInfos elems;
    if (pathExists(userEnv + "/manifest.json"))
        throw Error("profile '%s' is incompatible with 'nix-env'; please use 'nix profile' instead", userEnv);
    auto manifestFile = userEnv + "/manifest.nix";
    if (pathExists(manifestFile)) {
        Value v;
        state.evalFile(CanonPath(manifestFile), v);
        Bindings & bindings(*state.ctx.mem.allocBindings(0));
        getDerivations(state, v, "", bindings, elems, false);
    }
    return elems;
}

std::string showVersions(const std::set<std::string> & versions)
{
    if (versions.empty()) return "∅";
    std::set<std::string> versions2;
    for (auto & version : versions)
        versions2.insert(version.empty() ? "ε" : version);
    return concatStringsSep(", ", versions2);
}

bool ProfileElementSource::operator<(const ProfileElementSource & other) const
{
    return std::tuple(originalRef.to_string(), attrPath, outputs)
        < std::tuple(other.originalRef.to_string(), other.attrPath, other.outputs);
}

std::string ProfileElementSource::to_string() const
{
    return fmt("%s#%s%s", originalRef, attrPath, outputs.to_string());
}

std::string ProfileElement::identifier() const
{
    if (source) {
        return source->to_string();
    }
    StringSet names;
    for (auto & path : storePaths) {
        names.insert(DrvName(path.name()).name);
    }
    return concatStringsSep(", ", names);
}

std::set<std::string> ProfileElement::toInstallables(Store & store)
{
    if (source) {
        return {source->to_string()};
    }
    StringSet rawPaths;
    for (auto & path : storePaths) {
        rawPaths.insert(store.printStorePath(path));
    }
    return rawPaths;
}

std::string ProfileElement::versions() const
{
    StringSet versions;
    for (auto & path : storePaths) {
        versions.insert(DrvName(path.name()).version);
    }
    return showVersions(versions);
}

bool ProfileElement::operator<(const ProfileElement & other) const
{
    return std::tuple(identifier(), storePaths) < std::tuple(other.identifier(), other.storePaths);
}

void ProfileElement::updateStorePaths(
    ref<Store> evalStore, ref<Store> store, const BuiltPaths & builtPaths
)
{
    storePaths.clear();
    for (auto & buildable : builtPaths) {
        std::visit(
            overloaded{
                [&](const BuiltPath::Opaque & bo) { storePaths.insert(bo.path); },
                [&](const BuiltPath::Built & bfd) {
                    for (auto & output : bfd.outputs) {
                        storePaths.insert(output.second);
                    }
                },
            },
            buildable.raw()
        );
    }
}

ProfileManifest::ProfileManifest(EvalState & state, const Path & profile)
{
    auto manifestPath = profile + "/manifest.json";

    if (pathExists(manifestPath)) {
        auto json = json::parse(readFile(manifestPath), "a profile manifest");

        auto version = json.value("version", 0);
        std::string sUrl;
        std::string sOriginalUrl;
        switch (version) {
        case 1:
            sUrl = "uri";
            sOriginalUrl = "originalUri";
            break;
        case 2:
            [[fallthrough]];
        case 3:
            sUrl = "url";
            sOriginalUrl = "originalUrl";
            break;
        default:
            throw Error("profile manifest '%s' has unsupported version %d", manifestPath, version);
        }

        auto elems = json["elements"];

        for (auto & elem : elems.items()) {
            auto & e = elem.value();
            ProfileElement element;
            for (auto & p : e["storePaths"]) {
                element.storePaths.insert(state.ctx.store->parseStorePath((std::string) p));
            }
            element.active = e["active"];
            if (e.contains("priority")) {
                element.priority = e["priority"];
            }
            if (e.value(sUrl, "") != "") {
                element.source = ProfileElementSource{
                    parseFlakeRef(e[sOriginalUrl]),
                    parseFlakeRef(e[sUrl]),
                    e["attrPath"],
                    e["outputs"].get<ExtendedOutputsSpec>()};
            }

            // TODO(Qyriad): holy crap this chain of ternaries needs cleanup.
            std::string name =
                elems.is_object()
                ? elem.key()
                : element.source
                ? getNameFromURL(parseURL(element.source->to_string())).value_or(element.identifier())
                : element.identifier();

            addElement(name, std::move(element));
        }
    } else if (pathExists(profile + "/manifest.nix")) {
        // FIXME: needed because of pure mode; ugly.
        state.ctx.paths.allowPath(state.ctx.store->followLinksToStore(profile));
        state.ctx.paths.allowPath(state.ctx.store->followLinksToStore(profile + "/manifest.nix"));

        auto drvInfos = queryInstalled(state, state.ctx.store->followLinksToStore(profile));

        for (auto & drvInfo : drvInfos) {
            ProfileElement element;
            element.storePaths = {drvInfo.queryOutPath(state)};
            addElement(std::move(element));
        }
    }
}

void ProfileManifest::addElement(std::string_view nameCandidate, ProfileElement element)
{
    std::string finalName(nameCandidate);

    for (unsigned i = 1; elements.contains(finalName); ++i) {
        finalName = nameCandidate + "-" + std::to_string(i);
    }

    elements.insert_or_assign(finalName, std::move(element));
}

void ProfileManifest::addElement(ProfileElement element)
{
    auto name =
        element.source
        ? getNameFromURL(parseURL(element.source->to_string()))
        : std::nullopt;

    auto finalName = name.value_or(element.identifier());
    addElement(finalName, std::move(element));
}

JSON ProfileManifest::toJSON(Store & store) const
{
    auto es = JSON::object();
    for (auto & [name, element] : elements) {
        auto paths = JSON::array();
        for (auto & path : element.storePaths) {
            paths.push_back(store.printStorePath(path));
        }
        JSON obj;
        obj["storePaths"] = paths;
        obj["active"] = element.active;
        obj["priority"] = element.priority;
        if (element.source) {
            obj["originalUrl"] = element.source->originalRef.to_string();
            obj["url"] = element.source->lockedRef.to_string();
            obj["attrPath"] = element.source->attrPath;
            obj["outputs"] = element.source->outputs;
        }
        es[name] = obj;
    }
    JSON json;
    json["version"] = 3;
    json["elements"] = es;
    return json;
}

kj::Promise<Result<StorePath>> ProfileManifest::build(ref<Store> store)
try {
    auto tempDir = createTempDir();

    StorePathSet references;

    Packages pkgs;
    for (auto & [name, element] : elements) {
        for (auto & path : element.storePaths) {
            if (element.active) {
                pkgs.emplace_back(store->printStorePath(path), true, element.priority);
            }
            references.insert(path);
        }
    }

    buildProfile(tempDir, std::move(pkgs));

    writeFile(tempDir + "/manifest.json", toJSON(*store).dump());

    /* Add the symlink tree to the store. */
    StringSink sink;
    sink << dumpPath(tempDir);

    auto narHash = hashString(HashType::SHA256, sink.s);

    ValidPathInfo info{
        *store,
        "profile",
        FixedOutputInfo{
            .method = FileIngestionMethod::Recursive,
            .hash = narHash,
            .references =
                {
                    .others = std::move(references),
                    // profiles never refer to themselves
                    .self = false,
                },
        },
        narHash,
    };
    info.narSize = sink.s.size();

    AsyncStringInputStream source(sink.s);
    TRY_AWAIT(store->addToStore(info, source));

    co_return std::move(info.path);
} catch (...) {
    co_return result::current_exception();
}

void ProfileManifest::printDiff(
    const ProfileManifest & prev, const ProfileManifest & cur, std::string_view indent
)
{
    auto prevElemIt = prev.elements.begin();
    auto curElemIt = cur.elements.begin();

    bool changes = false;

    while (prevElemIt != prev.elements.end() || curElemIt != cur.elements.end()) {
        if (curElemIt != cur.elements.end() && (prevElemIt == prev.elements.end() || prevElemIt->first > curElemIt->first)) {
            logger->cout("%s%s: ∅ -> %s", indent, curElemIt->second.identifier(), curElemIt->second.versions());
            changes = true;
            ++curElemIt;
        } else if (prevElemIt != prev.elements.end() && (curElemIt == cur.elements.end() || prevElemIt->first < curElemIt->first)) {
            logger->cout("%s%s: %s -> ∅", indent, prevElemIt->second.identifier(), prevElemIt->second.versions());
            changes = true;
            ++prevElemIt;
        } else {
            auto v1 = prevElemIt->second.versions();
            auto v2 = curElemIt->second.versions();
            if (v1 != v2) {
                logger->cout("%s%s: %s -> %s", indent, prevElemIt->second.identifier(), v1, v2);
                changes = true;
            }
            ++prevElemIt;
            ++curElemIt;
        }
    }

    if (!changes) {
        logger->cout("%sNo changes.", indent);
    }
}
}
