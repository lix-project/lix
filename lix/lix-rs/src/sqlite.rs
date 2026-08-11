use std::{
    fmt::Debug,
    path::{Path, PathBuf},
    sync::{
        mpsc::{self, Receiver, Sender},
        Arc, Mutex,
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};

use either::Either::{self, Left, Right};
use futures::channel::oneshot;
use rootcause::{markers::Dynamic, prelude::*};
use rusqlite::{
    trace::{TraceEvent, TraceEventCodes},
    InterruptHandle, OpenFlags,
};

use crate::errors::FactorReport;

/// error type for [`Connection::call_with_retries`].
#[derive(Debug, thiserror::Error)]
pub enum RetryError<C: ?Sized + 'static = Dynamic> {
    /// retry the call from the beginning.
    ///
    /// sqlite errors with error code [`rusqlite::ErrorCode::DatabaseBusy`] and
    /// [`rusqlite::ErrorCode::FileLockingProtocolFailed`] convert to this when
    /// passed through [`From::from`] or the `?` operator.
    Retry,
    /// non-retryable sqlite error.
    Sqlite(Report<rusqlite::Error>),
    /// non-retryable user error.
    Error(#[from] Report<C>),
}

fn should_retry(e: &rusqlite::Error) -> bool {
    use rusqlite::ErrorCode::*;
    match e {
        rusqlite::Error::SqliteFailure(e, _) => e.code == DatabaseBusy || e.code == FileLockingProtocolFailed,
        _ => false,
    }
}

impl<C: ?Sized> From<rusqlite::Error> for RetryError<C> {
    #[track_caller]
    fn from(value: rusqlite::Error) -> Self {
        match should_retry(&value) {
            true => RetryError::Retry,
            false => RetryError::Sqlite(report!(value)),
        }
    }
}

impl<C> From<Report<C>> for RetryError {
    fn from(value: Report<C>) -> Self {
        Self::Error(value.into())
    }
}

enum Request {
    Execute(Box<dyn FnOnce(&mut rusqlite::Connection) + Send + 'static>),
}

/// async wrapper for [`rusqlite::Connection`]s.
///
/// all sqlite operations are handled in a dedicated worker thread per connection.
/// async operations on open connections are cancellable and will either interrupt
/// a running operation when cancelled if it has been started already, or mark the
/// queued operation as no-execute if it has not been started yet.
pub struct Connection {
    path: Option<PathBuf>,
    interrupt_handle: InterruptHandle,
    sender: Sender<Request>,
    thread: Mutex<Option<JoinHandle<()>>>,
}

struct InterruptOnDrop<'a>(&'a Connection);

impl<'a> Drop for InterruptOnDrop<'a> {
    fn drop(&mut self) {
        if !thread::panicking() {
            self.0.interrupt_handle.interrupt();
        }
    }
}

impl Debug for Connection {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Connection").field("path", &self.path).finish()
    }
}

/// how to open an sqlite database.
#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq, Default)]
pub enum Mode {
    /// open for read-write operations, creating the database if it doesn't exist yet.
    #[default]
    Normal,
    /// open for read-write operations, but do not create the database file.
    NoCreate,
    /// open read-only and with the sqlite `immutable` flag set.
    Immutable,
}

#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq, Default)]
pub struct OpenArgs<'a> {
    /// database file to open. an in-memory database will be used if this is [`None`].
    pub path: Option<&'a Path>,
    /// whether to enable write-ahead logging in sqlite. setting this to `false` also
    /// forces the `unix-dotfile` vfs to be used for the database for legacy reasons:
    /// WAL mode and file-locking vfs modes are unsuitable for NFS and the WSL compat
    /// layer implementations, those should always set use_wal to `false` for safety.
    ///
    /// [`OpenArgs::is_a_cache`] takes precendence over this for historical reasons.
    pub use_wal: bool,
    pub mode: Mode,
    /// whether to also send all executed sqlite statements to the logs.
    pub trace_sql: bool,
    /// whether this database is a cache. cache databases have reduced durability guarantees.
    pub is_a_cache: bool,
}

