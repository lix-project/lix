liblix_sources += files(
  'built-path.cc',
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

liblix_generated_headers += [
  gen_header.process('repl-overlays.nix', preserve_path_from: meson.current_source_dir()),
]

install_headers(libcmd_headers, subdir : 'lix/libcmd', preserve_path : true)
