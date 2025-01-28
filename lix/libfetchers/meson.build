libfetchers_sources = files(
  'attrs.cc',
  'cache.cc',
  'fetch-settings.cc',
  'fetch-to-store.cc',
  'fetchers.cc',
  'git.cc',
  'github.cc',
  'indirect.cc',
  'mercurial.cc',
  'path.cc',
  'registry.cc',
  'tarball.cc',
)

libfetchers_headers = files(
  'attrs.hh',
  'cache.hh',
  'fetch-settings.hh',
  'fetch-to-store.hh',
  'fetchers.hh',
  'registry.hh',
)

libfetchers_setting_definitions = files(
  'settings/accept-flake-config.md',
  'settings/access-tokens.md',
  'settings/allow-dirty.md',
  'settings/commit-lockfile-summary.md',
  'settings/flake-registry.md',
  'settings/use-registries.md',
  'settings/warn-dirty.md',
)
libfetchers_settings_header = custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_settings.py',
    '--kernel', host_machine.system(),
    '--header', '@OUTPUT@',
    '--experimental-features', '@SOURCE_ROOT@/lix/libutil/experimental-features',
    '@INPUT@',
  ],
  input : libfetchers_setting_definitions,
  output : 'libfetchers-settings.gen.inc',
  install : true,
  install_dir : includedir / 'lix/libfetchers',
)

libfetchers = library(
  'lixfetchers',
  libfetchers_settings_header,
  libfetchers_sources,
  dependencies : [
    liblixstore,
    liblixutil,
    nlohmann_json,
    kj,
  ],
  include_directories : [ '../..' ],
  cpp_pch : cpp_pch,
  install : true,
  # FIXME(Qyriad): is this right?
  install_rpath : libdir,
)

install_headers(libfetchers_headers, subdir : 'lix/libfetchers', preserve_path : true)

# FIXME: not using the pkg-config module because it creates way too many deps
# while meson migration is in progress, and we want to not include boost here
configure_file(
  input : 'lix-fetchers.pc.in',
  output : 'lix-fetchers.pc',
  install_dir : libdir / 'pkgconfig',
  configuration : {
    'prefix' : prefix,
    'libdir' : libdir,
    'includedir' : includedir,
    'PACKAGE_VERSION' : meson.project_version(),
  },
)

liblixfetchers = declare_dependency(
  include_directories : include_directories('../..'),
  sources : libfetchers_settings_header,
  link_with : libfetchers,
)

# FIXME: remove when https://git.lix.systems/lix-project/lix/issues/359 is fixed.
if is_static
  liblixfetchers_mstatic = declare_dependency(
    include_directories : include_directories('../..'),
    sources : libfetchers_settings_header,
    link_whole : libfetchers,
  )
else
  liblixfetchers_mstatic = liblixfetchers
endif
meson.override_dependency('lix-fetchers', liblixfetchers_mstatic)