impl<'a> OpenArgs<'a> {
    /// render to an sqlite database uri and flags for `sqlite3_open_v2`.
    fn build_sqlite_uri_and_flags(&self) -> (String, OpenFlags) {
        let (uri, flags) = match self.path {
            None => (":memory:".into(), OpenFlags::default()),
            Some(path) => {
                // allow a few non-alphanumeric characters to make paths more readable in error messages
                const NON_PATH: percent_encoding::AsciiSet = percent_encoding::NON_ALPHANUMERIC
                    .remove(b'/')
                    .remove(b'-')
                    .remove(b'.')
                    .remove(b'_');
                let base = percent_encoding::percent_encode(path.as_os_str().as_encoded_bytes(), &NON_PATH);
                let mut uri_args = vec![];
                if !self.use_wal {
                    uri_args.push("vfs=unix-dotfile");
                }
                if self.mode == Mode::Immutable {
                    uri_args.push("immutable=1");
                }
                let flags = match self.mode {
                    Mode::Normal => OpenFlags::SQLITE_OPEN_READ_WRITE | OpenFlags::SQLITE_OPEN_CREATE,
                    Mode::NoCreate => OpenFlags::SQLITE_OPEN_READ_WRITE,
                    Mode::Immutable => OpenFlags::SQLITE_OPEN_READ_ONLY,
                };
                (format!("file:{base}?{}", uri_args.join("&")), flags)
            }
        };
        (uri, flags | OpenFlags::SQLITE_OPEN_URI)
    }
}

impl Connection {
    /// open a database. automatically applies a number of default settings
    /// according to `args` and enables foreign key enforcement.
    pub async fn open(args: OpenArgs<'_>) -> Result<Self, Report<rusqlite::Error>> {
        let (uri, flags) = args.build_sqlite_uri_and_flags();
        let conn = rusqlite::Connection::open_with_flags(uri, flags)?;

        if args.is_a_cache {
            conn.pragma_update(None, "synchronous", "off")?;
            conn.pragma_update(Some("main"), "journal_mode", "truncate")?;
        } else if args.use_wal {
            conn.pragma_update(Some("main"), "journal_mode", "wal")?;
        }
        conn.pragma_update(None, "foreign_keys", 1)?;
        if args.trace_sql {
            conn.trace_v2(TraceEventCodes::SQLITE_TRACE_STMT, Some(Self::trace_statement));
        }

        let (sender, receiver) = mpsc::channel::<Request>();
        // our busy timeout report loop has 1 second granularity
        conn.busy_timeout(Duration::from_secs(1))?;
        let interrupt_handle = conn.get_interrupt_handle();
        let thread = thread::Builder::new()
            .name(format!("sqlite thread for {:?}", args.path.map(|p| p.display())))
            .spawn(move || Self::thread_main(conn, receiver))
            .expect("creating sqlite thread");

        Ok(Self {
            path: args.path.map(|p| p.to_owned()),
            interrupt_handle,
            sender,
            thread: Mutex::new(Some(thread)),
        })
    }

    fn thread_main(mut conn: rusqlite::Connection, receiver: Receiver<Request>) {
        while let Ok(message) = receiver.recv() {
            match message {
                Request::Execute(f) => f(&mut conn),
            }
        }
    }

    fn trace_statement(ev: TraceEvent) {
        if let TraceEvent::Stmt(stmt, _) = ev {
            // try to be more helpful by also showing parameters. having only statements
            // with no parameters is remarkably less useful for debugging any sql errors
            print_notice!(
                "SQL<{}>",
                stmt.expanded_sql().as_deref().unwrap_or(stmt.sql().as_ref())
            )
        }
    }

    fn thread_op_failed(&self) -> ! {
        #[allow(clippy::unwrap_used, reason = "mutex is never read elsewhere")]
        let info = self.thread.lock().unwrap().take().and_then(|t| t.join().err());
        match info {
            Some(err) => std::panic::resume_unwind(err),
            None => panic!("sqlite thread panic already consumed elsewhere"),
        }
    }

