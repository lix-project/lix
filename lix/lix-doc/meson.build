rnix = dependency('rnix-0.12-rs')
rowan = dependency('rowan-0.15-rs')

rust = import('rust')

# This hack is required by the wombo combo of meson bugs:
# Meson does not set the soname for us: https://github.com/mesonbuild/meson/issues/13537
# Meson ignores link_args for Rust targets: https://github.com/mesonbuild/meson/issues/13538
lix_doc_rust_args = [
  # link-arg=-soname has no effect when building a static lib,
  # since the linker is never actually invoked.
  '-C', f'link-arg=-Wl,@soname_arg@,liblix_doc.@dylib_suffix@',
]

lix_doc = static_library(
  'lix_doc',
  sources : files('src/lib.rs'),
  dependencies : [
    rowan,
    rnix,
  ],
  rust_args : [
    # Empty when default_library=static
    rust_dynamic_args,
    lix_doc_rust_args,
  ],
)

# NOTE: this dependency object is only used for `rust_monocrate`
liblix_doc_internal = declare_dependency(
  link_with : lix_doc,
)

rust.test(
	'lix_doc_test',
	lix_doc,
	suite : 'check'
)
