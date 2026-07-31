use std::{
    fs::File,
    future::Future,
    io,
    os::{
        fd::{AsFd, AsRawFd, BorrowedFd, OwnedFd},
        unix::thread::JoinHandleExt,
    },
    thread::{self, JoinHandle},
};

use futures::channel::oneshot;
use nix::{
    fcntl::FlockArg::{self, *},
    sys::{pthread::pthread_kill, signal::*},
};

mod seal {
    pub trait Seal {}
}
use seal::Seal;

/// asynchronous file operations that would normally block.
///
/// operations that cannot be completed immediately will be sent to another thread
/// for asynchronous completion. cancelling a waiting future interrupts the thread
/// by sending [`crate::UNSAFE_CANCELATION_SIGNAL`], refer to it for more details.
pub trait AsyncFileExt: Seal {
    /// async version of [`File::lock`].
    fn lock_async(&self) -> impl Future<Output = io::Result<()>> + Send;

    /// async version of [`File::lock_shared`].
    fn lock_shared_async(&self) -> impl Future<Output = io::Result<()>> + Send;
}

fn lock_fd(fd: impl AsFd, op: FlockArg) -> io::Result<()> {
    #[allow(
        deprecated,
        reason = "lock state is attached to file descriptions, not descriptors. nix's Flock conflates them."
    )]
    nix::fcntl::flock(fd.as_fd().as_raw_fd(), op).map_err(Into::into)
}

#[must_use = "flock threads are canceled on drop"]
struct FlockThread(Option<JoinHandle<()>>);

impl FlockThread {
    async fn new(fd: OwnedFd, op: FlockArg) -> io::Result<(Self, oneshot::Receiver<io::Result<()>>)> {
        let (start_signal, started) = oneshot::channel();
        let (done_signal, done) = oneshot::channel();
        let thread = thread::Builder::new().name("flock".into()).spawn(move || {
            let sig_result = SigSet::from(crate::UNSAFE_CANCELATION_SIGNAL).thread_unblock();
            if start_signal.send(sig_result).is_ok() && sig_result.is_ok() {
                let result = lock_fd(fd, op);
                let _ = done_signal.send(result); // if this fails the future was dropped => no problem
            }
        })?;
        started.await.map_err(io::Error::other)??;
        Ok((Self(Some(thread)), done))
    }

    fn cancel(&mut self) -> io::Result<()> {
        let thread = match self.0.take() {
            Some(thread) => thread,
            None => return Ok(()),
        };

        extern "C" fn ignore(_: std::ffi::c_int) {}
        // SAFETY: follows from the UNSAFE_CANCELATION_SIGNAL safe contract. our handlers
        // are safe, and correctness of the previous handler is up to the user to uphold.
        unsafe {
            sigaction(
                crate::UNSAFE_CANCELATION_SIGNAL,
                &SigAction::new(SigHandler::Handler(ignore), SaFlags::empty(), SigSet::empty()),
            )?;
        }
        pthread_kill(
            thread.as_pthread_t() as nix::libc::pthread_t,
            crate::UNSAFE_CANCELATION_SIGNAL,
        )?;
        thread
            .join()
            .map_err(|_| io::Error::other("flock thread panicked"))
    }
}

impl Drop for FlockThread {
    fn drop(&mut self) {
        // ignore the error, anything interesting is already reported through channels
        // TODO maybe log this somewhere?
        let _ = self.cancel();
    }
}

async fn lock_async(fd: BorrowedFd<'_>, initial: FlockArg, blocking: FlockArg) -> io::Result<()> {
    match lock_fd(fd, initial) {
        Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
            let (_thread, done) = FlockThread::new(fd.try_clone_to_owned()?, blocking).await?;
            Ok(done.await.map_err(|_| io::Error::other("flock thread died"))??)
        }
        done => done,
    }
}

impl Seal for File {}

impl AsyncFileExt for File {
    fn lock_async(&self) -> impl Future<Output = io::Result<()>> {
        lock_async(self.as_fd(), LockExclusiveNonblock, LockExclusive)
    }

    fn lock_shared_async(&self) -> impl Future<Output = io::Result<()>> {
        lock_async(self.as_fd(), LockSharedNonblock, LockShared)
    }
}

#[cfg(test)]
mod test {
    use std::{
        fs::TryLockError,
        future::Future,
        path::PathBuf,
        pin::{pin, Pin},
        sync::{Arc, Condvar, Mutex},
        task::{Context, Poll, Wake},
    };

    use temp_testdir::TempDir;

    use super::*;

    struct Waker {
        mutex: Mutex<bool>,
        cv: Condvar,
    }

    impl Waker {
        fn new() -> Self {
            Self {
                mutex: Mutex::new(false),
                cv: Condvar::new(),
            }
        }

        fn wait(&self) {
            let mut lock = self.mutex.lock().unwrap();
            while !std::mem::replace(&mut *lock, false) {
                lock = self.cv.wait(lock).unwrap();
            }
        }
    }

    impl Wake for Waker {
        fn wake(self: Arc<Self>) {
            *self.mutex.lock().unwrap() = true;
            self.cv.notify_all();
        }
    }

    fn prepare() -> (TempDir, PathBuf, File, Arc<Waker>, std::task::Waker) {
        let dir = TempDir::default();
        let path = dir.join("f.lock");
        let file = File::create(&path).unwrap();
        let waker = Arc::new(Waker::new());
        (dir, path, file, Arc::clone(&waker), waker.into())
    }

