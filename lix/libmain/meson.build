libmain_sources = files(
  'common-args.cc',
  'crash-handler.cc',
  'loggers.cc',
  'progress-bar.cc',
  'shared.cc',
  'stack.cc',
)

libmain_headers = files(
  'common-args.hh',
  'crash-handler.hh',
  'loggers.hh',
  'progress-bar.hh',
  'shared.hh',
)

libmain = library(
  'lixmain',
  libmain_sources,
  dependencies : [
    liblixutil,
    liblixstore,
    boost,
    kj,
  ],
  include_directories : [ '../..' ],
  cpp_pch : cpp_pch,
  install : true,
  # FIXME(Qyriad): is this right?
  install_rpath : libdir,
)

install_headers(libmain_headers, subdir : 'lix/libmain', preserve_path : true)

liblixmain = declare_dependency(
  include_directories : include_directories('../..'),
  dependencies : [
    liblixutil,
  ],
  link_with : libmain,
)
meson.override_dependency('lix-main', liblixmain)

# FIXME: not using the pkg-config module because it creates way too many deps
# while meson migration is in progress, and we want to not include boost here
configure_file(
  input : 'lix-main.pc.in',
  output : 'lix-main.pc',
  install_dir : libdir / 'pkgconfig',
  configuration : {
    'prefix' : prefix,
    'libdir' : libdir,
    'includedir' : includedir,
    'PACKAGE_VERSION' : meson.project_version(),
  },
)
