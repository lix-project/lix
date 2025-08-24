#include "lix/libfetchers/cache.hh"
#include "lix/libstore/sqlite.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/sync.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/users.hh"

#include <optional>

namespace nix::fetchers {

static const char * schema = R"sql(

create table if not exists Cache (
    input     text not null,
    info      text not null,
    path      text not null,
    immutable integer not null,
    timestamp integer not null,
    primary key (input)
);
)sql";

struct CacheImpl : Cache
{
    struct State
    {
        SQLite db;
        SQLiteStmt add, lookup;
    };

    Sync<State> _state;

    CacheImpl()
    {
        auto state(_state.lock());

        auto dbPath = getCacheDir() + "/nix/fetcher-cache-v1.sqlite";
        // It would be silly to fail fetcher operations if e.g. the user has no
        // XDG_CACHE_HOME and their HOME directory doesn't exist.
        // We'll warn the user if that happens, but fallback to an in-memory
        // backend for the SQLite database.
        try {
            createDirs(dirOf(dbPath));
        } catch (SysError const & ex) {
            printTaggedWarning("ignoring error initializing Lix fetcher cache: %s", ex.what());
            dbPath = ":memory:";
        }

        state->db = SQLite(dbPath);
        state->db.isCache();
        state->db.exec(schema, always_progresses);

        state->add = state->db.create(
            "insert or replace into Cache(input, info, path, immutable, timestamp) values (?, ?, ?, ?, ?)");

        state->lookup = state->db.create(
            "select info, path, immutable, timestamp from Cache where input = ?");
    }

    void add(
        ref<Store> store,
        const Attrs & inAttrs,
        const Attrs & infoAttrs,
        const StorePath & storePath,
        bool locked) override
    {
        _state.lock()->add.use()
            (attrsToJSON(inAttrs).dump())
            (attrsToJSON(infoAttrs).dump())
            (store->printStorePath(storePath))
            (locked)
            (time(0)).exec();
    }

    kj::Promise<Result<std::optional<std::pair<Attrs, StorePath>>>> lookup(
        ref<Store> store,
        const Attrs & inAttrs) override
    try {
        if (auto res = TRY_AWAIT(lookupExpired(store, inAttrs))) {
            if (!res->expired)
                co_return std::make_pair(std::move(res->infoAttrs), std::move(res->storePath));
            debug("ignoring expired cache entry '%s'",
                attrsToJSON(inAttrs).dump());
        }
        co_return std::nullopt;
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<std::optional<LookupResult>>> lookupExpired(
        ref<Store> store,
        const Attrs & inAttrs) override
    try {
        auto state(_state.lock());

        auto inAttrsJSON = attrsToJSON(inAttrs).dump();

        auto stmt(state->lookup.use()(inAttrsJSON));
        if (!stmt.next()) {
            debug("did not find cache entry for '%s'", inAttrsJSON);
            co_return std::nullopt;
        }

        auto infoJSON = stmt.getStr(0);
        auto storePath = store->parseStorePath(stmt.getStr(1));
        auto locked = stmt.getInt(2) != 0;
        auto timestamp = stmt.getInt(3);

        TRY_AWAIT(store->addTempRoot(storePath));
        if (!TRY_AWAIT(store->isValidPath(storePath))) {
            // FIXME: we could try to substitute 'storePath'.
            debug("ignoring disappeared cache entry '%s'", inAttrsJSON);
            co_return std::nullopt;
        }

        debug("using cache entry '%s' -> '%s', '%s'",
            inAttrsJSON, infoJSON, store->printStorePath(storePath));

        co_return LookupResult {
            .expired = !locked && (settings.tarballTtl.get() == 0 || timestamp + settings.tarballTtl < time(0)),
            .infoAttrs = jsonToAttrs(json::parse(infoJSON, "a fetcher cache entry")),
            .storePath = std::move(storePath)
        };
    } catch (...) {
        co_return result::current_exception();
    }
};

ref<Cache> getCache()
{
    static auto cache = make_ref<CacheImpl>();
    return cache;
}

}
