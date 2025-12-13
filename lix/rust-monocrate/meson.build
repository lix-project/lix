# This crate is what ultimately links to Lix. All other Rust crates Lix uses must go through this.

rust_monocrate_sources = files(
  'src/lib.rs',
)

rust_monocrate = static_library(
  'rust_monocrate',
  rust_abi : 'c',
  sources : rust_monocrate_sources,
  dependencies : [
    liblix_doc_internal,
    liblixutil_rs_internal,
    rnix,
    rowan,
  ],
  # If any installed target links against a static library,
  # the static library must also be installed.
  install : true,
)
