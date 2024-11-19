# Intermediate step for manpage generation.
# This splorks the output of generate-manpage.nix as JSON,
# which gets written as a directory tree below.
nix3_cli_files_json = custom_target(
  command : nix_eval_for_docs_common + [
    '--json',
    '--expr',
    'import @INPUT0@ true (builtins.readFile @INPUT1@)',
  ],
  input : [
    '../../generate-manpage.nix',
    nix3_cli_json,
  ],
  capture : true,
  output : 'new-cli.json',
  env : nix_env_for_docs,
)
nix3_cli_files = custom_target(
  command : [
    python,
    '@INPUT0@',
    '-i', '@INPUT1@',
    '-o', '@OUTPUT@',
  ],
  input : [
    '../../json-to-tree.py',
    nix3_cli_files_json,
  ],
  output : 'new-cli',
)

conf_file_md = custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_settings.py',
    '--kernel', host_machine.system(),
    '--docs', '@OUTPUT@',
    '--experimental-features', '@SOURCE_ROOT@/lix/libutil/experimental-features',
    '@INPUT@',
  ],
  input : [
    libexpr_setting_definitions,
    libfetchers_setting_definitions,
    file_transfer_setting_definitions,
    libstore_setting_definitions,
    archive_setting_definitions,
    feature_setting_definitions,
    logging_setting_definitions,
    daemon_setting_definitions,
    develop_settings_definitions,
  ],
  output : 'conf-file.md',
)
