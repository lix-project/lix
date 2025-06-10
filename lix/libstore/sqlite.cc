#include "lix/libutil/async.hh"
#include "lix/libutil/charptr-cast.hh"
#include "lix/libstore/sqlite.hh"
#include "lix/libstore/globals.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/url.hh"

#include <chrono>
#include <kj/time.h>
#include <random>
#include <sqlite3.h>

#include <random>
#include <thread>

namespace nix {

SQLiteError::SQLiteError(const char *path, const char *errMsg, int errNo, int extendedErrNo, int offset, HintFmt && hf)
  : Error(""), path(path), errMsg(errMsg), errNo(errNo), extendedErrNo(extendedErrNo), offset(offset)
{
    auto offsetStr = (offset == -1) ? "" : "at offset " + std::to_string(offset) + ": ";
    err.msg = HintFmt("%s: %s%s, %s (in '%s')",
        Uncolored(hf.str()),
        offsetStr,
        sqlite3_errstr(extendedErrNo),
        errMsg,
        path ? path : "(in-memory)");
}

[[noreturn]] void SQLiteError::throw_(sqlite3 * db, HintFmt && hf)
{
    int err = sqlite3_errcode(db);
    int exterr = sqlite3_extended_errcode(db);
    int offset = sqlite3_error_offset(db);

    auto path = sqlite3_db_filename(db, nullptr);
    auto errMsg = sqlite3_errmsg(db);

    if (err == SQLITE_BUSY || err == SQLITE_PROTOCOL) {
        auto exp = SQLiteBusy(path, errMsg, err, exterr, offset, std::move(hf));
        exp.err.msg = HintFmt(
            err == SQLITE_PROTOCOL
                ? "SQLite database '%s' is busy (SQLITE_PROTOCOL)"
                : "SQLite database '%s' is busy",
            path ? path : "(in-memory)");
        throw exp;
    } else
        throw SQLiteError(path, errMsg, err, exterr, offset, std::move(hf));
}

static void traceSQL(void * x, const char * sql)
{
    // wacky delimiters:
    //   so that we're quite unambiguous without escaping anything
    // notice instead of trace:
    //   so that this can be enabled without getting the firehose in our face.
    notice("SQL<[%1%]>", sql);
};

SQLite::SQLite(const Path & path, SQLiteOpenMode mode)
{
    // useSQLiteWAL also indicates what virtual file system we need.  Using
    // `unix-dotfile` is needed on NFS file systems and on Windows' Subsystem
    // for Linux (WSL) where useSQLiteWAL should be false by default.
    const char *vfs = settings.useSQLiteWAL ? 0 : "unix-dotfile";
    bool immutable = mode == SQLiteOpenMode::Immutable;
    int flags = immutable ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
    if (mode == SQLiteOpenMode::Normal) flags |= SQLITE_OPEN_CREATE;
    auto uri = "file:" + percentEncode(path) + "?immutable=" + (immutable ? "1" : "0");
    sqlite3 * db;
    int ret = sqlite3_open_v2(uri.c_str(), &db, SQLITE_OPEN_URI | flags, vfs);
    if (ret != SQLITE_OK) {
        const char * err = sqlite3_errstr(ret);
        throw Error("cannot open SQLite database '%s': %s", path, err);
    }
    this->db.reset(db);

    if (sqlite3_busy_timeout(db, 50) != SQLITE_OK)
        SQLiteError::throw_(db, "setting timeout");

    if (getEnv("NIX_DEBUG_SQLITE_TRACES") == "1") {
        // To debug sqlite statements; trace all of them
        sqlite3_trace(db, &traceSQL, nullptr);
    }

    exec("pragma foreign_keys = 1", always_progresses);
}

void SQLite::Close::operator()(sqlite3 * db)
{
    try {
        if (sqlite3_close(db) != SQLITE_OK)
            SQLiteError::throw_(db, "closing database");
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

void SQLite::isCache()
{
    exec("pragma synchronous = off", always_progresses);
    exec("pragma main.journal_mode = truncate", always_progresses);
}

void SQLite::exec(const std::string & stmt, NeverAsync)
{
    retrySQLite([&]() {
        if (sqlite3_exec(db.get(), stmt.c_str(), 0, 0, 0) != SQLITE_OK)
            SQLiteError::throw_(db.get(), "executing SQLite statement '%s'", stmt);
    });
}

SQLiteStmt SQLite::create(const std::string & stmt)
{
    return SQLiteStmt(db.get(), stmt);
}

SQLiteTxn SQLite::beginTransaction(SQLiteTxnType type)
{
    return SQLiteTxn(db.get(), type);
}

void SQLite::setPersistWAL(bool persist)
{
    int enable = persist ? 1 : 0;
    if (sqlite3_file_control(db.get(), nullptr, SQLITE_FCNTL_PERSIST_WAL, &enable) != SQLITE_OK) {
        SQLiteError::throw_(db.get(), "setting persistent WAL mode");
    }
}

uint64_t SQLite::getLastInsertedRowId()
{
    return sqlite3_last_insert_rowid(db.get());
}

uint64_t SQLite::getRowsChanged()
{
    return sqlite3_changes64(db.get());
}

SQLiteStmt::SQLiteStmt(sqlite3 * db, const std::string & sql)
{
    checkInterrupt();
    sqlite3_stmt * stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK)
        SQLiteError::throw_(db, "creating statement '%s'", sql);
    this->stmt = {stmt, {this}};
    this->db = db;
    this->sql = sql;
}

void SQLiteStmt::Finalize::operator()(sqlite3_stmt * stmt)
{
    try {
        if (sqlite3_finalize(stmt) != SQLITE_OK)
            SQLiteError::throw_(parent->db, "finalizing statement '%s'", parent->sql);
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

SQLiteStmt::Use::Use(SQLiteStmt & stmt)
    : stmt(stmt)
{
    assert(stmt.stmt);
    /* Note: sqlite3_reset() returns the error code for the most
       recent call to sqlite3_step().  So ignore it. */
    sqlite3_reset(stmt.stmt.get());
}

SQLiteStmt::Use::~Use()
{
    sqlite3_reset(stmt.stmt.get());
}

SQLiteStmt::Use & SQLiteStmt::Use::operator () (std::string_view value, bool notNull)
{
    if (notNull) {
        if (sqlite3_bind_text(
                stmt.stmt.get(), curArg++, value.data(), value.length(), SQLITE_TRANSIENT
            )
            != SQLITE_OK)
        {
            SQLiteError::throw_(stmt.db, "binding argument");
        }
    } else
        bind();
    return *this;
}

SQLiteStmt::Use & SQLiteStmt::Use::operator () (const unsigned char * data, size_t len, bool notNull)
{
    if (notNull) {
        if (sqlite3_bind_blob(stmt.stmt.get(), curArg++, data, len, SQLITE_TRANSIENT) != SQLITE_OK)
            SQLiteError::throw_(stmt.db, "binding argument");
    } else
        bind();
    return *this;
}

SQLiteStmt::Use & SQLiteStmt::Use::operator () (int64_t value, bool notNull)
{
    if (notNull) {
        if (sqlite3_bind_int64(stmt.stmt.get(), curArg++, value) != SQLITE_OK)
            SQLiteError::throw_(stmt.db, "binding argument");
    } else
        bind();
    return *this;
}

SQLiteStmt::Use & SQLiteStmt::Use::bind()
{
    if (sqlite3_bind_null(stmt.stmt.get(), curArg++) != SQLITE_OK)
        SQLiteError::throw_(stmt.db, "binding argument");
    return *this;
}

void SQLiteStmt::Use::exec()
{
    bool r = next();
    assert(!r);
}

bool SQLiteStmt::Use::next()
{
    int r = sqlite3_step(stmt.stmt.get());
    if (r != SQLITE_DONE && r != SQLITE_ROW)
        SQLiteError::throw_(stmt.db, fmt("executing SQLite query '%s'", sqlite3_expanded_sql(stmt.stmt.get())));
    return r == SQLITE_ROW;
}

std::optional<std::string> SQLiteStmt::Use::getStrNullable(int col)
{
    auto s = charptr_cast<const char *>(sqlite3_column_text(stmt.stmt.get(), col));
    return s != nullptr ? std::make_optional<std::string>((s)) : std::nullopt;
}

std::string SQLiteStmt::Use::getStr(int col)
{
    if (auto res = getStrNullable(col); res.has_value()) {
        return *res;
    } else {
        // FIXME: turn into fatal non-exception error with actual formatting when we have those
        assert(false && "sqlite3 retrieved unexpected null");
    }
}

int64_t SQLiteStmt::Use::getInt(int col)
{
    // FIXME: detect nulls?
    return sqlite3_column_int64(stmt.stmt.get(), col);
}

bool SQLiteStmt::Use::isNull(int col)
{
    return sqlite3_column_type(stmt.stmt.get(), col) == SQLITE_NULL;
}

SQLiteTxn::SQLiteTxn(sqlite3 * db, SQLiteTxnType type)
{
    const char * sql = "begin;";
    switch (type) {
    case SQLiteTxnType::Deferred:
        break;
    case SQLiteTxnType::Immediate:
        sql = "begin immediate;";
        break;
    case SQLiteTxnType::Exclusive:
        sql = "begin exclusive;";
        break;
    }
    if (sqlite3_exec(db, sql, 0, 0, 0) != SQLITE_OK)
        SQLiteError::throw_(db, "starting transaction");
    this->db.reset(db);
}

void SQLiteTxn::commit()
{
    if (sqlite3_exec(db.get(), "commit;", 0, 0, 0) != SQLITE_OK)
        SQLiteError::throw_(db.get(), "committing transaction");
    (void) db.release(); // not a leak, the deleter only runs `rollback;`
}

void SQLiteTxn::Rollback::operator()(sqlite3 * db)
{
    try {
        if (sqlite3_exec(db, "rollback;", 0, 0, 0) != SQLITE_OK)
            SQLiteError::throw_(db, "aborting transaction");
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

[[nodiscard]]
static std::chrono::milliseconds handleSQLiteBusyCommon(const SQLiteBusy & e, std::chrono::time_point<std::chrono::steady_clock> & nextWarning)
{
    auto now = std::chrono::steady_clock::now();
    if (now > nextWarning) {
        nextWarning = now + std::chrono::seconds(10);
        logWarning({
            .msg = HintFmt(e.what())
        });
    }

    /* Sleep for a while since retrying the transaction right away
       is likely to fail again. */
    checkInterrupt();
    static thread_local std::default_random_engine generator(std::random_device{}());
    std::uniform_int_distribution<long> uniform_dist(0, 100);
    /* <= 0.1s */
    return std::chrono::milliseconds { uniform_dist(generator) };
}

void handleSQLiteBusy(const SQLiteBusy & e, std::chrono::time_point<std::chrono::steady_clock> & nextWarning)
{
    std::this_thread::sleep_for(handleSQLiteBusyCommon(e, nextWarning));
}

kj::Promise<Result<void>> handleSQLiteBusyAsync(const SQLiteBusy & e, std::chrono::time_point<std::chrono::steady_clock> & nextWarning)
try {
    std::chrono::milliseconds delay_ms = handleSQLiteBusyCommon(e, nextWarning);
    co_await AIO().provider.getTimer().afterDelay(delay_ms.count() * kj::MILLISECONDS);
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

}
