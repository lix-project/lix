#include "lix/libstore/local-store.hh"
#include "lix/libstore/globals.hh"
#include "lix/libutil/archive.hh"
#include "lix/libstore/pathlocks.hh"
#include "lix/libstore/temporary-dir.hh"
#include "lix/libstore/worker-protocol.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/nar-info.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/references.hh"
#include "lix/libutil/topo-sort.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/compression.hh"
#include "lix/libutil/strings.hh"

#include <algorithm>
#include <cstring>

#include <memory>
#include <mutex>
#include <new>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <grp.h>

#if __linux__
#include <sched.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/xattr.h>
#endif

#include <sqlite3.h>


namespace nix {

std::string LocalStoreConfig::doc()
{
    return
        #include "local-store.md"
        ;
}

struct LocalStore::DBState::Stmts {
    /* Some precompiled SQLite statements. */
    SQLiteStmt RegisterValidPath;
    SQLiteStmt UpdatePathInfo;
    SQLiteStmt AddReference;
    SQLiteStmt QueryPathInfo;
    SQLiteStmt QueryReferences;
    SQLiteStmt QueryReferrers;
    SQLiteStmt InvalidatePath;
    SQLiteStmt AddDerivationOutput;
    SQLiteStmt RegisterRealisedOutput;
    SQLiteStmt UpdateRealisedOutput;
    SQLiteStmt QueryValidDerivers;
    SQLiteStmt QueryDerivationOutputs;
    SQLiteStmt QueryRealisedOutput;
    SQLiteStmt QueryAllRealisedOutputs;
    SQLiteStmt QueryPathFromHashPart;
    SQLiteStmt QueryValidPaths;
    SQLiteStmt QueryRealisationReferences;
    SQLiteStmt AddRealisationReference;
};

int getSchema(Path schemaPath)
{
    int curSchema = 0;
    if (pathExists(schemaPath)) {
        auto s = readFile(schemaPath);
        auto n = string2Int<int>(s);
        if (!n)
            throw Error("'%1%' is corrupt", schemaPath);
        curSchema = *n;
    }
    return curSchema;
}

void migrateCASchema(SQLite& db, Path schemaPath, AutoCloseFD& lockFd)
{
    const int nixCASchemaVersion = 4;
    int curCASchema = getSchema(schemaPath);
    if (curCASchema != nixCASchemaVersion) {
        if (curCASchema > nixCASchemaVersion) {
            throw Error("current Nix store ca-schema is version %1%, but I only support %2%",
                 curCASchema, nixCASchemaVersion);
        }

        if (!lockFile(lockFd.get(), ltWrite, false)) {
            printInfo("waiting for exclusive access to the Nix store for ca drvs...");
            unlockFile(lockFd.get()); // We have acquired a shared lock; release it to prevent deadlocks
            lockFile(lockFd.get(), ltWrite, true);
        }

        if (curCASchema == 0) {
            static const char schema[] =
              #include "ca-specific-schema.sql.gen.hh"
                ;
            db.exec(schema);
            curCASchema = nixCASchemaVersion;
        }

        if (curCASchema < 2) {
            SQLiteTxn txn = db.beginTransaction();
            // Ugly little sql dance to add a new `id` column and make it the primary key
            db.exec(R"(
                create table Realisations2 (
                    id integer primary key autoincrement not null,
                    drvPath text not null,
                    outputName text not null, -- symbolic output id, usually "out"
                    outputPath integer not null,
                    signatures text, -- space-separated list
                    foreign key (outputPath) references ValidPaths(id) on delete cascade
                );
                insert into Realisations2 (drvPath, outputName, outputPath, signatures)
                    select drvPath, outputName, outputPath, signatures from Realisations;
                drop table Realisations;
                alter table Realisations2 rename to Realisations;
            )");
            db.exec(R"(
                create index if not exists IndexRealisations on Realisations(drvPath, outputName);

                create table if not exists RealisationsRefs (
                    referrer integer not null,
                    realisationReference integer,
                    foreign key (referrer) references Realisations(id) on delete cascade,
                    foreign key (realisationReference) references Realisations(id) on delete restrict
                );
            )");
            txn.commit();
        }

        if (curCASchema < 3) {
            SQLiteTxn txn = db.beginTransaction();
            // Apply new indices added in this schema update.
            db.exec(R"(
                -- used by QueryRealisationReferences
                create index if not exists IndexRealisationsRefs on RealisationsRefs(referrer);
                -- used by cascade deletion when ValidPaths is deleted
                create index if not exists IndexRealisationsRefsOnOutputPath on Realisations(outputPath);
            )");
            txn.commit();
        }
        if (curCASchema < 4) {
            SQLiteTxn txn = db.beginTransaction();
            db.exec(R"(
                create trigger if not exists DeleteSelfRefsViaRealisations before delete on ValidPaths
                begin
                    delete from RealisationsRefs where realisationReference in (
                    select id from Realisations where outputPath = old.id
                    );
                end;
                -- used by deletion trigger
                create index if not exists IndexRealisationsRefsRealisationReference on RealisationsRefs(realisationReference);
            )");
            txn.commit();
        }

        writeFile(schemaPath, fmt("%d", nixCASchemaVersion), 0666, true);
        lockFile(lockFd.get(), ltRead, true);
    }
}

LocalStore::LocalStore(LocalStoreConfig config)
    : Store(config)
    , config_(std::move(config))
    , dbDir(config_.stateDir + "/db")
    , linksDir(config_.realStoreDir + "/.links")
    , reservedSpacePath(dbDir + "/reserved")
    , schemaPath(dbDir + "/schema")
    , tempRootsDir(config_.stateDir + "/temproots")
    , fnTempRoots(fmt("%s/%d", tempRootsDir, getpid()))
    , locksHeld(tokenizeString<PathSet>(getEnv("NIX_HELD_LOCKS").value_or("")))
{
    auto state(_dbState.lock());
    state->stmts = std::make_unique<DBState::Stmts>();

    /* Create missing state directories if they don't already exist. */
    createDirs(config_.realStoreDir);
    if (config_.readOnly) {
        experimentalFeatureSettings.require(Xp::ReadOnlyLocalStore);
    } else {
        makeStoreWritable();
    }
    createDirs(linksDir);
    Path profilesDir = config_.stateDir + "/profiles";
    createDirs(profilesDir);
    createDirs(tempRootsDir);
    createDirs(dbDir);
    Path gcRootsDir = config_.stateDir + "/gcroots";
    if (!pathExists(gcRootsDir)) {
        createDirs(gcRootsDir);
        createSymlink(profilesDir, gcRootsDir + "/profiles");
    }

    for (auto & perUserDir : {profilesDir + "/per-user", gcRootsDir + "/per-user"}) {
        createDirs(perUserDir);
        if (!config_.readOnly) {
            if (chmod(perUserDir.c_str(), 0755) == -1)
                throw SysError("could not set permissions on '%s' to 755", perUserDir);
        }
    }

    /* Optionally, create directories and set permissions for a
       multi-user install. */
    if (getuid() == 0 && settings.buildUsersGroup != "") {
        mode_t perm = 01775;

        struct group * gr = getgrnam(settings.buildUsersGroup.get().c_str());
        if (!gr)
            printError("warning: the group '%1%' specified in 'build-users-group' does not exist", settings.buildUsersGroup);
        else {
            struct stat st;
            if (stat(config_.realStoreDir.get().c_str(), &st))
                throw SysError("getting attributes of path '%1%'", config_.realStoreDir);

            if (st.st_uid != 0 || st.st_gid != gr->gr_gid || (st.st_mode & ~S_IFMT) != perm) {
                if (chown(config_.realStoreDir.get().c_str(), 0, gr->gr_gid) == -1)
                    throw SysError("changing ownership of path '%1%'", config_.realStoreDir);
                if (chmod(config_.realStoreDir.get().c_str(), perm) == -1)
                    throw SysError("changing permissions on path '%1%'", config_.realStoreDir);
            }
        }
    }

    /* Ensure that the store and its parents are not symlinks. */
    if (!settings.allowSymlinkedStore) {
        Path path = config_.realStoreDir;
        struct stat st;
        while (path != "/") {
            st = lstat(path);
            if (S_ISLNK(st.st_mode))
                throw Error(
                        "the path '%1%' is a symlink; "
                        "this is not allowed for the Nix store and its parent directories",
                        path);
            path = dirOf(path);
        }
    }

    /* We can't open a SQLite database if the disk is full.  Since
       this prevents the garbage collector from running when it's most
       needed, we reserve some dummy space that we can free just
       before doing a garbage collection. */
    try {
        struct stat st;
        if (stat(reservedSpacePath.c_str(), &st) == -1 ||
            st.st_size != settings.reservedSize)
        {
            AutoCloseFD fd{open(reservedSpacePath.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0600)};
            int res = -1;
#if HAVE_POSIX_FALLOCATE
            res = posix_fallocate(fd.get(), 0, settings.reservedSize);
#endif
            if (res == -1) {
                writeFull(fd.get(), std::string(settings.reservedSize, 'X'));
                [[gnu::unused]] auto res2 = ftruncate(fd.get(), settings.reservedSize);
            }
        }
    } catch (SysError & e) { /* don't care about errors */
    }

    /* Acquire the big fat lock in shared mode to make sure that no
       schema upgrade is in progress. */
    if (!config_.readOnly) {
        Path globalLockPath = dbDir + "/big-lock";
        globalLock = openLockFile(globalLockPath.c_str(), true);
    }

    if (!config_.readOnly && !lockFile(globalLock.get(), ltRead, false)) {
        printInfo("waiting for the big Nix store lock...");
        lockFile(globalLock.get(), ltRead, true);
    }

    /* Check the current database schema and if necessary do an
       upgrade.  */
    int curSchema = getSchema();
    if (config_.readOnly && curSchema < nixSchemaVersion) {
        debug("current schema version: %d", curSchema);
        debug("supported schema version: %d", nixSchemaVersion);
        throw Error(curSchema == 0 ?
            "database does not exist, and cannot be created in read-only mode" :
            "database schema needs migrating, but this cannot be done in read-only mode");
    }

    if (curSchema > nixSchemaVersion)
        throw Error("current Nix store schema is version %1%, but I only support %2%",
             curSchema, nixSchemaVersion);

    else if (curSchema == 0) { /* new store */
        curSchema = nixSchemaVersion;
        openDB(*state, true);
        writeFile(schemaPath, fmt("%1%", nixSchemaVersion), 0666, true);
    }

    else if (curSchema < nixSchemaVersion) {
        if (curSchema < 5)
            throw Error(
                "Your Nix store has a database in Berkeley DB format,\n"
                "which is no longer supported. To convert to the new format,\n"
                "please use the original Nix version 0.12 first.");

        if (curSchema < 6)
            throw Error(
                "Your Nix store has a database in flat file format,\n"
                "which is no longer supported. To convert to the new format,\n"
                "please use the original Nix version 1.11 first.");

        if (!lockFile(globalLock.get(), ltWrite, false)) {
            printInfo("waiting for exclusive access to the Nix store...");
            unlockFile(globalLock.get()); // We have acquired a shared lock; release it to prevent deadlocks
            lockFile(globalLock.get(), ltWrite, true);
        }

        /* Get the schema version again, because another process may
           have performed the upgrade already. */
        curSchema = getSchema();

        if (curSchema < 7) { upgradeStore7(); }

        openDB(*state, false);

        if (curSchema < 8) {
            SQLiteTxn txn = state->db.beginTransaction();
            state->db.exec("alter table ValidPaths add column ultimate integer");
            state->db.exec("alter table ValidPaths add column sigs text");
            txn.commit();
        }

        if (curSchema < 9) {
            SQLiteTxn txn = state->db.beginTransaction();
            state->db.exec("drop table FailedPaths");
            txn.commit();
        }

        if (curSchema < 10) {
            SQLiteTxn txn = state->db.beginTransaction();
            state->db.exec("alter table ValidPaths add column ca text");
            txn.commit();
        }

        writeFile(schemaPath, fmt("%1%", nixSchemaVersion), 0666, true);

        lockFile(globalLock.get(), ltRead, true);
    }

    else openDB(*state, false);

    if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
        if (!config_.readOnly) {
            migrateCASchema(state->db, dbDir + "/ca-schema", globalLock);
        } else {
            throw Error("need to migrate to content-addressed schema, but this cannot be done in read-only mode");
        }
    }

