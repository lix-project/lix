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
    liblixexpr,
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
