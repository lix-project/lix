#pragma once
///@file

#include <string>

#include "lix/libutil/types.hh"
#include "lix/libutil/error.hh"

struct sqlite3;
struct sqlite3_stmt;

namespace nix {

enum class SQLiteOpenMode {
    /**
     * Open the database in read-write mode.
     * If the database does not exist, it will be created.
     */
    Normal,
    /**
     * Open the database in read-write mode.
     * Fails with an error if the database does not exist.
     */
    NoCreate,
    /**
     * Open the database in immutable mode.
     * In addition to the database being read-only,
     * no wal or journal files will be created by sqlite.
     * Use this mode if the database is on a read-only filesystem.
     * Fails with an error if the database does not exist.
     */
    Immutable,
};

struct SQLiteError;
class SQLiteStmt;
class SQLiteTxn;

/**
 * RAII wrapper to close a SQLite database automatically.
 */
class SQLite
{
    friend SQLiteError;

    struct Close {
        void operator()(sqlite3 * db);
    };
    std::unique_ptr<sqlite3, Close> db;

public:
    SQLite() = default;
    SQLite(const Path & path, SQLiteOpenMode mode = SQLiteOpenMode::Normal);

    /**
     * Disable synchronous mode, set truncate journal mode.
     */
    void isCache();

    void exec(const std::string & stmt);

    SQLiteStmt create(const std::string & stmt);

    SQLiteTxn beginTransaction();

    void setPersistWAL(bool persist);

    uint64_t getLastInsertedRowId();
    uint64_t getRowsChanged();
};

/**
 * RAII wrapper to create and destroy SQLite prepared statements.
 */
class SQLiteStmt
{
    friend SQLite;

    struct Finalize {
        SQLiteStmt * parent;
        void operator()(sqlite3_stmt * stmt);
    };

    sqlite3 * db = 0;
    std::unique_ptr<sqlite3_stmt, Finalize> stmt;
    std::string sql;

    SQLiteStmt(sqlite3 * db, const std::string & sql);

public:
    SQLiteStmt() = default;

    /**
     * Helper for binding / executing statements.
     */
    class Use
    {
        friend class SQLiteStmt;
    private:
        SQLiteStmt & stmt;
        unsigned int curArg = 1;
        Use(SQLiteStmt & stmt);

    public:

        ~Use();

        /**
         * Bind the next parameter.
         */
        Use & operator () (std::string_view value, bool notNull = true);
        Use & operator () (const unsigned char * data, size_t len, bool notNull = true);
        Use & operator () (int64_t value, bool notNull = true);
        Use & bind(); // null

        /**
         * Execute a statement that does not return rows.
         */
        void exec();

        /**
         * For statements that return 0 or more rows. Returns true iff
         * a row is available.
         */
        bool next();

        std::string getStr(int col);
        std::optional<std::string> getStrNullable(int col);
        int64_t getInt(int col);
        bool isNull(int col);
    };

    Use use()
    {
        return Use(*this);
    }
};

/**
 * RAII helper that ensures transactions are aborted unless explicitly
 * committed.
 */
class SQLiteTxn
{
    friend SQLite;

    struct Rollback {
        void operator()(sqlite3 * db);
    };
    std::unique_ptr<sqlite3, Rollback> db;

    explicit SQLiteTxn(sqlite3 * db);

public:
    void commit();
};


struct SQLiteError : Error
{
    friend SQLite;
    friend SQLiteStmt;
    friend SQLiteTxn;

    std::string path;
    std::string errMsg;
    int errNo, extendedErrNo, offset;

    SQLiteError(const char *path, const char *errMsg, int errNo, int extendedErrNo, int offset, HintFmt && hf);

protected:

    template<typename... Args>
    SQLiteError(const char *path, const char *errMsg, int errNo, int extendedErrNo, int offset, const std::string & fs, const Args & ... args)
      : SQLiteError(path, errMsg, errNo, extendedErrNo, offset, HintFmt(fs, args...))
    { }

    template<typename... Args>
    [[noreturn]] static void throw_(sqlite3 * db, const std::string & fs, const Args & ... args) {
        throw_(db, HintFmt(fs, args...));
    }

    [[noreturn]] static void throw_(sqlite3 * db, HintFmt && hf);

};

MakeError(SQLiteBusy, SQLiteError);

void handleSQLiteBusy(const SQLiteBusy & e, time_t & nextWarning);

/**
 * Convenience function for retrying a SQLite transaction when the
 * database is busy.
 */
template<typename F>
auto retrySQLite(F && fun)
{
    time_t nextWarning = time(0) + 1;

    while (true) {
        try {
            return fun();
        } catch (SQLiteBusy & e) {
            handleSQLiteBusy(e, nextWarning);
        }
    }
}

}
