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

libfetchers = library(
  'lixfetchers',
  libfetchers_sources,
  dependencies : [
    liblixstore,
    liblixutil,
    nlohmann_json,
  ],
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
  include_directories : include_directories('.'),
  link_with : libfetchers,
)
