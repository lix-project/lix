//! This is a hack that forces Rust to link all the Lix libs as a single static
//! library for usage when building Lix itself in static mode.
//!
//! The reason for this is that Rust does not support linking multiple
//! staticlibs into one executable, as it will jam a libstd into every single
//! one of them. This is ridiculously goofy because it would be trivially
//! solved by linking libstd separately.
//!
//! https://github.com/rust-lang/rust/issues/44322
//!
//! It re-exports all the symbols that should be exported to C++.
pub use lix_doc::*;
pub use lixutil_rs::*;