    /* Prepare SQL statements. */
    state->stmts->RegisterValidPath = state->db.create(
        "insert into ValidPaths (path, hash, registrationTime, deriver, narSize, ultimate, sigs, ca) values (?, ?, ?, ?, ?, ?, ?, ?);");
    state->stmts->UpdatePathInfo = state->db.create(
        "update ValidPaths set narSize = ?, hash = ?, ultimate = ?, sigs = ?, ca = ? where path = ?;");
    state->stmts->AddReference = state->db.create(
        "insert or replace into Refs (referrer, reference) values (?, ?);");
    state->stmts->QueryPathInfo = state->db.create(
        "select id, hash, registrationTime, deriver, narSize, ultimate, sigs, ca from ValidPaths where path = ?;");
    state->stmts->QueryReferences = state->db.create(
        "select path from Refs join ValidPaths on reference = id where referrer = ?;");
    state->stmts->QueryReferrers = state->db.create(
        "select path from Refs join ValidPaths on referrer = id where reference = (select id from ValidPaths where path = ?);");
    state->stmts->InvalidatePath = state->db.create(
        "delete from ValidPaths where path = ?;");
    state->stmts->AddDerivationOutput = state->db.create(
        "insert or replace into DerivationOutputs (drv, id, path) values (?, ?, ?);");
    state->stmts->QueryValidDerivers = state->db.create(
        "select v.id, v.path from DerivationOutputs d join ValidPaths v on d.drv = v.id where d.path = ?;");
    state->stmts->QueryDerivationOutputs = state->db.create(
        "select id, path from DerivationOutputs where drv = ?;");
    // Use "path >= ?" with limit 1 rather than "path like '?%'" to
    // ensure efficient lookup.
    state->stmts->QueryPathFromHashPart = state->db.create(
        "select path from ValidPaths where path >= ? limit 1;");
    state->stmts->QueryValidPaths = state->db.create("select path from ValidPaths");
    if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
        state->stmts->RegisterRealisedOutput = state->db.create(
            R"(
                insert into Realisations (drvPath, outputName, outputPath, signatures)
                values (?, ?, (select id from ValidPaths where path = ?), ?)
                ;
            )");
        state->stmts->UpdateRealisedOutput = state->db.create(
            R"(
                update Realisations
                    set signatures = ?
                where
                    drvPath = ? and
                    outputName = ?
                ;
            )");
        state->stmts->QueryRealisedOutput = state->db.create(
            R"(
                select Realisations.id, Output.path, Realisations.signatures from Realisations
                    inner join ValidPaths as Output on Output.id = Realisations.outputPath
                    where drvPath = ? and outputName = ?
                    ;
            )");
        state->stmts->QueryAllRealisedOutputs = state->db.create(
            R"(
                select outputName, Output.path from Realisations
                    inner join ValidPaths as Output on Output.id = Realisations.outputPath
                    where drvPath = ?
                    ;
            )");
        state->stmts->QueryRealisationReferences = state->db.create(
            R"(
                select drvPath, outputName from Realisations
                    join RealisationsRefs on realisationReference = Realisations.id
                    where referrer = ?;
            )");
        state->stmts->AddRealisationReference = state->db.create(
            R"(
                insert or replace into RealisationsRefs (referrer, realisationReference)
                values (
                    (select id from Realisations where drvPath = ? and outputName = ?),
                    (select id from Realisations where drvPath = ? and outputName = ?));
            )");
    }
}


