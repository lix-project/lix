store_xs_cpp = custom_target(
  input : 'Store.xs',
  output : 'Store.cc',
  command : [
    xsubpp,
    '@INPUT@',
    '-output',
    '@OUTPUT@',
  ],
  build_by_default : true,
)

soname_args = []
if cxx.get_linker_id() in ['ld.bfd', 'ld.gold']
  soname_args = ['-Wl,-soname=Store.so']
endif

perl_libstore = shared_module(
  'Store',
  store_xs_cpp,
  # This library does NOT get the normal libprefix. it's just `Store.so`, not `libStore.so`.
  name_prefix : '',
  dependencies : [
    libstore,
    sodium,
    perl_include,
  ],
  link_args : [
    # Nix doesn't provide a pkg-config file for libutil.
    '-llixutil',
    soname_args,
  ],
  install : true,
  install_dir : perl_libdir / 'auto/Nix/Store',
)

config_pm = configure_file(
  input : 'Config.pm.in',
  output : 'Config.pm',
  configuration : {
    'PACKAGE_VERSION': meson.project_version(),
  },
)

nix_perl_sources = files(
  'Store.pm',
  'Manifest.pm',
  'SSH.pm',
  'CopyClosure.pm',
  'Utils.pm',
)

install_data(
  nix_perl_sources,
  config_pm,
  install_dir : perl_libdir / 'Nix',
  preserve_path : true,
)
