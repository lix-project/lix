libmain_sources = files(
  'common-args.cc',
  'loggers.cc',
  'progress-bar.cc',
  'shared.cc',
  'stack.cc',
)

libmain_headers = files(
  'common-args.hh',
  'loggers.hh',
  'progress-bar.hh',
  'shared.hh',
)

libmain = library(
  'nixmain',
  libmain_sources,
  dependencies : [
    liblixutil,
    liblixstore,
  ],
  install : true,
  # FIXME(Qyriad): is this right?
  install_rpath : libdir,
)

install_headers(libmain_headers, subdir : 'nix', preserve_path : true)

liblixmain = declare_dependency(
  include_directories : include_directories('.'),
  link_with : libmain,
)
