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
  'nixfetchers',
  libfetchers_sources,
  dependencies : [
    liblixstore,
    liblixutil,
  ],
  install : true,
  # FIXME(Qyriad): is this right?
  install_rpath : libdir,
)

install_headers(libfetchers_headers, subdir : 'nix', preserve_path : true)

liblixfetchers = declare_dependency(
  include_directories : include_directories('.'),
  link_with : libfetchers,
)
