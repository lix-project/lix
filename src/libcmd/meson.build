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