    /// asynchronously execute a function on the wrapped sqlite connection.
    ///
    /// errors are passed through verbatim. if the call is cancelled before the
    /// operation starts then the operation will be skipped, if it is cancelled
    /// after the operation has started then it will be interrupted instead.
    pub async fn call<R, E>(
        &self,
        function: impl FnOnce(&mut rusqlite::Connection) -> Result<R, E> + 'static + Send,
    ) -> Result<R, E>
    where
        R: Send + 'static,
        E: Send + 'static,
    {
        let (sender, receiver) = oneshot::channel();

        let _interrupt_on_drop = InterruptOnDrop(self);

        self.sender
            .send(Request::Execute(Box::new(move |conn| {
                if !sender.is_canceled() {
                    let value = function(conn);
                    let _ = sender.send(value);
                }
            })))
            .unwrap_or_else(|_| self.thread_op_failed());

        receiver.await.unwrap_or_else(|_| self.thread_op_failed())
    }

    /// asynchronously execute a function on the wrapped sqlite connection, with retries.
    ///
    /// if the function fails because the database was busy *and* the failure was returned
    /// as a [`RetryError::Retry`] (e.g. by the `?` operation or converting the sqlite error
    /// with [`From`]) then the operation will be retried later after an unspecified time of
    /// waiting for contention to resolve. all other [`RetryError`] variants are fatal.
    ///
    /// if the call is cancelled before the operation starts then the operation
    /// will be skipped, if it is cancelled after the operation has started then
    /// it will be interrupted instead.
    pub async fn call_with_retries<R, C>(
        &self,
        action: impl Fn(&mut rusqlite::Connection) -> Result<R, RetryError<C>> + Send + Sync + 'static,
    ) -> Result<R, <Either<Report<rusqlite::Error>, Report<C>> as FactorReport>::Factored>
    where
        R: Send + 'static,
        C: ?Sized + 'static,
        Either<Report<rusqlite::Error>, Report<C>>: FactorReport,
    {
        let mut next_warning = Instant::now() + Duration::from_secs(1);
        let action = Arc::new(action);
        loop {
            let print_warning = match Instant::now() {
                now if next_warning < now => {
                    next_warning = now + Duration::from_secs(10);
                    true
                }
                _ => false,
            };
            let action = Arc::clone(&action);
            let result = self
                .call(move |conn| {
                    if print_warning {
                        log_warning!("database {} is busy", conn.path().unwrap_or("(in-memory)"));
                    }
                    action(conn)
                })
                .await;
            match result {
                Ok(o) => return Ok(o),
                Err(RetryError::Retry) => {
                    // just retry, and maybe let a non-blocking action complete first
                }
                Err(RetryError::Sqlite(e)) => return Err(Left(e)).factor_report(),
                Err(RetryError::Error(e)) => return Err(Right(e)).factor_report(),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use std::{
        ops::Deref,
        path::PathBuf,
        sync::{atomic::AtomicI32, Barrier},
    };

    use super::*;
    use rstest::*;
    use rusqlite::functions::FunctionFlags;

    struct TestPath(#[allow(unused)] temp_testdir::TempDir, PathBuf);

    impl Deref for TestPath {
        type Target = PathBuf;

        fn deref(&self) -> &Self::Target {
            &self.1
        }
    }

    #[fixture]
    fn path() -> TestPath {
        let dir = temp_testdir::TempDir::default();
        let path = dir.join("test.sqlite");
        TestPath(dir, path)
    }

    async fn open(
        path: &Path,
        in_memory: bool,
        use_wal: bool,
        is_a_cache: bool,
    ) -> Result<Connection, Report<rusqlite::Error>> {
        let args = OpenArgs {
            path: (!in_memory).then_some(path),
            use_wal,
            is_a_cache,
            mode: Mode::Normal,
            trace_sql: false,
        };
        Connection::open(args).await
    }

    #[rstest]
    #[tokio::test]
    async fn open_normal(path: TestPath) {
        let db = open(&path, false, false, false).await.unwrap();
        db.call(|c| c.execute("create table foo(text)", ()))
            .await
            .unwrap();
    }

    #[rstest]
    #[tokio::test]
    async fn open_no_create(path: TestPath) {
        assert_matches!(
            Connection::open(OpenArgs {
                path: Some(&path),
                mode: Mode::NoCreate,
                ..Default::default()
            })
                .await,
            Err(e) if match e.current_context() {
                rusqlite::Error::SqliteFailure(e, _) => e.code == rusqlite::ErrorCode::CannotOpen,
                _ => false,
            }
        );

        open(&path, false, false, false).await.unwrap();
        let db = Connection::open(OpenArgs {
            path: Some(&path),
            mode: Mode::NoCreate,
            ..Default::default()
        })
        .await
        .unwrap();
        db.call(|c| c.execute("create table foo(text)", ()))
            .await
            .unwrap();
    }

    #[rstest]
    #[tokio::test]
    async fn flags_set_correctly(
        path: TestPath,
        #[values(false, true)] in_memory: bool,
        #[values(false, true)] use_wal: bool,
        #[values(false, true)] is_a_cache: bool,
    ) {
        let db = open(&path, in_memory, use_wal, is_a_cache).await.unwrap();
        assert_eq!(
            db.call(|c| c.pragma_query_value(Some("main"), "journal_mode", |r| r.get::<_, String>(0)))
                .await
                .unwrap(),
            match () {
                _ if in_memory => "memory",
                _ if is_a_cache => "truncate",
                _ if use_wal => "wal",
                _ => "delete",
            }
        );
        if is_a_cache {
            assert_eq!(
                db.call(|c| c.pragma_query_value(None, "synchronous", |r| r.get::<_, i32>(0)))
                    .await
                    .unwrap(),
                0
            );
        }
    }

    #[rstest]
    #[tokio::test]
    #[ignore]
    async fn result_conversions(path: TestPath) {
        // this is a test that things actually compile

        let db = open(&path, false, true, false).await.unwrap();

        // call passes the error type unchanged
        let _: Result<String, rusqlite::Error> = db
            .call(|c| c.pragma_query_value(None, "journal_mode", |f| f.get::<_, String>(0)))
            .await;
        // but needs help sometimes, of course
        let _: Result<(), Report> = db
            .call::<_, Report>(|c| {
                c.pragma_query_value(None, "journal_mode", |f| f.get::<_, String>(0))?;
                Ok(())
            })
            .await;

        // call_with_retries merges context types appropriately, ie to Dynamic if requested
        let _: Result<(), Report> = db
            .call_with_retries::<_, Dynamic>(|c| {
                c.pragma_query_value(None, "journal_mode", |f| f.get::<_, String>(0))?;
                Ok(())
            })
            .await;
        let _: Result<(), Report<Either<rusqlite::Error, i32>>> = db
            .call_with_retries::<_, i32>(|c| {
                c.pragma_query_value(None, "journal_mode", |f| f.get::<_, String>(0))?;
                Ok(())
            })
            .await;
        // report types interconvert
        let _: Result<(), Report> = db
            .call_with_retries::<_, Dynamic>(|_| {
                Err(report!("test").context("&str"))?;
                Ok(())
            })
            .await;
    }

    #[tokio::test]
    async fn retry() {
        let db = open("".as_ref(), true, false, false).await.unwrap();

        fn database_busy() -> Result<(), rusqlite::Error> {
            Err(rusqlite::Error::SqliteFailure(
                rusqlite::ffi::Error {
                    code: rusqlite::ErrorCode::DatabaseBusy,
                    extended_code: 0,
                },
                None,
            ))
        }

        fn lock_protocol_failed() -> Result<(), rusqlite::Error> {
            Err(rusqlite::Error::SqliteFailure(
                rusqlite::ffi::Error {
                    code: rusqlite::ErrorCode::FileLockingProtocolFailed,
                    extended_code: 0,
                },
                None,
            ))
        }

        // retryable errors that pass through directly should retry
        let round = AtomicI32::new(0);
        let rounds = db
            .call_with_retries::<_, Dynamic>(move |_| {
                let x = round.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
                match x {
                    // retry with Retry first to be sure
                    0 => return Err(RetryError::Retry),
                    // SQLITE_BUSY and SQLITE_PROTOCOL should retry
                    1 => database_busy()?,
                    2 => lock_protocol_failed()?,
                    last => return Ok(last),
                }
                panic!("round {x} failed");
            })
            .await
            .unwrap();
        assert_eq!(rounds, 3);

        // retryable errors that are converted to reports should fail
        db.call_with_retries::<_, Dynamic>(|_| database_busy().map_err(|e| RetryError::Sqlite(e.into())))
            .await
            .unwrap_err();
        db.call_with_retries::<_, Dynamic>(|_| {
            lock_protocol_failed().map_err(|e| RetryError::Sqlite(e.into()))
        })
        .await
        .unwrap_err();
        db.call_with_retries::<_, Dynamic>(|_| Ok(database_busy().context("report")?))
            .await
            .unwrap_err();
        db.call_with_retries::<_, Dynamic>(|_| Ok(lock_protocol_failed().context("report")?))
            .await
            .unwrap_err();
    }

    #[tokio::test]
    async fn cancel_unstarted() {
        let db = Arc::new(Connection::open(OpenArgs::default()).await.unwrap());

        let b = Arc::new(Barrier::new(2));

        // block the db
        let block = tokio::spawn({
            let b = Arc::clone(&b);
            let db = Arc::clone(&db);
            async move { db.call::<_, ()>(move |_| Ok(b.wait())).await }
        });

        // spawn a waiter and ensure its command has started. another channel signals that the
        // function it wants to run has been called destroyed.
        let (wait_send, wait_recv) = oneshot::channel();
        let (called_send, called_recv) = oneshot::channel();
        let wait = tokio::spawn({
            struct Called(Option<oneshot::Sender<bool>>);
            impl Drop for Called {
                fn drop(&mut self) {
                    let _ = self.0.take().map(|s| s.send(false));
                }
            }
            let mut called = Called(Some(called_send));
            let db = db.clone();
            async move {
                wait_send.send(()).unwrap();
                db.call::<_, ()>(move |_| Ok(called.0.take().map(|s| s.send(true).unwrap())))
                    .await
            }
        });
        wait_recv.await.unwrap();
        wait.abort();
        let _ = wait.await;

        // clear the blocker
        b.wait();
        let _ = block.await;

        // the waiter should not have been called since the future was dropped
        assert!(!called_recv.await.unwrap());
    }

    #[rstest]
    #[tokio::test]
    async fn cancel_running(path: TestPath) {
        let args = OpenArgs {
            path: Some(path.as_ref()),
            use_wal: true,
            ..Default::default()
        };
        let db = Arc::new(Connection::open(args).await.unwrap());

        let b = Arc::new(Barrier::new(2));

        // we use a custom scalar function to block query execution any time the function runs.
        // the first call will be used to trigger an interrupt, the second should never run.
        let (wait_send, wait_recv) = oneshot::channel();
        let (tx_send, tx_recv) = oneshot::channel();
        let wait = tokio::task::spawn({
            let b = Arc::clone(&b);
            let db = Arc::clone(&db);
            async move {
                db.call::<_, ()>(move |c| {
                    c.create_scalar_function("block", 1, FunctionFlags::empty(), {
                        let b = Arc::clone(&b);
                        let called = AtomicI32::new(0);
                        move |_| {
                            if called.fetch_add(1, std::sync::atomic::Ordering::SeqCst) == 0 {
                                b.wait();
                                b.wait();
                            }
                            Ok(true)
                        }
                    })
                    .unwrap();
                    wait_send.send(()).unwrap();
                    tx_send
                        .send(c.query_one("select block(1) and block(2)", (), |r| r.get::<_, i32>(0)))
                        .unwrap();
                    Ok(())
                })
                .await
            }
        });
        wait_recv.await.unwrap();
        wait.abort();
        b.wait();
        let _ = wait.await;
        b.wait();
        assert_matches!(
            tx_recv.await.unwrap(),
            Err(rusqlite::Error::SqliteFailure(e, _))
                if e.code == rusqlite::ErrorCode::OperationInterrupted
        );
    }

    #[tokio::test]
    #[should_panic = "inner panic"]
    async fn callback_panic_single() {
        let db = Connection::open(OpenArgs::default()).await.unwrap();
        db.call::<(), ()>(|_| panic!("inner panic")).await.unwrap();
    }

    #[tokio::test]
    #[should_panic = "consumed elsewhere"]
    async fn callback_panic_multiple() {
        let db = Arc::new(Connection::open(OpenArgs::default()).await.unwrap());

        let op = tokio::spawn({
            let db = Arc::clone(&db);
            async move { db.call::<(), ()>(|_| panic!("inner panic")).await.unwrap() }
        });
        assert!(op.await.unwrap_err().is_panic());

        // another operation on the same db should also panic, but differently since we can only resume once
        db.call::<_, ()>(|_| Ok(())).await.unwrap();
    }
}
