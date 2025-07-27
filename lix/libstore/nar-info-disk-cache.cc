#include "lix/libstore/nar-info-disk-cache.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/sync.hh"
#include "lix/libstore/sqlite.hh"
#include "lix/libstore/globals.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/users.hh"
#include "lix/libutil/strings.hh"

#include <sqlite3.h>

namespace nix {

static const char * schema = R"sql(

create table if not exists BinaryCaches (
    id        integer primary key autoincrement not null,
    url       text unique not null,
    timestamp integer not null,
    storeDir  text not null,
    wantMassQuery integer not null,
    priority  integer not null
);

create table if not exists NARs (
    cache            integer not null,
    hashPart         text not null,
    namePart         text,
    url              text,
    compression      text,
    fileHash         text,
    fileSize         integer,
    narHash          text,
    narSize          integer,
    refs             text,
    deriver          text,
    sigs             text,
    ca               text,
    timestamp        integer not null,
    present          integer not null,
    primary key (cache, hashPart),
    foreign key (cache) references BinaryCaches(id) on delete cascade
);

create table if not exists LastPurge (
    dummy            text primary key,
    value            integer
);

)sql";

class NarInfoDiskCacheImpl : public NarInfoDiskCache
{
public:

    /* How often to purge expired entries from the cache. */
    const int purgeInterval = 24 * 3600;

    /* How long to cache binary cache info (i.e. /nix-cache-info) */
    const int cacheInfoTtl = 7 * 24 * 3600;

    struct Cache
    {
        int id;
        Path storeDir;
        bool wantMassQuery;
        int priority;
    };

    struct State
    {
        SQLite db;
        SQLiteStmt insertCache, queryCache, insertNAR, insertMissingNAR, queryNAR, purgeCache,
            removeNegativeCacheEntry;
        std::map<std::string, Cache> caches;
    };

    Sync<State> _state;

    NarInfoDiskCacheImpl(Path dbPath = getCacheDir() + "/nix/binary-cache-v6.sqlite")
    {
        auto state(_state.lock());

        createDirs(dirOf(dbPath));

        state->db = SQLite(dbPath);

        state->db.isCache();

        state->db.exec(schema, always_progresses);

        state->insertCache = state->db.create(
            "insert into BinaryCaches(url, timestamp, storeDir, wantMassQuery, priority) values (?1, ?2, ?3, ?4, ?5) on conflict (url) do update set timestamp = ?2, storeDir = ?3, wantMassQuery = ?4, priority = ?5 returning id;");

        state->queryCache = state->db.create(
            "select id, storeDir, wantMassQuery, priority from BinaryCaches where url = ? and timestamp > ?");

        state->insertNAR = state->db.create(
            "insert or replace into NARs(cache, hashPart, namePart, url, compression, fileHash, fileSize, narHash, "
            "narSize, refs, deriver, sigs, ca, timestamp, present) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1)");

        state->insertMissingNAR = state->db.create(
            "insert or replace into NARs(cache, hashPart, timestamp, present) values (?, ?, ?, 0)");

        state->queryNAR = state->db.create(
            "select present, namePart, url, compression, fileHash, fileSize, narHash, narSize, refs, deriver, sigs, ca from NARs where cache = ? and hashPart = ? and ((present = 0 and timestamp > ?) or (present = 1 and timestamp > ?))");

        state->removeNegativeCacheEntry =
            state->db.create("delete from NARs where present = 0 and hashPart = ? and cache = ?");

        /* Periodically purge expired entries from the database. */
        retrySQLite([&]() {
            auto now = time(0);

            SQLiteStmt queryLastPurge = state->db.create("select value from LastPurge");
            auto queryLastPurge_(queryLastPurge.use());

            if (!queryLastPurge_.next() || queryLastPurge_.getInt(0) < now - purgeInterval) {
                state->db.create(
                    "delete from NARs where ((present = 0 and timestamp < ?) or (present = 1 and timestamp < ?))")
                    .use()
                    // Use a minimum TTL to prevent --refresh from
                    // nuking the entire disk cache.
                    (now - std::max(settings.ttlNegativeNarInfoCache.get(), 3600U))
                    (now - std::max(settings.ttlPositiveNarInfoCache.get(), 30 * 24 * 3600U))
                    .exec();

                debug("deleted %d entries from the NAR info disk cache", state->db.getRowsChanged());

                state->db.create(
                    "insert or replace into LastPurge(dummy, value) values ('', ?)")
                    .use()(now).exec();
            }
        }, always_progresses);
    }

    Cache & getCache(State & state, const std::string & uri)
    {
        auto i = state.caches.find(uri);
        if (i == state.caches.end()) abort();
        return i->second;
    }

private:

