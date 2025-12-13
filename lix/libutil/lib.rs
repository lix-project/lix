#[repr(C)]
pub struct TestMultiplyArgs {
    pub a: u64,
    pub b: u64,
}

#[no_mangle]
pub extern "C" fn test_multiply(args: TestMultiplyArgs) -> u64 {
    args.a * args.b
}

pub mod exports {
    pub use super::test_multiply;
}
