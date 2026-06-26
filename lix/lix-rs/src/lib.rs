extern crate lix_doc;

mod generated {
    include!(concat!(env!("OUT_DIR"), "/generated.rs"));
}

mod ffi {
    use std::{
        error,
        fmt::{Debug, Display},
        slice::from_raw_parts,
    };

    pub unsafe fn from_raw_parts_u8<'a>(data: *const u8, length: usize) -> &'a [u8] {
        from_raw_parts(data, length)
    }

    pub(crate) use crate::generated::cpp::Error;

    impl Display for Error {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.write_str(&self.to_string())
        }
    }

    impl Debug for Error {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.write_str(&self.to_string())
        }
    }

    impl error::Error for Error {}
}

mod ffi_test;
