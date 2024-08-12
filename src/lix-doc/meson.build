# Until Meson 1.5¹, we can't just give Meson a Cargo.lock file and be done with it.
# Meson will *detect* what dependencies are needed from Cargo files; it just won't
# fetch them. The Meson 1.5 feature essentially internally translates Cargo.lock entries
# to .wrap files, and that translation is incredibly straightforward, so let's just
# use a simple Python script to generate the .wrap files ourselves while we wait for
# Meson 1.5. Weirdly, it seems Meson will only detect dependencies from other
# dependency() calls, so we have to specify lix-doc's two top-level dependencies,
# rnix and rowan, manually, and then their dependencies will be recursively translated
# into more dependency() calls.
#
# When Meson translates a Cargo dependency, the string passed to `dependency()` follows
# a fixed format, which is important as the .wrap files' basenames must match the string
# passed to `dependency()` exactly.
# In Meson 1.4, this format is `$packageName-rs`. Meson 1.5 changes this to
# `$packageName-$shortenedVersionString-rs`, because of course it does, but we'll cross
# that bridge when we get there...
#
# [1]: https://github.com/mesonbuild/meson/commit/9b8378985dbdc0112d11893dd42b33b7bc8d1e62

run_command(
  python,
  meson.project_source_root() / 'meson/cargo-lock-to-wraps.py',
  meson.current_source_dir() / 'Cargo.lock',
  meson.project_source_root() / 'subprojects',
  check : true,
)

# The external crate rowan has an ambiguous pointer comparison warning, which
# we don't want to fail our whole build if werror is on.
subproject('rowan-rs', default_options : ['werror=false'])

rnix = dependency('rnix-rs')
rowan = dependency('rowan-rs')

lix_doc = static_library(
  'lix_doc',
  sources : files('src/lib.rs'),
  rust_abi : 'c',
  dependencies : [
    rowan,
    rnix,
  ],
  # If an installed static library depends on this target, then Meson will force
  # that to link with `-Wl,--whole-archive`, unless we also install this target.
  # `-Wl,--whole-archive` can cause some Problems when linking multiple nested
  # static libraries, so let's just install the damn thing.
  install : true,
)

liblix_doc = declare_dependency(
  link_with : lix_doc,
)
