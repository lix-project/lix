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

dependencies = [
  liblixstore,
  liblixutil,
  kj,
  nlohmann_json,
]

libfetchers_temp = library(
  is_static ? 'lixfetchers_temp' : 'lixfetchers',
  libfetchers_settings_header,
  libfetchers_sources,
  dependencies : dependencies,
  include_directories : [ '../..' ],
  cpp_pch : cpp_pch,
  install : not is_static,
  # FIXME(Qyriad): is this right?
  install_rpath : libdir,
)
# FIXME: remove when https://git.lix.systems/lix-project/lix/issues/359 is fixed.
# FIXME: replace by prelink when https://github.com/mesonbuild/meson/pull/14846 is widely available.
if is_static
  libfetchers_prelink = custom_target(
    'lixfetchers-prelink',
    output : 'lixfetchers-prelink.o',
    input : libfetchers_temp,
    command : [
      cxx.cmd_array(),
      '-r',
      '-o',
      '@OUTPUT@',
      is_darwin ? '-Wl,-force_load' : '-Wl,--whole-archive',
      '@INPUT@',
    ],
  )
  libfetchers = library(
    'lixfetchers',
    [libfetchers_prelink],
    dependencies : dependencies,
    install : true,
  )
else
  libfetchers = libfetchers_temp
endif

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
  dependencies : [
    liblixutil,
    liblixstore,
    kj,
  ],
  link_with : libfetchers,
)

meson.override_dependency('lix-fetchers', liblixfetchers)
