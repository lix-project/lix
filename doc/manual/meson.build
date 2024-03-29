nix_env_for_docs = {
  'HOME': '/dummy',
  'NIX_CONF_DIR': '/dummy',
  'NIX_SSL_CERT_FILE': '/dummy/no-ca-bundle.crt',
  'NIX_STATE_DIR': '/dummy',
  'NIX_CONFIG': 'cores = 0',
}

nix_for_docs = [ nix, '--experimental-features', 'nix-command' ]
nix_eval_for_docs = nix_for_docs + [
  'eval',
  '-I', 'nix/corepkgs=corepkgs',
  '--store', 'dummy://',
  '--impure',
  '--raw',
]

nix_conf_file_json = custom_target(
  command : nix_for_docs + [ 'show-config', '--json' ],
  capture : true,
  output : 'conf-file.json',
  env : nix_env_for_docs,
)

nix_conf_file_md_body = custom_target(
  command : nix_eval_for_docs + [
    '--expr',
    '(import @INPUT0@).showSettings { inlineHTML = true; } (builtins.fromJSON (builtins.readFile @INPUT1@))',
  ],
  capture : true,
  input : [
    'utils.nix',
    nix_conf_file_json,
  ],
  output : 'conf-file.md.body',
  env : nix_env_for_docs,
)

nix_conf_file_md = custom_target(
  command : [ 'cat', '@INPUT@' ],
  capture : true,
  input : [
    'src/command-ref/conf-file-prefix.md',
    nix_conf_file_md_body,
  ],
  output : 'conf-file.md',
)

nix_exp_features_json = custom_target(
  command : [ nix, '__dump-xp-features' ],
  capture : true,
  output : 'xp-features.json',
)
nix_exp_feature_shortlist = custom_target(
  command : nix_eval_for_docs + [
    '--expr',
    'import @INPUT0@ (builtins.fromJSON (builtins.readFile @INPUT1@))',
  ],
  input : [
    'generate-xp-features-shortlist.nix',
    nix_exp_features_json,
  ],
  output : 'experimental-features-shortlist.md',
  capture : true,
  env : nix_env_for_docs,
)

nix_nested_manpages = [
  [ 'nix-env',
    [
      'delete-generations',
      'install',
      'list-generations',
      'query',
      'rollback',
      'set-flag',
      'set',
      'switch-generation',
      'switch-profile',
      'uninstall',
      'upgrade',
    ],
  ],
  [ 'nix-store',
    [
      'add-fixed',
      'add',
      'delete',
      'dump-db',
      'dump',
      'export',
      'gc',
      'generate-binary-cache-key',
      'import',
      'load-db',
      'optimise',
      'print-env',
      'query',
      'read-log',
      'realise',
      'repair-path',
      'restore',
      'serve',
      'verify',
      'verify-path',
    ],
  ],
]

foreach command : nix_nested_manpages
  foreach page : command[1]
    custom_target(
      command : [
        './render-manpage.sh',
        '--unescape-dashes',
        command[0] + ' --' + page,
        '1',
        '@INPUT@',
        '@OUTPUT@.tmp',
        '@OUTPUT@',
      ],
      input : 'src/command-ref' / command[0] / (page + '.md'),
      output : command[0] + '-' + page + '.1',
      install : true,
      install_dir : mandir / 'man1',
    )
  endforeach
endforeach

nix3_cli_json = custom_target(
  command : [ nix, '__dump-cli' ],
  capture : true,
  output : 'nix.json',
)
nix3_cli_files = custom_target(
  command : nix_eval_for_docs + [
    '--write-to', '@OUTPUT@',
    '--expr',
    'import @INPUT1@ true (builtins.readFile @INPUT0@)',
  ],
  input : [
    nix3_cli_json,
    'generate-manpage.nix',
    'utils.nix',
  ],
  output : 'new-cli',
)