LocalStore::LocalStore(std::string scheme, std::string path, LocalStoreConfig config)
    : LocalStore(std::move(config))
{
    throw UnimplementedError("LocalStore");
}


AutoCloseFD LocalStore::openGCLock()
{
    Path fnGCLock = config_.stateDir + "/gc.lock";
    AutoCloseFD fdGCLock{open(fnGCLock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600)};
    if (!fdGCLock)
        throw SysError("opening global GC lock '%1%'", fnGCLock);
    return fdGCLock;
}


LocalStore::~LocalStore()
{
    std::shared_future<void> future;

    {
        auto state(_gcState.lock());
        if (state->gcRunning)
            future = state->gcFuture;
    }

    if (future.valid()) {
        printInfo("waiting for auto-GC to finish on exit...");
        future.get();
    }

    try {
        auto fdTempRoots(_fdTempRoots.lock());
        if (*fdTempRoots) {
            fdTempRoots->reset();
            unlink(fnTempRoots.c_str());
        }
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}


std::string LocalStore::getUri()
{
    return "local";
}


int LocalStore::getSchema()
{ return nix::getSchema(schemaPath); }

void LocalStore::openDB(DBState & state, bool create)
{
    if (create && config_.readOnly) {
        throw Error("cannot create database while in read-only mode");
    }

    if (access(dbDir.c_str(), R_OK | (config_.readOnly ? 0 : W_OK)))
        throw SysError("Nix database directory '%1%' is not writable", dbDir);

    /* Open the Nix database. */
    std::string dbPath = dbDir + "/db.sqlite";
    auto & db(state.db);
    auto openMode = config_.readOnly ? SQLiteOpenMode::Immutable
                  : create ? SQLiteOpenMode::Normal
                  : SQLiteOpenMode::NoCreate;
    state.db = SQLite(dbPath, openMode);

    /* !!! check whether sqlite has been built with foreign key
       support */

    /* Whether SQLite should fsync().  "Normal" synchronous mode
       should be safe enough.  If the user asks for it, don't sync at
       all.  This can cause database corruption if the system
       crashes. */
    std::string syncMode = settings.fsyncMetadata ? "normal" : "off";
    db.exec("pragma synchronous = " + syncMode);

    /* Set the SQLite journal mode.  WAL mode is fastest, so it's the
       default. */
    std::string mode = settings.useSQLiteWAL ? "wal" : "truncate";
    std::string prevMode;
    {
        SQLiteStmt stmt = db.create("pragma main.journal_mode;");
        auto use = stmt.use();
        assert(use.next());
        prevMode = use.getStr(0);
    }
    if (prevMode != mode)
        db.exec("pragma main.journal_mode = " + mode + ";");

    if (mode == "wal" ) {
         /* persist the WAL files when the DB connection is closed.
         * This allows for read-only connections without any write permissions
         * on the state directory to succeed on a closed database. Setting the
         * journal_size_limit to 2^40 bytes results in the WAL files getting
         * truncated to 0 on exit and limits the on disk size of the WAL files
         * to 2^40 bytes following a checkpoint */
        db.exec("pragma main.journal_size_limit = 1099511627776;");
        db.setPersistWAL(true);

        /* Increase the auto-checkpoint interval to 40000 pages.  This
           seems enough to ensure that instantiating the NixOS system
           derivation is done in a single fsync(). */
        db.exec("pragma wal_autocheckpoint = 40000;");
    }

    /* Initialise the database schema, if necessary. */
    if (create) {
        static const char schema[] =
#include "schema.sql.gen.hh"
            ;
        db.exec(schema);
    }
}


/* To improve purity, users may want to make the Nix store a read-only
   bind mount.  So make the Nix store writable for this process. */
void LocalStore::makeStoreWritable()
{
#if __linux__
    if (getuid() != 0) return;
    /* Check if /nix/store is on a read-only mount. */
    struct statvfs stat;
    if (statvfs(config_.realStoreDir.get().c_str(), &stat) != 0)
        throw SysError("getting info about the Nix store mount point");

    if (stat.f_flag & ST_RDONLY) {
        if (mount(0, config_.realStoreDir.get().c_str(), "none", MS_REMOUNT | MS_BIND, 0) == -1)
            throw SysError("remounting %1% writable", config_.realStoreDir);
    }
#endif
}


const time_t mtimeStore = 1; /* 1 second into the epoch */


static void canonicaliseTimestampAndPermissions(const Path & path, const struct stat & st)
{
    if (!S_ISLNK(st.st_mode)) {

        /* Mask out all type related bits. */
        mode_t mode = st.st_mode & ~S_IFMT;

        if (mode != 0444 && mode != 0555) {
            mode = (st.st_mode & S_IFMT)
                 | 0444
                 | (st.st_mode & S_IXUSR ? 0111 : 0);
            if (chmod(path.c_str(), mode) == -1)
                throw SysError("changing mode of '%1%' to %2$o", path, mode);
        }

    }

    if (st.st_mtime != mtimeStore) {
        struct timeval times[2];
        times[0].tv_sec = st.st_atime;
        times[0].tv_usec = 0;
        times[1].tv_sec = mtimeStore;
        times[1].tv_usec = 0;
#if HAVE_LUTIMES
        if (lutimes(path.c_str(), times) == -1)
            if (errno != ENOSYS ||
                (!S_ISLNK(st.st_mode) && utimes(path.c_str(), times) == -1))
#else
        if (!S_ISLNK(st.st_mode) && utimes(path.c_str(), times) == -1)
#endif
            throw SysError("changing modification time of '%1%'", path);
    }
}


void canonicaliseTimestampAndPermissions(const Path & path)
{
    canonicaliseTimestampAndPermissions(path, lstat(path));
}


static void canonicalisePathMetaData_(
    const Path & path,
    std::optional<std::pair<uid_t, uid_t>> uidRange,
    InodesSeen & inodesSeen)
{
    checkInterrupt();

#if __APPLE__
    /* Remove flags, in particular UF_IMMUTABLE which would prevent
       the file from being garbage-collected. FIXME: Use
       setattrlist() to remove other attributes as well. */
    if (lchflags(path.c_str(), 0)) {
        if (errno != ENOTSUP)
            throw SysError("clearing flags of path '%1%'", path);
    }
#endif

    auto st = lstat(path);

    /* Really make sure that the path is of a supported type. */
    if (!(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)))
        throw Error("file '%1%' has an unsupported type", path);

    /* Fail if the file is not owned by the build user.  This prevents
       us from messing up the ownership/permissions of files
       hard-linked into the output (e.g. "ln /etc/shadow $out/foo").
       However, ignore files that we chown'ed ourselves previously to
       ensure that we don't fail on hard links within the same build
       (i.e. "touch $out/foo; ln $out/foo $out/bar"). */
    if (uidRange && (st.st_uid < uidRange->first || st.st_uid > uidRange->second)) {
        if (S_ISDIR(st.st_mode) || !inodesSeen.count(Inode(st.st_dev, st.st_ino)))
            throw BuildError("invalid ownership on file '%1%'", path);
        mode_t mode = st.st_mode & ~S_IFMT;
        assert(S_ISLNK(st.st_mode) || (st.st_uid == geteuid() && (mode == 0444 || mode == 0555) && st.st_mtime == mtimeStore));
        return;
    }

#if __linux__
    /* Remove extended attributes / ACLs. */
    ssize_t eaSize = llistxattr(path.c_str(), nullptr, 0);

    if (eaSize < 0) {
        if (errno != ENOTSUP && errno != ENODATA)
            throw SysError("querying extended attributes of '%s'", path);
    } else if (eaSize > 0) {
        std::vector<char> eaBuf(eaSize);

        if ((eaSize = llistxattr(path.c_str(), eaBuf.data(), eaBuf.size())) < 0)
            throw SysError("querying extended attributes of '%s'", path);

        bool resetMode = false;
        if ((S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)) && !(st.st_mode & S_IWUSR)) {
            resetMode = true;
            chmod(path.c_str(), st.st_mode | S_IWUSR);
        }
        for (auto & eaName: tokenizeString<Strings>(std::string(eaBuf.data(), eaSize), std::string("\000", 1))) {
            if (settings.ignoredAcls.get().count(eaName)) continue;
            if (lremovexattr(path.c_str(), eaName.c_str()) == -1)
                throw SysError("removing extended attribute '%s' from '%s'", eaName, path);
        }
        if (resetMode) {
            chmod(path.c_str(), st.st_mode);
            resetMode = false;
        }
     }
