libcmd_sources = files(
  'built-path.cc',
  'command-installable-value.cc',
  'command.cc',
  'common-eval-args.cc',
  'editor-for.cc',
  'installable-attr-path.cc',
  'installable-derived-path.cc',
  'installable-flake.cc',
  'installable-value.cc',
  'installables.cc',
  'legacy.cc',
  'markdown.cc',
  'repl.cc',
  'repl-interacter.cc',
)

libcmd_headers = files(
  'built-path.hh',
  'command-installable-value.hh',
  'command.hh',
  'common-eval-args.hh',
  'editor-for.hh',
  'installable-attr-path.hh',
  'installable-derived-path.hh',
  'installable-flake.hh',
  'installable-value.hh',
  'installables.hh',
  'legacy.hh',
  'markdown.hh',
  'repl-interacter.hh',
  'repl.hh',
)

libcmd = library(
  'nixcmd',
  libcmd_sources,
  dependencies : [
    liblixutil,
    liblixstore,
    liblixexpr,
    liblixfetchers,
    liblixmain,
    boehm,
    editline,
    lowdown,
    nlohmann_json,
    lix_doc
  ],
  install : true,
  # FIXME(Qyriad): is this right?
  install_rpath : libdir,
)

install_headers(libcmd_headers, subdir : 'nix', preserve_path : true)

liblixcmd = declare_dependency(
  include_directories : '.',
  link_with : libcmd,
)

# FIXME: not using the pkg-config module because it creates way too many deps
# while meson migration is in progress, and we want to not include boost here
configure_file(
  input : 'nix-cmd.pc.in',
  output : 'nix-cmd.pc',
  install_dir : libdir / 'pkgconfig',
  configuration : {
    'prefix' : prefix,
    'libdir' : libdir,
    'includedir' : includedir,
    'PACKAGE_VERSION' : meson.project_version(),
  },
)
