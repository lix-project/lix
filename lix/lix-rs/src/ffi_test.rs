use std::{
    error::Error,
    future::poll_fn,
    io,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    task::Poll,
    thread::{self},
    time::Duration,
};

use rootcause::Report;

use crate::{
    ffi,
    futures::{CxxFuture, RsFuture},
};

#[derive(Clone, Copy, PartialEq, PartialOrd)]
pub struct TestMultiplyAddLenArgs {
    pub a: u64,
    pub b: u64,
    pub c: Option<u64>,
}

pub trait SetB {
    fn set_b(&mut self, b: u64);
}

impl SetB for TestMultiplyAddLenArgs {
    fn set_b(&mut self, b: u64) {
        self.b = b;
    }
}

impl TestMultiplyAddLenArgs {
    pub fn new(a: u64, b: u64) -> Self {
        Self { a, b, c: None }
    }
}

pub(crate) fn test_multiply_add_len(args: TestMultiplyAddLenArgs, s: &Vec<String>) -> (String, u64) {
    let result = args.a * args.b + s.len() as u64;
    (
        format!("({} * {} + {s:?}.len()) = {result}", args.a, args.b),
        result,
    )
}

pub(crate) fn test_result() -> Result<(), Box<dyn Error>> {
    Err(Box::new(io::Error::other("errors travel freely")))
}

pub(crate) fn test_option_some() -> Option<u64> {
    Some(1)
}

pub(crate) fn test_option_none() -> Option<u64> {
    None
}

pub(crate) fn test_exceptions(f: Box<dyn Fn() -> Result<(), ffi::Error>>) -> String {
    match f() {
        Ok(()) => "".into(),
        Err(e) => e.to_string(),
    }
}

pub(crate) fn wakes_self() -> RsFuture<i32> {
    let mut called = false;
    poll_fn(move |ctx| {
        if called {
            Poll::Ready(1)
        } else {
            called = true;
            ctx.waker().wake_by_ref();
            Poll::Pending
        }
    })
    .into()
}

pub(crate) fn wakes_from_thread(wait: u64) -> RsFuture<i32> {
    let ready = Arc::new(AtomicBool::new(false));
    poll_fn(move |ctx| {
        if ready.load(Ordering::SeqCst) {
            Poll::Ready(9001)
        } else {
            let waker = ctx.waker().clone();
            let ready = Arc::clone(&ready);
            thread::spawn(move || {
                thread::sleep(Duration::from_millis(wait));
                ready.store(true, Ordering::SeqCst);
                waker.wake();
            });
            Poll::Pending
        }
    })
    .into()
}

pub(crate) fn await_add_one(f: CxxFuture<i32>) -> RsFuture<Result<i32, Report>> {
    async move { Ok(f.await? + 1) }.into()
}
