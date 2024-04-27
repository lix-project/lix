#include "cmd-profiles.hh"
#include "built-path.hh"
#include "builtins/buildenv.hh"
#include "names.hh"
#include "store-api.hh"

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
        state.evalFile(state.rootPath(CanonPath(manifestFile)), v);
        Bindings & bindings(*state.allocBindings(0));
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
        auto json = nlohmann::json::parse(readFile(manifestPath));

        auto version = json.value("version", 0);
        std::string sUrl;
        std::string sOriginalUrl;
        switch (version) {
        case 1:
            sUrl = "uri";
            sOriginalUrl = "originalUri";
            break;
        case 2:
            sUrl = "url";
            sOriginalUrl = "originalUrl";
            break;
        default:
            throw Error("profile manifest '%s' has unsupported version %d", manifestPath, version);
        }

        for (auto & e : json["elements"]) {
            ProfileElement element;
            for (auto & p : e["storePaths"]) {
                element.storePaths.insert(state.store->parseStorePath((std::string) p));
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
            elements.emplace_back(std::move(element));
        }
    }

    else if (pathExists(profile + "/manifest.nix"))
    {
        // FIXME: needed because of pure mode; ugly.
        state.allowPath(state.store->followLinksToStore(profile));
        state.allowPath(state.store->followLinksToStore(profile + "/manifest.nix"));

        auto drvInfos = queryInstalled(state, state.store->followLinksToStore(profile));

        for (auto & drvInfo : drvInfos) {
            ProfileElement element;
            element.storePaths = {drvInfo.queryOutPath()};
            elements.emplace_back(std::move(element));
        }
    }
}

nlohmann::json ProfileManifest::toJSON(Store & store) const
{
    auto array = nlohmann::json::array();
    for (auto & element : elements) {
        auto paths = nlohmann::json::array();
        for (auto & path : element.storePaths) {
            paths.push_back(store.printStorePath(path));
        }
        nlohmann::json obj;
        obj["storePaths"] = paths;
        obj["active"] = element.active;
        obj["priority"] = element.priority;
        if (element.source) {
            obj["originalUrl"] = element.source->originalRef.to_string();
            obj["url"] = element.source->lockedRef.to_string();
            obj["attrPath"] = element.source->attrPath;
            obj["outputs"] = element.source->outputs;
        }
        array.push_back(obj);
    }
    nlohmann::json json;
    json["version"] = 2;
    json["elements"] = array;
    return json;
}

StorePath ProfileManifest::build(ref<Store> store)
{
    auto tempDir = createTempDir();

    StorePathSet references;

    Packages pkgs;
    for (auto & element : elements) {
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
    dumpPath(tempDir, sink);

    auto narHash = hashString(htSHA256, sink.s);

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

    StringSource source(sink.s);
    store->addToStore(info, source);

    return std::move(info.path);
}

void ProfileManifest::printDiff(
    const ProfileManifest & prev, const ProfileManifest & cur, std::string_view indent
)
{
    auto prevElems = prev.elements;
    std::sort(prevElems.begin(), prevElems.end());

    auto curElems = cur.elements;
    std::sort(curElems.begin(), curElems.end());

    auto i = prevElems.begin();
    auto j = curElems.begin();

    bool changes = false;

    while (i != prevElems.end() || j != curElems.end()) {
        if (j != curElems.end() && (i == prevElems.end() || i->identifier() > j->identifier())) {
            logger->cout("%s%s: ∅ -> %s", indent, j->identifier(), j->versions());
            changes = true;
            ++j;
        } else if (i != prevElems.end() && (j == curElems.end() || i->identifier() < j->identifier())) {
            logger->cout("%s%s: %s -> ∅", indent, i->identifier(), i->versions());
            changes = true;
            ++i;
        } else {
            auto v1 = i->versions();
            auto v2 = j->versions();
            if (v1 != v2) {
                logger->cout("%s%s: %s -> %s", indent, i->identifier(), v1, v2);
                changes = true;
            }
            ++i;
            ++j;
        }
    }

    if (!changes) {
        logger->cout("%sNo changes.", indent);
    }
}
}
