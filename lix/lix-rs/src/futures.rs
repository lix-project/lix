use std::{
    future::Future,
    marker::PhantomPinned,
    pin::Pin,
    rc::Rc,
    sync::Mutex,
    task::{Context, Poll, RawWaker, RawWakerVTable, Waker},
};

use rootcause::Report;

pub use crate::generated::cpp::CxxPromise;
pub use crate::generated::cpp::CxxWaker;

const VTABLE: RawWakerVTable = RawWakerVTable::new(
    |clone| unsafe { RawWaker::new(CxxWaker::clone(clone), &VTABLE) },
    CxxWaker::wake,
    CxxWaker::wake_by_ref,
    CxxWaker::drop,
);

/// rust future, adapted for c++ use.
///
/// once passed to c++ this future will be polled from the thread it was passed
/// to *only, but wakers passed to poll functions may be called from any thread
/// as per rust requirements. try to *not* use this though, wakeups sent across
/// thread boundaries are extremely expensive in kj: wakeups sent from the same
/// thread take about 60 ns each whereas wakeups sent across threads take 8 µs.
pub struct RsFuture<R>(Pin<Box<dyn Future<Output = R>>>);

impl<R, Fut: Future<Output = R> + 'static> From<Fut> for RsFuture<R> {
    fn from(f: Fut) -> Self {
        Self(Box::pin(f))
    }
}

impl<R> RsFuture<R> {
    /// poll the future and return its status.
    ///
    /// for c++ interop reasons this does not return [`std::task::Poll`] values
    /// directly: the c++ side must be able to retrieve values from the future,
    /// and zngur is not currently able to move data out of fields. there is no
    /// equivalent to [`std::option::Option::take`] in [`std::task::Poll`], but
    /// since `Poll` has the same shape as `Option` we can simply use `Option`.
    pub fn poll(&mut self, w: &CxxWaker) -> Option<R> {
        // SAFETY: each Waker instance needs a reference, and the C++ side only initializes
        // its own reference. pre-initializing the reference in C++ risks memory leaks when
        // rust code decides to not create a Waker instance at all, leaking this reference.
        let waker = unsafe { Waker::new(CxxWaker::clone(w as *const _ as _), &VTABLE) };
        let mut ctx = Context::from_waker(&waker);
        match Future::poll(self.0.as_mut(), &mut ctx) {
            Poll::Pending => None,
            Poll::Ready(v) => Some(v),
        }
    }
}

enum CxxFutureStateInner<T> {
    Initial,
    Waiting(Waker),
    Ready(Result<T, Report>),
    Finished(PhantomPinned),
}

/// shared state type for c++ promises adapted to rust futures.
pub struct CxxFutureState<T>(Pin<Rc<Mutex<CxxFutureStateInner<T>>>>);

impl<T> CxxFutureState<T> {
    #[allow(clippy::new_without_default)]
    pub fn new() -> Self {
        Self(Rc::pin(Mutex::new(CxxFutureStateInner::Initial)))
    }

    pub fn add_ref(&self) -> Self {
        Self(Pin::clone(&self.0))
    }

    pub fn resolve(&self, v: Result<T, Report>) {
        let mut lock = self.0.lock().expect("broken future state");
        match *lock {
            CxxFutureStateInner::Initial => (),
            CxxFutureStateInner::Waiting(ref w) => w.wake_by_ref(),
            _ => panic!("will not re-resolve a promise"),
        }
        *lock = CxxFutureStateInner::Ready(v);
    }

    fn poll(&self, cx: &mut Context<'_>) -> Poll<Result<T, Report>> {
        let mut lock = self.0.lock().expect("broken future state");
        match *lock {
            CxxFutureStateInner::Initial => {
                *lock = CxxFutureStateInner::Waiting(cx.waker().clone());
                Poll::Pending
            }
            CxxFutureStateInner::Waiting(ref mut w) => {
                *w = cx.waker().clone();
                Poll::Pending
            }
            ref mut ready @ CxxFutureStateInner::Ready(_) => {
                match std::mem::replace(ready, CxxFutureStateInner::Finished(PhantomPinned)) {
                    CxxFutureStateInner::Ready(v) => Poll::Ready(v),
                    _ => unreachable!(),
                }
            }
            CxxFutureStateInner::Finished(_) => panic!("polled a finished promise"),
        }
    }
}

/// c++ future, adapted for rust use.
///
/// it's a future. you await it. it yields. just future things.
pub struct CxxFuture<T> {
    _promise: CxxPromise,
    state: CxxFutureState<T>,
}

impl<T> CxxFuture<T> {
    /// create a new future. should only be used from c++.
    pub(crate) unsafe fn new(_promise: CxxPromise, state: CxxFutureState<T>) -> Self {
        Self { _promise, state }
    }
}

impl<T> Future for CxxFuture<T> {
    type Output = Result<T, Report>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.state.poll(cx)
    }
}