#endif

    inodesSeen.insert(Inode(st.st_dev, st.st_ino));

    canonicaliseTimestampAndPermissions(path, st);

    /* Change ownership to the current uid.  If it's a symlink, use
       lchown if available, otherwise don't bother.  Wrong ownership
       of a symlink doesn't matter, since the owning user can't change
       the symlink and can't delete it because the directory is not
       writable.  The only exception is top-level paths in the Nix
       store (since that directory is group-writable for the Nix build
       users group); we check for this case below. */
    if (st.st_uid != geteuid()) {
#if HAVE_LCHOWN
        if (lchown(path.c_str(), geteuid(), getegid()) == -1)
#else
        if (!S_ISLNK(st.st_mode) &&
            chown(path.c_str(), geteuid(), getegid()) == -1)
#endif
            throw SysError("changing owner of '%1%' to %2%",
                path, geteuid());
    }

    if (S_ISDIR(st.st_mode)) {
        DirEntries entries = readDirectory(path);
        for (auto & i : entries)
            canonicalisePathMetaData_(path + "/" + i.name, uidRange, inodesSeen);
    }
}


void canonicalisePathMetaData(
    const Path & path,
    std::optional<std::pair<uid_t, uid_t>> uidRange,
    InodesSeen & inodesSeen)
{
    canonicalisePathMetaData_(path, uidRange, inodesSeen);

    /* On platforms that don't have lchown(), the top-level path can't
       be a symlink, since we can't change its ownership. */
    auto st = lstat(path);

    if (st.st_uid != geteuid()) {
        assert(S_ISLNK(st.st_mode));
        throw Error("wrong ownership of top-level store path '%1%'", path);
    }
}


void canonicalisePathMetaData(const Path & path,
    std::optional<std::pair<uid_t, uid_t>> uidRange)
{
    InodesSeen inodesSeen;
    canonicalisePathMetaData(path, uidRange, inodesSeen);
}


void LocalStore::registerDrvOutput(const Realisation & info, CheckSigsFlag checkSigs)
{
    experimentalFeatureSettings.require(Xp::CaDerivations);
    if (checkSigs == NoCheckSigs || !realisationIsUntrusted(info))
        registerDrvOutput(info);
    else
        throw Error("cannot register realisation '%s' because it lacks a signature by a trusted key", info.outPath.to_string());
}

void LocalStore::registerDrvOutput(const Realisation & info)
{
    experimentalFeatureSettings.require(Xp::CaDerivations);
    retrySQLite([&]() {
        auto state(_dbState.lock());
        if (auto oldR = queryRealisation_(*state, info.id)) {
            if (info.isCompatibleWith(*oldR)) {
                auto combinedSignatures = oldR->signatures;
                combinedSignatures.insert(info.signatures.begin(),
                    info.signatures.end());
                state->stmts->UpdateRealisedOutput.use()
                    (concatStringsSep(" ", combinedSignatures))
                    (info.id.strHash())
                    (info.id.outputName)
                    .exec();
            } else {
                throw Error("Trying to register a realisation of '%s', but we already "
                            "have another one locally.\n"
                            "Local:  %s\n"
                            "Remote: %s",
                    info.id.to_string(),
                    printStorePath(oldR->outPath),
                    printStorePath(info.outPath)
                );
            }
        } else {
            state->stmts->RegisterRealisedOutput.use()
                (info.id.strHash())
                (info.id.outputName)
                (printStorePath(info.outPath))
                (concatStringsSep(" ", info.signatures))
                .exec();
        }
        for (auto & [outputId, depPath] : info.dependentRealisations) {
            auto localRealisation = queryRealisationCore_(*state, outputId);
            if (!localRealisation)
                throw Error("unable to register the derivation '%s' as it "
                            "depends on the non existent '%s'",
                    info.id.to_string(), outputId.to_string());
            if (localRealisation->second.outPath != depPath)
                throw Error("unable to register the derivation '%s' as it "
                            "depends on a realisation of '%s' that doesn’t"
                            "match what we have locally",
                    info.id.to_string(), outputId.to_string());
            state->stmts->AddRealisationReference.use()
                (info.id.strHash())
                (info.id.outputName)
                (outputId.strHash())
                (outputId.outputName)
                .exec();
        }
    });
}

void LocalStore::cacheDrvOutputMapping(
    DBState & state,
    const uint64_t deriver,
    const std::string & outputName,
    const StorePath & output)
{
    state.stmts->AddDerivationOutput.use()
        (deriver)
        (outputName)
        (printStorePath(output))
        .exec();
}


uint64_t LocalStore::addValidPath(DBState & state,
    const ValidPathInfo & info, bool checkOutputs)
{
    if (info.ca.has_value() && !info.isContentAddressed(*this))
        throw Error("cannot add path '%s' to the Nix store because it claims to be content-addressed but isn't",
            printStorePath(info.path));

    state.stmts->RegisterValidPath.use()
        (printStorePath(info.path))
        (info.narHash.to_string(Base::Base16, true))
        (info.registrationTime == 0 ? time(0) : info.registrationTime)
        (info.deriver ? printStorePath(*info.deriver) : "", (bool) info.deriver)
        (info.narSize, info.narSize != 0)
        (info.ultimate ? 1 : 0, info.ultimate)
        (concatStringsSep(" ", info.sigs), !info.sigs.empty())
        (renderContentAddress(info.ca), (bool) info.ca)
        .exec();
    uint64_t id = state.db.getLastInsertedRowId();

    /* If this is a derivation, then store the derivation outputs in
       the database.  This is useful for the garbage collector: it can
       efficiently query whether a path is an output of some
       derivation. */
    if (info.path.isDerivation()) {
        auto drv = readInvalidDerivation(info.path);

        /* Verify that the output paths in the derivation are correct
           (i.e., follow the scheme for computing output paths from
           derivations).  Note that if this throws an error, then the
           DB transaction is rolled back, so the path validity
           registration above is undone. */
        if (checkOutputs) drv.checkInvariants(*this, info.path);

        for (auto & i : drv.outputsAndOptPaths(*this)) {
            /* Floating CA derivations have indeterminate output paths until
               they are built, so don't register anything in that case */
            if (i.second.second)
                cacheDrvOutputMapping(state, id, i.first, *i.second.second);
        }
    }

    {
        auto state_(Store::state.lock());
        state_->pathInfoCache.upsert(std::string(info.path.to_string()),
            PathInfoCacheValue{ .value = std::make_shared<const ValidPathInfo>(info) });
    }

    return id;
}