    std::optional<Cache> queryCacheRaw(State & state, const std::string & uri)
    {
        auto i = state.caches.find(uri);
        if (i == state.caches.end()) {
            auto queryCache(state.queryCache.use()(uri)(time(0) - cacheInfoTtl));
            if (!queryCache.next())
                return std::nullopt;
            auto cache = Cache {
                .id = (int) queryCache.getInt(0),
                .storeDir = queryCache.getStr(1),
                .wantMassQuery = queryCache.getInt(2) != 0,
                .priority = (int) queryCache.getInt(3),
            };
            state.caches.emplace(uri, cache);
        }
        return getCache(state, uri);
    }

public:
    int createCache(const std::string & uri, const Path & storeDir, bool wantMassQuery, int priority) override
    {
        return retrySQLite([&]() {
            auto state(_state.lock());
            SQLiteTxn txn = state->db.beginTransaction();

            // To avoid the race, we have to check if maybe someone hasn't yet created
            // the cache for this URI in the meantime.
            auto cache(queryCacheRaw(*state, uri));

            if (cache)
                return cache->id;

            Cache ret {
                .id = -1, // set below
                .storeDir = storeDir,
                .wantMassQuery = wantMassQuery,
                .priority = priority,
            };

            {
                auto r(state->insertCache.use()(uri)(time(0))(storeDir)(wantMassQuery)(priority));
                assert(r.next());
                ret.id = (int) r.getInt(0);
            }

            state->caches[uri] = ret;

            txn.commit();
            return ret.id;
        }, always_progresses);
    }

    std::optional<CacheInfo> upToDateCacheExists(const std::string & uri) override
    {
        return retrySQLite([&]() -> std::optional<CacheInfo> {
            auto state(_state.lock());
            auto cache(queryCacheRaw(*state, uri));
            if (!cache)
                return std::nullopt;
            return CacheInfo {
                .id = cache->id,
                .wantMassQuery = cache->wantMassQuery,
                .priority = cache->priority
            };
        }, always_progresses);
    }

    std::pair<Outcome, std::shared_ptr<NarInfo>> lookupNarInfo(
        const std::string & uri, const std::string & hashPart) override
    {
        return retrySQLite([&]() -> std::pair<Outcome, std::shared_ptr<NarInfo>> {
            auto state(_state.lock());

            auto & cache(getCache(*state, uri));

            auto now = time(0);

            auto queryNAR(state->queryNAR.use()
                (cache.id)
                (hashPart)
                (now - settings.ttlNegativeNarInfoCache)
                (now - settings.ttlPositiveNarInfoCache));

            if (!queryNAR.next())
                return {oUnknown, 0};

            if (!queryNAR.getInt(0))
                return {oInvalid, 0};

            auto namePart = queryNAR.getStr(1);
            auto narInfo = make_ref<NarInfo>(
                StorePath(hashPart + "-" + namePart),
                Hash::parseAnyPrefixed(queryNAR.getStr(6)));
            narInfo->url = queryNAR.getStr(2);
            narInfo->compression = queryNAR.getStr(3);
            if (!queryNAR.isNull(4))
                narInfo->fileHash = Hash::parseAnyPrefixed(queryNAR.getStr(4));
            narInfo->fileSize = queryNAR.getInt(5);
            narInfo->narSize = queryNAR.getInt(7);
            for (auto & r : tokenizeString<Strings>(queryNAR.getStr(8), " "))
                narInfo->references.insert(StorePath(r));
            if (!queryNAR.isNull(9))
                narInfo->deriver = StorePath(queryNAR.getStr(9));
            for (auto & sig : tokenizeString<Strings>(queryNAR.getStr(10), " "))
                narInfo->sigs.insert(sig);
            narInfo->ca = ContentAddress::parseOpt(queryNAR.getStr(11));

            return {oValid, narInfo};
        }, always_progresses);
    }

    void removeNegativeCacheEntry(const std::string & uri, const std::string & hashPart) override
    {
        retrySQLite(
            [&]() {
                auto state(_state.lock());
                auto & cache(getCache(*state, uri));
                state->removeNegativeCacheEntry.use()(hashPart)(cache.id).exec();
            },
            always_progresses
        );
    }

    void upsertNarInfo(
        const std::string & uri, const std::string & hashPart,
        std::shared_ptr<const ValidPathInfo> info) override
    {
        retrySQLite([&]() {
            auto state(_state.lock());

            auto & cache(getCache(*state, uri));

            if (info) {

                auto narInfo = std::dynamic_pointer_cast<const NarInfo>(info);

                //assert(hashPart == storePathToHash(info->path));

                state->insertNAR.use()
                    (cache.id)
                    (hashPart)
                    (std::string(info->path.name()))
                    (narInfo ? narInfo->url : "", narInfo != 0)
                    (narInfo ? narInfo->compression : "", narInfo != 0)
                    (narInfo && narInfo->fileHash ? narInfo->fileHash->to_string(Base::Base32, true) : "", narInfo && narInfo->fileHash)
                    (narInfo ? narInfo->fileSize : 0, narInfo != 0 && narInfo->fileSize)
                    (info->narHash.to_string(Base::Base32, true))
                    (info->narSize)
                    (concatStringsSep(" ", info->shortRefs()))
                    (info->deriver ? std::string(info->deriver->to_string()) : "", (bool) info->deriver)
                    (concatStringsSep(" ", info->sigs))
                    (renderContentAddress(info->ca))
                    (time(0)).exec();

            } else {
                state->insertMissingNAR.use()
                    (cache.id)
                    (hashPart)
                    (time(0)).exec();
            }
        }, always_progresses);
    }
};

ref<NarInfoDiskCache> getNarInfoDiskCache()
{
    static ref<NarInfoDiskCache> cache = make_ref<NarInfoDiskCacheImpl>();
    return cache;
}

ref<NarInfoDiskCache> getTestNarInfoDiskCache(Path dbPath)
{
    return make_ref<NarInfoDiskCacheImpl>(dbPath);
}

}
