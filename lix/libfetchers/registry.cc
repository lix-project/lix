#include "lix/libfetchers/registry.hh"
#include "lix/libfetchers/fetchers.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/sync.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/users.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/local-fs-store.hh"

#include "lix/libfetchers/fetch-settings.hh"

#include <memory>
#include <mutex>

namespace nix::fetchers {

std::shared_ptr<Registry> Registry::read(
    const Path & path, RegistryType type)
{
    auto registry = std::make_shared<Registry>(type);

    if (!pathExists(path)) {
        if (type == RegistryType::Global) {
            printTaggedWarning("cannot read flake registry '%s': path does not exist", path);
        }
        return std::make_shared<Registry>(type);
    }

    try {

        auto json = json::parse(readFile(path));

        auto version = json.value("version", 0);

        if (version == 2) {
            for (auto & i : json["flakes"]) {
                auto toAttrs = jsonToAttrs(i["to"]);
                Attrs extraAttrs;
                auto j = toAttrs.find("dir");
                if (j != toAttrs.end()) {
                    extraAttrs.insert(*j);
                    toAttrs.erase(j);
                }
                auto exact = i.find("exact");
                registry->entries.push_back(
                    Entry {
                        .from = Input::fromAttrs(jsonToAttrs(i["from"])),
                        .to = Input::fromAttrs(std::move(toAttrs)),
                        .extraAttrs = extraAttrs,
                        .exact = exact != i.end() && exact.value()
                    });
            }
        }

        else
            throw Error("flake registry '%s' has unsupported version %d", path, version);

    } catch (Error & e) {
        printTaggedWarning("cannot read flake registry '%s': %s", path, e.what());
    }

    return registry;
}

void Registry::write(const Path & path)
{
    JSON arr;
    for (auto & entry : entries) {
        JSON obj;
        obj["from"] = attrsToJSON(entry.from.toAttrs());
        obj["to"] = attrsToJSON(entry.to.toAttrs());
        if (!entry.extraAttrs.empty())
            obj["to"].update(attrsToJSON(entry.extraAttrs));
        if (entry.exact)
            obj["exact"] = true;
        arr.emplace_back(std::move(obj));
    }

    JSON json;
    json["version"] = 2;
    json["flakes"] = std::move(arr);

    createDirs(dirOf(path));
    writeFile(path, json.dump(2));
}

void Registry::add(
    const Input & from,
    const Input & to,
    const Attrs & extraAttrs)
{
    entries.emplace_back(
        Entry {
            .from = from,
            .to = to,
            .extraAttrs = extraAttrs
        });
}

void Registry::remove(const Input & input)
{
    // FIXME: use C++20 std::erase.
    for (auto i = entries.begin(); i != entries.end(); )
        if (i->from == input)
            i = entries.erase(i);
        else
            ++i;
}

static Path getSystemRegistryPath()
{
    return settings.nixConfDir + "/registry.json";
}

static std::shared_ptr<Registry> getSystemRegistry()
{
    static auto systemRegistry =
        Registry::read(getSystemRegistryPath(), Registry::System);
    return systemRegistry;
}

Path getUserRegistryPath()
{
    return getConfigDir() + "/nix/registry.json";
}

std::shared_ptr<Registry> getUserRegistry()
{
    static auto userRegistry =
        Registry::read(getUserRegistryPath(), Registry::User);
    return userRegistry;
}

std::shared_ptr<Registry> getCustomRegistry(const Path & p)
{
    static auto customRegistry =
        Registry::read(p, Registry::Custom);
    return customRegistry;
}

static std::shared_ptr<Registry> flagRegistry =
    std::make_shared<Registry>(Registry::Flag);

std::shared_ptr<Registry> getFlagRegistry()
{
    return flagRegistry;
}

void overrideRegistry(
    const Input & from,
    const Input & to,
    const Attrs & extraAttrs)
{
    flagRegistry->add(from, to, extraAttrs);
}

static kj::Promise<Result<std::shared_ptr<Registry>>> getGlobalRegistry(ref<Store> store)
try {
    static Sync<std::shared_ptr<Registry>, AsyncMutex> reg;

    auto lk = co_await reg.lock();

    if (!*lk) {
        auto path = fetchSettings.flakeRegistry.get();
        if (path == "") {
            *lk = std::make_shared<Registry>(Registry::Global); // empty registry
        } else if (path == "vendored") {
            *lk = Registry::read(settings.nixDataDir + "/flake-registry.json", Registry::Global);
        } else {
            if (!path.starts_with("/")) {
                printTaggedWarning(
                    "config option flake-registry referring to a URL is deprecated and will be "
                    "removed in Lix 3.0; yours is: `%s'",
                    path
                );

                auto storePath =
                    TRY_AWAIT(downloadFile(store, path, "flake-registry.json", false)).storePath;
                if (auto store2 = store.try_cast_shared<LocalFSStore>()) {
                    TRY_AWAIT(
                        store2->addPermRoot(storePath, getCacheDir() + "/nix/flake-registry.json")
                    );
                }
                path = store->toRealPath(storePath);
            }

            *lk = Registry::read(path, Registry::Global);
        }
    };

    co_return *lk;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<Registries>> getRegistries(ref<Store> store)
try {
    Registries registries;
    registries.push_back(getFlagRegistry());
    registries.push_back(getUserRegistry());
    registries.push_back(getSystemRegistry());
    registries.push_back(TRY_AWAIT(getGlobalRegistry(store)));
    co_return registries;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::pair<Input, Attrs>>> lookupInRegistries(
    ref<Store> store,
    const Input & _input)
try {
    Attrs extraAttrs;
    int n = 0;
    Input input(_input);

 restart:

    n++;
    if (n > 100) throw Error("cycle detected in flake registry for '%s'", input.to_string());

    for (auto & registry : TRY_AWAIT(getRegistries(store))) {
        // FIXME: O(n)
        for (auto & entry : registry->entries) {
            if (entry.exact) {
                if (entry.from == input) {
                    input = entry.to;
                    extraAttrs = entry.extraAttrs;
                    goto restart;
                }
            } else {
                if (entry.from.contains(input)) {
                    input = entry.to.applyOverrides(
                        !entry.from.getRef() && input.getRef() ? input.getRef() : std::optional<std::string>(),
                        !entry.from.getRev() && input.getRev() ? input.getRev() : std::optional<Hash>());
                    extraAttrs = entry.extraAttrs;
                    goto restart;
                }
            }
        }
    }

    if (!input.isDirect())
        throw Error("cannot find flake '%s' in the flake registries", input.to_string());

    debug("looked up '%s' -> '%s'", _input.to_string(), input.to_string());

    co_return {input, extraAttrs};
} catch (...) {
    co_return result::current_exception();
}

}
