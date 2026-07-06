use std::{error::Error, io};

use crate::ffi;

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
    Err(Box::new(io::Error::new(
        io::ErrorKind::Other,
        "errors travel freely",
    )))
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