std::shared_ptr<const ValidPathInfo> LocalStore::queryPathInfoUncached(const StorePath & path)
{
    return retrySQLite([&]() {
        auto state(_dbState.lock());
        return queryPathInfoInternal(*state, path);
    });
}


std::shared_ptr<const ValidPathInfo> LocalStore::queryPathInfoInternal(DBState & state, const StorePath & path)
{
    /* Get the path info. */
    auto useQueryPathInfo(state.stmts->QueryPathInfo.use()(printStorePath(path)));

    if (!useQueryPathInfo.next())
        return nullptr;

    auto id = useQueryPathInfo.getInt(0);

    auto narHash = Hash::dummy;
    try {
        narHash = Hash::parseAnyPrefixed(useQueryPathInfo.getStr(1));
    } catch (BadHash & e) {
        throw BadStorePath("bad hash in store path '%s': %s", printStorePath(path), e.what());
    }

    auto info = std::make_shared<ValidPathInfo>(path, narHash);

    info->id = id;

    info->registrationTime = useQueryPathInfo.getInt(2);

    if (auto deriver = useQueryPathInfo.getStrNullable(3); deriver.has_value()) {
        info->deriver = parseStorePath(*deriver);
    }

    /* Note that narSize = NULL yields 0. */
    info->narSize = useQueryPathInfo.getInt(4);

    info->ultimate = useQueryPathInfo.getInt(5) == 1;

    if (auto sigs = useQueryPathInfo.getStrNullable(6); sigs.has_value()) {
        info->sigs = tokenizeString<StringSet>(*sigs, " ");
    }

    if (auto ca = useQueryPathInfo.getStrNullable(7); ca.has_value()) {
        info->ca = ContentAddress::parseOpt(*ca);
    }

    /* Get the references. */
    auto useQueryReferences(state.stmts->QueryReferences.use()(info->id));

    while (useQueryReferences.next())
        info->references.insert(parseStorePath(useQueryReferences.getStr(0)));

    return info;
}


/* Update path info in the database. */
void LocalStore::updatePathInfo(DBState & state, const ValidPathInfo & info)
{
    state.stmts->UpdatePathInfo.use()
        (info.narSize, info.narSize != 0)
        (info.narHash.to_string(Base::Base16, true))
        (info.ultimate ? 1 : 0, info.ultimate)
        (concatStringsSep(" ", info.sigs), !info.sigs.empty())
        (renderContentAddress(info.ca), (bool) info.ca)
        (printStorePath(info.path))
        .exec();
}


uint64_t LocalStore::queryValidPathId(DBState & state, const StorePath & path)
{
    auto use(state.stmts->QueryPathInfo.use()(printStorePath(path)));
    if (!use.next()) // TODO: I guess if SQLITE got corrupted..?
        throw InvalidPath("path '%s' does not exist in the Lix database", printStorePath(path));
    return use.getInt(0);
}


bool LocalStore::isValidPath_(DBState & state, const StorePath & path)
{
    return state.stmts->QueryPathInfo.use()(printStorePath(path)).next();
}


bool LocalStore::isValidPathUncached(const StorePath & path)
{
    return retrySQLite([&]() {
        auto state(_dbState.lock());
        return isValidPath_(*state, path);
    });
}


StorePathSet LocalStore::queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute)
{
    StorePathSet res;
    for (auto & i : paths)
        if (isValidPath(i)) res.insert(i);
    return res;
}


StorePathSet LocalStore::queryAllValidPaths()
{
    return retrySQLite([&]() {
        auto state(_dbState.lock());
        auto use(state->stmts->QueryValidPaths.use());
        StorePathSet res;
        while (use.next()) res.insert(parseStorePath(use.getStr(0)));
        return res;
    });
}


void LocalStore::queryReferrers(DBState & state, const StorePath & path, StorePathSet & referrers)
{
    auto useQueryReferrers(state.stmts->QueryReferrers.use()(printStorePath(path)));

    while (useQueryReferrers.next())
        referrers.insert(parseStorePath(useQueryReferrers.getStr(0)));
}


void LocalStore::queryReferrers(const StorePath & path, StorePathSet & referrers)
{
    return retrySQLite([&]() {
        auto state(_dbState.lock());
        queryReferrers(*state, path, referrers);
    });
}


StorePathSet LocalStore::queryValidDerivers(const StorePath & path)
{
    return retrySQLite([&]() {
        auto state(_dbState.lock());

        auto useQueryValidDerivers(state->stmts->QueryValidDerivers.use()(printStorePath(path)));

        StorePathSet derivers;
        while (useQueryValidDerivers.next())
            derivers.insert(parseStorePath(useQueryValidDerivers.getStr(1)));

        return derivers;
    });
}


std::map<std::string, std::optional<StorePath>>
LocalStore::queryStaticPartialDerivationOutputMap(const StorePath & path)
{
    return retrySQLite([&]() {
        auto state(_dbState.lock());
        std::map<std::string, std::optional<StorePath>> outputs;
        uint64_t drvId;
        drvId = queryValidPathId(*state, path);
        auto use(state->stmts->QueryDerivationOutputs.use()(drvId));
        while (use.next())
            outputs.insert_or_assign(
                use.getStr(0), parseStorePath(use.getStr(1)));

        return outputs;
    });
}

std::optional<StorePath> LocalStore::queryPathFromHashPart(const std::string & hashPart)
{
    if (hashPart.size() != StorePath::HashLen) throw Error("invalid hash part");

    Path prefix = config_.storeDir + "/" + hashPart;

    return retrySQLite([&]() -> std::optional<StorePath> {
        auto state(_dbState.lock());

        auto useQueryPathFromHashPart(state->stmts->QueryPathFromHashPart.use()(prefix));

        if (!useQueryPathFromHashPart.next()) return {};

        auto s = useQueryPathFromHashPart.getStrNullable(0);
        if (s.has_value() && s->starts_with(prefix))
            return parseStorePath(*s);
        return {};
    });
}


StorePathSet LocalStore::querySubstitutablePaths(const StorePathSet & paths)
{
    if (!settings.useSubstitutes) return StorePathSet();

    StorePathSet remaining;
    for (auto & i : paths)
        remaining.insert(i);

    StorePathSet res;

    for (auto & sub : getDefaultSubstituters()) {
        if (remaining.empty()) break;
        if (sub->config().storeDir != config_.storeDir) continue;
        if (!sub->config().wantMassQuery) continue;

        auto valid = sub->queryValidPaths(remaining);

        StorePathSet remaining2;
        for (auto & path : remaining)
            if (valid.count(path))
                res.insert(path);
            else
                remaining2.insert(path);

        std::swap(remaining, remaining2);
    }

    return res;
}


void LocalStore::registerValidPath(const ValidPathInfo & info)
{
    registerValidPaths({{info.path, info}});
}


void LocalStore::registerValidPaths(const ValidPathInfos & infos)
{
    /* SQLite will fsync by default, but the new valid paths may not
       be fsync-ed.  So some may want to fsync them before registering
       the validity, at the expense of some speed of the path
       registering operation. */
    if (settings.syncBeforeRegistering) sync();

    return retrySQLite([&]() {
        auto state(_dbState.lock());

        SQLiteTxn txn = state->db.beginTransaction();
        StorePathSet paths;

        for (auto & [_, i] : infos) {
            assert(i.narHash.type == HashType::SHA256);
            if (isValidPath_(*state, i.path))
                updatePathInfo(*state, i);
            else
                addValidPath(*state, i, false);
            paths.insert(i.path);
        }

        for (auto & [_, i] : infos) {
            auto referrer = queryValidPathId(*state, i.path);
            for (auto & j : i.references)
                state->stmts->AddReference.use()(referrer)(queryValidPathId(*state, j)).exec();
        }

        /* Check that the derivation outputs are correct.  We can't do
           this in addValidPath() above, because the references might
           not be valid yet. */
        for (auto & [_, i] : infos)
            if (i.path.isDerivation()) {
                // FIXME: inefficient; we already loaded the derivation in addValidPath().
                readInvalidDerivation(i.path).checkInvariants(*this, i.path);
            }

        /* Do a topological sort of the paths.  This will throw an
           error if a cycle is detected and roll back the
           transaction.  Cycles can only occur when a derivation
           has multiple outputs. */
        topoSort(paths,
            {[&](const StorePath & path) {
                auto i = infos.find(path);
                return i == infos.end() ? StorePathSet() : i->second.references;
            }},
            {[&](const StorePath & path, const StorePath & parent) {
                return BuildError(
                    "cycle detected in the references of '%s' from '%s'",
                    printStorePath(path),
                    printStorePath(parent));
            }});

        txn.commit();
    });
}


