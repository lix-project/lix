libcmd_sources = files(
  'built-path.cc',
  'command-installable-value.cc',
  'cmd-profiles.cc',
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
  'cmd-profiles.hh',
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

libcmd_generated_headers = [
  gen_header.process('repl-overlays.nix', preserve_path_from: meson.current_source_dir()),
]

libcmd = library(
  'lixcmd',
  libcmd_generated_headers,
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

install_headers(libcmd_headers, subdir : 'lix/libcmd', preserve_path : true)
custom_target(
  command : [ 'cp', '@INPUT@', '@OUTPUT@' ],
  input : libcmd_generated_headers,
  output : '@PLAINNAME@',
  install : true,
  install_dir : includedir / 'lix/libcmd',
)

liblixcmd = declare_dependency(
  include_directories : '.',
  link_with : libcmd,
)

# FIXME: not using the pkg-config module because it creates way too many deps
# while meson migration is in progress, and we want to not include boost here
configure_file(
  input : 'lix-cmd.pc.in',
  output : 'lix-cmd.pc',
  install_dir : libdir / 'pkgconfig',
  configuration : {
    'prefix' : prefix,
    'libdir' : libdir,
    'includedir' : includedir,
    'PACKAGE_VERSION' : meson.project_version(),
  },
)
