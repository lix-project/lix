nix3_cli_files = custom_target(
  'command-ref-new-cli',
  command : nix_eval_for_docs + [
    '--write-to', '@OUTPUT@',
    '--expr',
    'import @INPUT1@ true (builtins.readFile @INPUT0@)',
  ],
  input : [
    nix3_cli_json,
    files(
      meson.project_source_root() / 'doc/manual/generate-manpage.nix',
      meson.project_source_root() / 'doc/manual/utils.nix',
    ),
  ],
  output : 'new-cli',
)
