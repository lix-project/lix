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

# FIXME: not using the pkg-config module because it creates way too many deps
# while meson migration is in progress, and we want to not include boost here
configure_file(
  input : 'nix-main.pc.in',
  output : 'nix-main.pc',
  install_dir : libdir / 'pkgconfig',
  configuration : {
    'prefix' : prefix,
    'libdir' : libdir,
    'includedir' : includedir,
    'PACKAGE_VERSION' : meson.project_version(),
  },
)
