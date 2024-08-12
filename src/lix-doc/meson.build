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
