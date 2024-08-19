experimental_features_shortlist_md = custom_target(
  command : nix_eval_for_docs + [
    '--expr',
    'import @INPUT0@ "experimental" "xp" (builtins.fromJSON (builtins.readFile @INPUT1@))',
  ],
  input : [
    '../../generate-features-shortlist.nix',
    nix_exp_features_json,
  ],
  capture : true,
  output : 'experimental-features-shortlist.md',
  env : nix_env_for_docs,
)

dp_features_json = custom_target(
  command : [nix, '__dump-dp-features'],
  capture : true,
  output : 'dp-features.json',
)

deprecated_features_shortlist_md = custom_target(
  command : nix_eval_for_docs + [
    '--expr',
    'import @INPUT0@ "deprecated" "dp" (builtins.fromJSON (builtins.readFile @INPUT1@))',
  ],
  input : [
    '../../generate-features-shortlist.nix',
    dp_features_json,
  ],
  capture : true,
  output : 'deprecated-features-shortlist.md',
  env : nix_env_for_docs,
)

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
    nix_eval_for_docs,
    '--expr',
    '(import @INPUT0@).showSettings { inlineHTML = true; } (builtins.fromJSON (builtins.readFile @INPUT1@))',
  ],
  capture : true,
  input : [
    '../../utils.nix',
    conf_file_json,
    experimental_features_shortlist_md,
    deprecated_features_shortlist_md,
  ],
  output : 'conf-file.md',
  env : nix_env_for_docs,
)
