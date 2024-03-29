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