nix3_manpages = [
  'nix3-build',
  'nix3-bundle',
  'nix3-copy',
  'nix3-daemon',
  'nix3-derivation-add',
  'nix3-derivation',
  'nix3-derivation-show',
  'nix3-develop',
  'nix3-doctor',
  'nix3-edit',
  'nix3-eval',
  'nix3-flake-archive',
  'nix3-flake-check',
  'nix3-flake-clone',
  'nix3-flake-info',
  'nix3-flake-init',
  'nix3-flake-lock',
  'nix3-flake',
  'nix3-flake-metadata',
  'nix3-flake-new',
  'nix3-flake-prefetch',
  'nix3-flake-show',
  'nix3-flake-update',
  'nix3-fmt',
  'nix3-hash-file',
  'nix3-hash',
  'nix3-hash-path',
  'nix3-hash-to-base16',
  'nix3-hash-to-base32',
  'nix3-hash-to-base64',
  'nix3-hash-to-sri',
  'nix3-help',
  'nix3-help-stores',
  'nix3-key-convert-secret-to-public',
  'nix3-key-generate-secret',
  'nix3-key',
  'nix3-log',
  'nix3-nar-cat',
  'nix3-nar-dump-path',
  'nix3-nar-ls',
  'nix3-nar',
  'nix3-path-info',
  'nix3-print-dev-env',
  'nix3-profile-diff-closures',
  'nix3-profile-history',
  'nix3-profile-install',
  'nix3-profile-list',
  'nix3-profile',
  'nix3-profile-remove',
  'nix3-profile-rollback',
  'nix3-profile-upgrade',
  'nix3-profile-wipe-history',
  'nix3-realisation-info',
  'nix3-realisation',
  'nix3-registry-add',
  'nix3-registry-list',
  'nix3-registry',
  'nix3-registry-pin',
  'nix3-registry-remove',
  'nix3-repl',
  'nix3-run',
  'nix3-search',
  'nix3-shell',
  'nix3-show-config',
  'nix3-store-add-file',
  'nix3-store-add-path',
  'nix3-store-cat',
  'nix3-store-copy-log',
  'nix3-store-copy-sigs',
  'nix3-store-delete',
  'nix3-store-diff-closures',
  'nix3-store-dump-path',
  'nix3-store-gc',
  'nix3-store-ls',
  'nix3-store-make-content-addressed',
  'nix3-store',
  'nix3-store-optimise',
  'nix3-store-path-from-hash-part',
  'nix3-store-ping',
  'nix3-store-prefetch-file',
  'nix3-store-repair',
  'nix3-store-sign',
  'nix3-store-verify',
  'nix3-upgrade-nix',
  'nix3-why-depends',
  'nix',
]

foreach page : nix3_manpages
  custom_target(
    command : [
      './render-manpage.sh',
      page,
      '1',
      '@INPUT0@/' + page + '.md',
      '@OUTPUT@.tmp',
      '@OUTPUT@',
    ],
    input : nix3_cli_files,
    output : page + '.1',
    install : true,
    install_dir : mandir / 'man1',
  )
endforeach

nix_manpages = [
  [ 'nix-env', 1 ],
  [ 'nix-store', 1 ],
  [ 'nix-build', 1 ],
  [ 'nix-shell', 1 ],
  [ 'nix-instantiate', 1 ],
  [ 'nix-collect-garbage', 1 ],
  [ 'nix-prefetch-url', 1 ],
  [ 'nix-channel', 1 ],
  [ 'nix-hash', 1 ],
  [ 'nix-copy-closure', 1 ],
  [ 'nix.conf', 5, nix_conf_file_md, nix_exp_feature_shortlist ],
  [ 'nix-daemon', 8 ],
  [ 'nix-profiles', 5, 'src/command-ref/files/profiles.md', nix_exp_feature_shortlist ],
]

foreach entry : nix_manpages
  custom_target(
    command : [
      './render-manpage.sh',
      entry[0],
      entry[1].to_string(),
      '@INPUT0@',
      '@OUTPUT@.tmp',
      '@OUTPUT@',
      meson.current_build_dir(),
    ],
    input : [
      entry.get(2, 'src/command-ref' / (entry[0] + '.md')),
      entry.get(3, []),
    ],
    output : '@0@.@1@'.format(entry[0], entry[1]),
    install : true,
    install_dir : mandir / 'man@0@'.format(entry[1]),
  )
endforeach
