builtins_md = custom_target(
  command : nix_eval_for_docs + [
    '--expr',
    'import @INPUT0@ (builtins.fromJSON (builtins.readFile @INPUT1@)).builtins',
  ],
  capture : true,
  input : [
    '../../generate-builtins.nix',
    language_json,
  ],
  output : 'builtins.md',
  env : nix_env_for_docs,
)

builtin_constants_md = custom_target(
  command : nix_eval_for_docs + [
    '--expr',
    'import @INPUT0@ (builtins.fromJSON (builtins.readFile @INPUT1@)).constants',
  ],
  capture : true,
  input : [
    '../../generate-builtin-constants.nix',
    language_json,
  ],
  output : 'builtin-constants.md',
  env : nix_env_for_docs,
)