/* Invalidate a path.  The caller is responsible for checking that
   there are no referrers. */
void LocalStore::invalidatePath(DBState & state, const StorePath & path)
{
    debug("invalidating path '%s'", printStorePath(path));

    state.stmts->InvalidatePath.use()(printStorePath(path)).exec();

    /* Note that the foreign key constraints on the Refs table take
       care of deleting the references entries for `path'. */

    {
        auto state_(Store::state.lock());
        state_->pathInfoCache.erase(std::string(path.to_string()));
    }
}

const PublicKeys & LocalStore::getPublicKeys()
{
    std::call_once(publicKeysFlag, [](std::unique_ptr<const PublicKeys> &pks) -> void {
        pks = std::make_unique<const PublicKeys>(getDefaultPublicKeys());
    }, publicKeys);
    return *publicKeys;
}

bool LocalStore::pathInfoIsUntrusted(const ValidPathInfo & info)
{
    return config_.requireSigs && !info.checkSignatures(*this, getPublicKeys());
}

bool LocalStore::realisationIsUntrusted(const Realisation & realisation)
{
    return config_.requireSigs && !realisation.checkSignatures(getPublicKeys());
}

void LocalStore::addToStore(const ValidPathInfo & info, Source & source,
    RepairFlag repair, CheckSigsFlag checkSigs)
{
    if (checkSigs && pathInfoIsUntrusted(info))
        throw Error("cannot add path '%s' because it lacks a signature by a trusted key", printStorePath(info.path));

    /* In case we are not interested in reading the NAR: discard it. */
    bool narRead = false;
    Finally cleanup = [&]() {
        if (!narRead) {
            NARParseVisitor sink;
            try {
                parseDump(sink, source);
            } catch (...) {
                ignoreExceptionExceptInterrupt();
            }
        }
    };

    addTempRoot(info.path);

    if (repair || !isValidPath(info.path)) {

        PathLocks outputLock;

        auto realPath = Store::toRealPath(info.path);

        /* Lock the output path.  But don't lock if we're being called
           from a build hook (whose parent process already acquired a
           lock on this path). */
        if (!locksHeld.count(printStorePath(info.path)))
            outputLock.lockPaths({realPath});

        if (repair || !isValidPath(info.path)) {

            deletePath(realPath);

            /* While restoring the path from the NAR, compute the hash
               of the NAR. */
            HashSink hashSink(HashType::SHA256);

            TeeSource wrapperSource { source, hashSink };

            narRead = true;
            restorePath(realPath, wrapperSource);

            auto hashResult = hashSink.finish();

            if (hashResult.first != info.narHash)
                throw Error("hash mismatch importing path '%s';\n  specified: %s\n  got:       %s",
                    printStorePath(info.path), info.narHash.to_string(Base::Base32, true), hashResult.first.to_string(Base::Base32, true));

            if (hashResult.second != info.narSize)
                throw Error("size mismatch importing path '%s';\n  specified: %s\n  got:       %s",
                    printStorePath(info.path), info.narSize, hashResult.second);

            if (info.ca) {
                auto & specified = *info.ca;
                auto actualHash = hashCAPath(
                    specified.method,
                    specified.hash.type,
                    info.path
                );
                if (specified.hash != actualHash.hash) {
                    throw Error("ca hash mismatch importing path '%s';\n  specified: %s\n  got:       %s",
                        printStorePath(info.path),
                        specified.hash.to_string(Base::Base32, true),
                        actualHash.hash.to_string(Base::Base32, true));
                }
            }

            autoGC();

            canonicalisePathMetaData(realPath, {});

            optimisePath(realPath, repair); // FIXME: combine with hashPath()

            registerValidPath(info);
        }

        outputLock.setDeletion(true);
    }
}


