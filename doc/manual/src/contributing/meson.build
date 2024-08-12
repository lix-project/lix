# Intermediate step for experimental-feature-descriptions.md.
# This splorks the output of generate-xp-features.nix as JSON,
# which gets written as a directory tree below.
experimental_feature_descriptions_md = custom_target(
  command : nix_eval_for_docs + [
    '--expr',
    'import @INPUT0@ "experimental" "xp" (builtins.fromJSON (builtins.readFile @INPUT1@))',
  ],
  input : [
    '../../generate-features.nix',
    nix_exp_features_json,
  ],
  capture : true,
  output : 'experimental-feature-descriptions.md',
)

deprecated_feature_descriptions_md = custom_target(
  command : nix_eval_for_docs + [
    '--expr',
    'import @INPUT0@ "deprecated" "dp" (builtins.fromJSON (builtins.readFile @INPUT1@))',
  ],
  input : [
    '../../generate-features.nix',
    dp_features_json,
  ],
  capture : true,
  output : 'deprecated-feature-descriptions.md',
)