    #[test]
    fn available_exclusive_lock_succeeds_immediately() {
        let (_dir, path, file, _, waker) = prepare();
        let lock = pin!(file.lock_async());
        assert_matches!(lock.poll(&mut Context::from_waker(&waker)), Poll::Ready(Ok(())));
        let file2 = File::create(&path).unwrap();
        assert_eq!(
            super::lock_fd(&file2, FlockArg::LockExclusiveNonblock)
                .unwrap_err()
                .kind(),
            io::ErrorKind::WouldBlock
        );
    }

    #[test]
    fn available_shared_lock_succeeds_immediately() {
        let (_dir, path, file, _, waker) = prepare();
        let lock = pin!(file.lock_shared_async());
        assert_matches!(lock.poll(&mut Context::from_waker(&waker)), Poll::Ready(Ok(())));
        let file2 = File::create(&path).unwrap();
        assert_matches!(
            super::lock_fd(&file2, FlockArg::LockExclusiveNonblock),
            Err(e) if e.kind() == io::ErrorKind::WouldBlock
        );
        let lock = pin!(file2.lock_shared_async());
        assert_matches!(lock.poll(&mut Context::from_waker(&waker)), Poll::Ready(Ok(())));
    }

    fn unavailable_lock_waits(initial: FlockArg, take_shared: bool) {
        let (_dir, path, file, state, waker) = prepare();
        let file2 = File::create(&path).unwrap();

        // repeat the test a good few times to hopefully catch races
        for _round in 0..10000 {
            super::lock_fd(&file2, initial).unwrap();

            let mut lock: Pin<Box<dyn Future<Output = _>>> = if take_shared {
                Box::pin(file.lock_shared_async())
            } else {
                Box::pin(file.lock_async())
            };
            assert_matches!(
                lock.as_mut().poll(&mut Context::from_waker(&waker)),
                Poll::Pending
            );

            super::lock_fd(&file2, FlockArg::Unlock).unwrap();

            loop {
                match lock.as_mut().poll(&mut Context::from_waker(&waker)) {
                    Poll::Ready(Ok(())) => break,
                    Poll::Pending => state.wait(),
                    err => panic!("lock failed: {err:?}"),
                }
            }

            super::lock_fd(&file, FlockArg::Unlock).unwrap();
        }
    }

    #[test]
    fn unavailable_exclusive_lock_waits_ex() {
        unavailable_lock_waits(FlockArg::LockExclusiveNonblock, false);
    }

    #[test]
    fn unavailable_exclusive_lock_waits_shared() {
        unavailable_lock_waits(FlockArg::LockSharedNonblock, false);
    }

    #[test]
    fn unavailable_shared_lock_waits_ex() {
        unavailable_lock_waits(FlockArg::LockExclusiveNonblock, true);
    }

    #[test]
    fn future_drop_cancels_lock() {
        let (_dir, path, file, _, waker) = prepare();
        let file2 = File::create(&path).unwrap();
        super::lock_fd(&file2, FlockArg::LockExclusive).unwrap();

        {
            let mut lock = pin!(file.lock_async());
            assert_matches!(
                lock.as_mut().poll(&mut Context::from_waker(&waker)),
                Poll::Pending
            );
        }

        super::lock_fd(&file2, FlockArg::Unlock).unwrap();
        super::lock_fd(&file2, FlockArg::LockExclusiveNonblock).unwrap();
    }

    #[test]
    fn locks_interact() {
        // stdlib locks and our async locks are specified to interact, but
        // stdlib locks are platform-dependent. these are cheap insurance.
        let (_dir, path, file, _, _) = prepare();
        let file2 = File::create(&path).unwrap();

        // lock_fd exclusive blocks File::lock and File::lock_shared
        super::lock_fd(&file, FlockArg::LockExclusiveNonblock).unwrap();
        assert_matches!(file2.try_lock(), Err(TryLockError::WouldBlock));
        assert_matches!(file2.try_lock_shared(), Err(TryLockError::WouldBlock));
        file.unlock().unwrap();

        // File::lock block locks_fd exclusive and shared
        file.try_lock().unwrap();
        assert_matches!(
            super::lock_fd(&file2, FlockArg::LockExclusiveNonblock),
            Err(e) if e.kind() == io::ErrorKind::WouldBlock
        );
        assert_matches!(
            super::lock_fd(&file2, FlockArg::LockSharedNonblock),
            Err(e) if e.kind() == io::ErrorKind::WouldBlock
        );
        file.unlock().unwrap();

        // lock_fd shared blocks File::lock, but not File::lock_shared
        super::lock_fd(&file, FlockArg::LockSharedNonblock).unwrap();
        assert_matches!(file2.try_lock(), Err(TryLockError::WouldBlock));
        file2.try_lock_shared().unwrap();
        file.unlock().unwrap();

        // File::lock_shared block locks_fd exclusive, but not shared
        file.try_lock_shared().unwrap();
        assert_matches!(
            super::lock_fd(&file2, FlockArg::LockExclusiveNonblock),
            Err(e) if e.kind() == io::ErrorKind::WouldBlock
        );
        super::lock_fd(&file2, FlockArg::LockSharedNonblock).unwrap();
        file.unlock().unwrap();
    }
}