StorePath LocalStore::addToStoreFromDump(Source & source0, std::string_view name,
    FileIngestionMethod method, HashType hashAlgo, RepairFlag repair, const StorePathSet & references)
{
    /* For computing the store path. */
    auto hashSink = std::make_unique<HashSink>(hashAlgo);
    TeeSource source { source0, *hashSink };

    /* Read the source path into memory, but only if it's up to
       narBufferSize bytes. If it's larger, write it to a temporary
       location in the Nix store. If the subsequently computed
       destination store path is already valid, we just delete the
       temporary path. Otherwise, we move it to the destination store
       path. */
    bool inMemory = false;

    struct Free {
        void operator()(void* v) { free(v); }
    };
    std::unique_ptr<char, Free> dumpBuffer(nullptr);
    std::string_view dump;

    /* Fill out buffer, and decide whether we are working strictly in
       memory based on whether we break out because the buffer is full
       or the original source is empty */
    while (dump.size() < settings.narBufferSize) {
        auto oldSize = dump.size();
        constexpr size_t chunkSize = 65536;
        auto want = std::min(chunkSize, settings.narBufferSize - oldSize);

        auto *toRealloc = dumpBuffer.release();
        if (auto realloced = realloc(toRealloc, oldSize + want)) {
            dumpBuffer.reset(static_cast<char *>(realloced));
        } else {
            free(toRealloc);
            throw std::bad_alloc();
        }
        auto got = 0;
        Finally cleanup([&]() {
            dump = {dumpBuffer.get(), dump.size() + got};
        });
        try {
            got = source.read(dumpBuffer.get() + oldSize, want);
        } catch (EndOfFile &) {
            inMemory = true;
            break;
        }
    }

    std::unique_ptr<AutoDelete> delTempDir;
    Path tempPath;
    Path tempDir;
    AutoCloseFD tempDirFd;

    if (!inMemory) {
        /* Drain what we pulled so far, and then keep on pulling */
        StringSource dumpSource { dump };
        ChainSource bothSource { dumpSource, source };

        std::tie(tempDir, tempDirFd) = createTempDirInStore();
        delTempDir = std::make_unique<AutoDelete>(tempDir);
        tempPath = tempDir + "/x";

        if (method == FileIngestionMethod::Recursive)
            restorePath(tempPath, bothSource);
        else
            writeFile(tempPath, bothSource);

        dumpBuffer.reset();
        dump = {};
    }

    auto [hash, size] = hashSink->finish();

    ContentAddressWithReferences desc = FixedOutputInfo {
        .method = method,
        .hash = hash,
        .references = {
            .others = references,
            // caller is not capable of creating a self-reference, because this is content-addressed without modulus
            .self = false,
        },
    };

    auto dstPath = makeFixedOutputPathFromCA(name, desc);

    addTempRoot(dstPath);

    if (repair || !isValidPath(dstPath)) {

        /* The first check above is an optimisation to prevent
           unnecessary lock acquisition. */

        auto realPath = Store::toRealPath(dstPath);

        PathLocks outputLock({realPath});

        if (repair || !isValidPath(dstPath)) {

            deletePath(realPath);

            autoGC();

            if (inMemory) {
                StringSource dumpSource { dump };
                /* Restore from the NAR in memory. */
                if (method == FileIngestionMethod::Recursive)
                    restorePath(realPath, dumpSource);
                else
                    writeFile(realPath, dumpSource);
            } else {
                /* Move the temporary path we restored above. */
                moveFile(tempPath, realPath);
            }

            /* For computing the nar hash. In recursive SHA-256 mode, this
               is the same as the store hash, so no need to do it again. */
            auto narHash = std::pair { hash, size };
            if (method != FileIngestionMethod::Recursive || hashAlgo != HashType::SHA256) {
                HashSink narSink { HashType::SHA256 };
                narSink << dumpPath(realPath);
                narHash = narSink.finish();
            }

            canonicalisePathMetaData(realPath, {}); // FIXME: merge into restorePath

            optimisePath(realPath, repair);

            ValidPathInfo info {
                *this,
                name,
                std::move(desc),
                narHash.first
            };
            info.narSize = narHash.second;
            registerValidPath(info);
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


StorePath LocalStore::addTextToStore(
    std::string_view name,
    std::string_view s,
    const StorePathSet & references, RepairFlag repair)
{
    auto hash = hashString(HashType::SHA256, s);
    auto dstPath = makeTextPath(name, TextInfo {
        .hash = hash,
        .references = references,
    });

    addTempRoot(dstPath);

    if (repair || !isValidPath(dstPath)) {

        auto realPath = Store::toRealPath(dstPath);

        PathLocks outputLock({realPath});

        if (repair || !isValidPath(dstPath)) {

            deletePath(realPath);

            autoGC();

            writeFile(realPath, s);

            canonicalisePathMetaData(realPath, {});

            StringSink sink;
            sink << dumpString(s);
            auto narHash = hashString(HashType::SHA256, sink.s);

            optimisePath(realPath, repair);

            ValidPathInfo info { dstPath, narHash };
            info.narSize = sink.s.size();
            info.references = references;
            info.ca = {
                .method = TextIngestionMethod {},
                .hash = hash,
            };
            registerValidPath(info);
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


/* Create a temporary directory in the store that won't be
   garbage-collected until the returned FD is closed. */
std::pair<Path, AutoCloseFD> LocalStore::createTempDirInStore()
{
    Path tmpDirFn;
    AutoCloseFD tmpDirFd;
    bool lockedByUs = false;
    do {
        /* There is a slight possibility that `tmpDir' gets deleted by
           the GC between createTempDir() and when we acquire a lock on it.
           We'll repeat until 'tmpDir' exists and we've locked it. */
        tmpDirFn = createTempDir(config_.realStoreDir, "tmp");
        tmpDirFd = AutoCloseFD{open(tmpDirFn.c_str(), O_RDONLY | O_DIRECTORY)};
        if (tmpDirFd.get() < 0) {
            continue;
        }
        lockedByUs = lockFile(tmpDirFd.get(), ltWrite, true);
    } while (!pathExists(tmpDirFn) || !lockedByUs);
    return {tmpDirFn, std::move(tmpDirFd)};
}


void LocalStore::invalidatePathChecked(const StorePath & path)
{
    retrySQLite([&]() {
        auto state(_dbState.lock());

        SQLiteTxn txn = state->db.beginTransaction();

        if (isValidPath_(*state, path)) {
            StorePathSet referrers; queryReferrers(*state, path, referrers);
            referrers.erase(path); /* ignore self-references */
            if (!referrers.empty())
                throw PathInUse("cannot delete path '%s' because it is in use by %s",
                    printStorePath(path), showPaths(referrers));
            invalidatePath(*state, path);
        }

        txn.commit();
    });
}


bool LocalStore::verifyStore(bool checkContents, RepairFlag repair)
{
    printInfo("reading the Nix store...");

    bool errors = false;

    /* Acquire the global GC lock to get a consistent snapshot of
       existing and valid paths. */
    auto fdGCLock = openGCLock();
    FdLock gcLock(fdGCLock.get(), ltRead, true, "waiting for the big garbage collector lock...");

    StorePathSet validPaths;

    {
        StorePathSet storePathsInStoreDir;
        /* Why aren't we using `queryAllValidPaths`? Because that would
           tell us about all the paths than the database knows about. Here we
           want to know about all the store paths in the store directory,
           regardless of what the database thinks.

           We will end up cross-referencing these two sources of truth (the
           database and the filesystem) in the loop below, in order to catch
           invalid states.
         */
        for (auto & i : readDirectory(config_.realStoreDir)) {
            try {
                storePathsInStoreDir.insert({i.name});
            } catch (BadStorePath &) { }
        }

        /* Check whether all valid paths actually exist. */
        printInfo("checking path existence...");

        StorePathSet done;

        for (auto & i : queryAllValidPaths())
            verifyPath(i, storePathsInStoreDir, done, validPaths, repair, errors);
    }

    /* Optionally, check the content hashes (slow). */
    if (checkContents) {

        printInfo("checking link hashes...");

        for (auto & link : readDirectory(linksDir)) {
            printMsg(lvlTalkative, "checking contents of '%s'", link.name);
            Path linkPath = linksDir + "/" + link.name;
            std::string hash = hashPath(HashType::SHA256, linkPath).first.to_string(Base::Base32, false);
            if (hash != link.name) {
                printError("link '%s' was modified! expected hash '%s', got '%s'",
                    linkPath, link.name, hash);
                if (repair) {
                    if (unlink(linkPath.c_str()) == 0)
                        printInfo("removed link '%s'", linkPath);
                    else
                        throw SysError("removing corrupt link '%s'", linkPath);
                } else {
                    errors = true;
                }
            }
        }

        printInfo("checking store hashes...");

        Hash nullHash(HashType::SHA256);

        for (auto & i : validPaths) {
            try {
                auto info = std::const_pointer_cast<ValidPathInfo>(std::shared_ptr<const ValidPathInfo>(queryPathInfo(i)));

                /* Check the content hash (optionally - slow). */
                printMsg(lvlTalkative, "checking contents of '%s'", printStorePath(i));

                auto hashSink = HashSink(info->narHash.type);

                hashSink << dumpPath(Store::toRealPath(i));
                auto current = hashSink.finish();

                if (info->narHash != nullHash && info->narHash != current.first) {
                    printError("path '%s' was modified! expected hash '%s', got '%s'",
                        printStorePath(i), info->narHash.to_string(Base::Base32, true), current.first.to_string(Base::Base32, true));
                    if (repair) RUN_ASYNC_IN_NEW_THREAD(repairPath(i)); else errors = true;
                } else {

                    bool update = false;

                    /* Fill in missing hashes. */
                    if (info->narHash == nullHash) {
                        printInfo("fixing missing hash on '%s'", printStorePath(i));
                        info->narHash = current.first;
                        update = true;
                    }

                    /* Fill in missing narSize fields (from old stores). */
                    if (info->narSize == 0) {
                        printInfo("updating size field on '%s' to %s", printStorePath(i), current.second);
                        info->narSize = current.second;
                        update = true;
                    }

                    if (update) {
                        auto state(_dbState.lock());
                        updatePathInfo(*state, *info);
                    }

                }

            } catch (Error & e) {
                /* It's possible that the path got GC'ed, so ignore
                   errors on invalid paths. */
                if (isValidPath(i))
                    logError(e.info());
                else
                    warn(e.msg());
                errors = true;
            }
        }
    }

    return errors;
}


void LocalStore::verifyPath(const StorePath & path, const StorePathSet & storePathsInStoreDir,
    StorePathSet & done, StorePathSet & validPaths, RepairFlag repair, bool & errors)
{
    checkInterrupt();

    if (!done.insert(path).second) return;

    if (!storePathsInStoreDir.count(path)) {
        /* Check any referrers first.  If we can invalidate them
           first, then we can invalidate this path as well. */
        bool canInvalidate = true;
        StorePathSet referrers; queryReferrers(path, referrers);
        for (auto & i : referrers)
            if (i != path) {
                verifyPath(i, storePathsInStoreDir, done, validPaths, repair, errors);
                if (validPaths.count(i))
                    canInvalidate = false;
            }

        auto pathS = printStorePath(path);

        if (canInvalidate) {
            printInfo("path '%s' disappeared, removing from database...", pathS);
            auto state(_dbState.lock());
            invalidatePath(*state, path);
        } else {
            printError("path '%s' disappeared, but it still has valid referrers!", pathS);
            if (repair)
                try {
                    RUN_ASYNC_IN_NEW_THREAD(repairPath(path));
                } catch (Error & e) {
                    logWarning(e.info());
                    errors = true;
                }
            else errors = true;
        }

        return;
    }

    validPaths.insert(std::move(path));
}


unsigned int LocalStore::getProtocol()
{
    return PROTOCOL_VERSION;
}

std::optional<TrustedFlag> LocalStore::isTrustedClient()
{
    return Trusted;
}


#if defined(FS_IOC_SETFLAGS) && defined(FS_IOC_GETFLAGS) && defined(FS_IMMUTABLE_FL)

static void makeMutable(const Path & path)
{
    checkInterrupt();

    auto st = lstat(path);

    if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) return;

    if (S_ISDIR(st.st_mode)) {
        for (auto & i : readDirectory(path))
            makeMutable(path + "/" + i.name);
    }

    /* The O_NOFOLLOW is important to prevent us from changing the
       mutable bit on the target of a symlink (which would be a
       security hole). */
    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd == -1) {
        if (errno == ELOOP) return; // it's a symlink
        throw SysError("opening file '%1%'", path);
    }

    unsigned int flags = 0, old;

    /* Silently ignore errors getting/setting the immutable flag so
       that we work correctly on filesystems that don't support it. */
    if (ioctl(fd, FS_IOC_GETFLAGS, &flags)) return;
    old = flags;
    flags &= ~FS_IMMUTABLE_FL;
    if (old == flags) return;
    if (ioctl(fd, FS_IOC_SETFLAGS, &flags)) return;
}

/* Upgrade from schema 6 (Nix 0.15) to schema 7 (Nix >= 1.3). */
void LocalStore::upgradeStore7()
{
    if (getuid() != 0) return;
    printInfo("removing immutable bits from the Nix store (this may take a while)...");
    makeMutable(config_.realStoreDir);
}

#else

void LocalStore::upgradeStore7()
{
}

#endif


void LocalStore::addSignatures(const StorePath & storePath, const StringSet & sigs)
{
    retrySQLite([&]() {
        auto state(_dbState.lock());

        SQLiteTxn txn = state->db.beginTransaction();

        auto info = std::const_pointer_cast<ValidPathInfo>(queryPathInfoInternal(*state, storePath));

        info->sigs.insert(sigs.begin(), sigs.end());

        updatePathInfo(*state, *info);

        txn.commit();
    });
}


void LocalStore::signRealisation(Realisation & realisation)
{
    // FIXME: keep secret keys in memory.

    auto secretKeyFiles = settings.secretKeyFiles;

    for (auto & secretKeyFile : secretKeyFiles.get()) {
        SecretKey secretKey(readFile(secretKeyFile));
        realisation.sign(secretKey);
    }
}

void LocalStore::signPathInfo(ValidPathInfo & info)
{
    // FIXME: keep secret keys in memory.

    auto secretKeyFiles = settings.secretKeyFiles;

    for (auto & secretKeyFile : secretKeyFiles.get()) {
        SecretKey secretKey(readFile(secretKeyFile));
        info.sign(*this, secretKey);
    }
}


std::optional<std::pair<int64_t, Realisation>> LocalStore::queryRealisationCore_(
        LocalStore::DBState & state,
        const DrvOutput & id)
{
    auto useQueryRealisedOutput(
            state.stmts->QueryRealisedOutput.use()
                (id.strHash())
                (id.outputName));
    if (!useQueryRealisedOutput.next())
        return std::nullopt;
    auto realisationDbId = useQueryRealisedOutput.getInt(0);
    auto outputPath = parseStorePath(useQueryRealisedOutput.getStr(1));
    auto signatures =
        tokenizeString<StringSet>(useQueryRealisedOutput.getStr(2));

    return {{
        realisationDbId,
        Realisation{
            .id = id,
            .outPath = outputPath,
            .signatures = signatures,
        }
    }};
}

std::optional<const Realisation> LocalStore::queryRealisation_(
            LocalStore::DBState & state,
            const DrvOutput & id)
{
    auto maybeCore = queryRealisationCore_(state, id);
    if (!maybeCore)
        return std::nullopt;
    auto [realisationDbId, res] = *maybeCore;

    std::map<DrvOutput, StorePath> dependentRealisations;
    auto useRealisationRefs(
        state.stmts->QueryRealisationReferences.use()
            (realisationDbId));
    while (useRealisationRefs.next()) {
        auto depId = DrvOutput {
            Hash::parseAnyPrefixed(useRealisationRefs.getStr(0)),
            useRealisationRefs.getStr(1),
        };
        auto dependentRealisation = queryRealisationCore_(state, depId);
        assert(dependentRealisation); // Enforced by the db schema
        auto outputPath = dependentRealisation->second.outPath;
        dependentRealisations.insert({depId, outputPath});
    }

    res.dependentRealisations = dependentRealisations;

    return { res };
}

std::shared_ptr<const Realisation> LocalStore::queryRealisationUncached(const DrvOutput & id)
{
    auto maybeRealisation = retrySQLite([&]() {
        auto state(_dbState.lock());
        return queryRealisation_(*state, id);
    });
    if (maybeRealisation)
        return std::make_shared<const Realisation>(maybeRealisation.value());
    else
        return nullptr;
}

ContentAddress LocalStore::hashCAPath(
    const ContentAddressMethod & method, const HashType & hashType,
    const StorePath & path)
{
    return hashCAPath(method, hashType, Store::toRealPath(path), path.hashPart());
}

ContentAddress LocalStore::hashCAPath(
    const ContentAddressMethod & method,
    const HashType & hashType,
    const Path & path,
    const std::string_view pathHash
)
{
    auto data = std::visit(overloaded {
        [&](const TextIngestionMethod &) -> GeneratorSource {
            return GeneratorSource(readFileSource(path));
        },
        [&](const FileIngestionMethod & m2) -> GeneratorSource {
            switch (m2) {
            case FileIngestionMethod::Recursive:
                return GeneratorSource(dumpPath(path));
            case FileIngestionMethod::Flat:
                return GeneratorSource(readFileSource(path));
            }
            assert(false);
        },
    }, method.raw);
    return ContentAddress {
        .method = method,
        .hash = computeHashModulo(hashType, std::string(pathHash), data).first,
    };
}

void LocalStore::addBuildLog(const StorePath & drvPath, std::string_view log)
{
    assert(drvPath.isDerivation());

    auto baseName = drvPath.to_string();

    auto logPath =
        fmt("%s/%s/%s/%s.bz2", config_.logDir, drvsLogDir, baseName.substr(0, 2), baseName.substr(2)
        );

    if (pathExists(logPath)) return;

    createDirs(dirOf(logPath));

    auto tmpFile = fmt("%s.tmp.%d", logPath, getpid());

    writeFile(tmpFile, compress("bzip2", log));

    renameFile(tmpFile, logPath);
}

std::optional<std::string> LocalStore::getVersion()
{
    return nixVersion;
}

}  // namespace nix
