nix_settings_headers = []

daemon_setting_definitions = files(
  'daemon-settings/allowed-users.md',
  'daemon-settings/trusted-users.md',
)
nix_settings_headers += custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_settings.py',
    '--kernel', host_machine.system(),
    '--header', '@OUTPUT@',
    '--experimental-features', '@SOURCE_ROOT@/lix/libutil/experimental-features',
    '@INPUT@',
  ],
  input : daemon_setting_definitions,
  output : 'daemon-settings.gen.inc',
)

develop_settings_definitions = files(
  'develop-settings/bash-prompt-prefix.md',
  'develop-settings/bash-prompt-suffix.md',
  'develop-settings/bash-prompt.md',
)
nix_settings_headers += custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_settings.py',
    '--kernel', host_machine.system(),
    '--header', '@OUTPUT@',
    '--experimental-features', '@SOURCE_ROOT@/lix/libutil/experimental-features',
    '@INPUT@',
  ],
  input : develop_settings_definitions,
  output : 'develop-settings.gen.inc',
)

nix_sources = files(
  # keep-sorted start
  'add-to-store.cc',
  'app.cc',
  'build.cc',
  'bundle.cc',
  'cat.cc',
  'config.cc',
  'copy.cc',
  'daemon.cc',
  'derivation-add.cc',
  'derivation-show.cc',
  'derivation.cc',
  'develop.cc',
  'diff-closures.cc',
  'doctor.cc',
  'dump-path.cc',
  'edit.cc',
  'eval.cc',
  'flake.cc',
  'fmt.cc',
  'hash.cc',
  'log.cc',
  'ls.cc',
  'make-content-addressed.cc',
  'nar.cc',
  'optimise-store.cc',
  'path-from-hash-part.cc',
  'path-info.cc',
  'ping-store.cc',
  'prefetch.cc',
  'profile.cc',
  'registry.cc',
  'repl.cc',
  'run.cc',
  'search.cc',
  'sigs.cc',
  'store-copy-log.cc',
  'store-delete.cc',
  'store-gc.cc',
  'store-repair.cc',
  'store.cc',
  'upgrade-nix.cc',
  'verify.cc',
  'why-depends.cc',
  # keep-sorted end
)

nix_headers = files(
  # keep-sorted start
  'add-to-store.hh',
  'build.hh',
  'bundle.hh',
  'cat.hh',
  'config.hh',
  'copy.hh',
  'daemon.hh',
  'derivation-add.hh',
  'derivation-show.hh',
  'derivation.hh',
  'develop.hh',
  'diff-closures.hh',
  'doctor.hh',
  'dump-path.hh',
  'edit.hh',
  'eval.hh',
  'flake.hh',
  'fmt.hh',
  'hash.hh',
  'log.hh',
  'ls.hh',
  'make-content-addressed.hh',
  'nar.hh',
  'optimise-store.hh',
  'path-from-hash-part.hh',
  'path-info.hh',
  'ping-store.hh',
  'prefetch.hh',
  'profile.hh',
  'registry.hh',
  'repl.hh',
  'search.hh',
  'sigs.hh',
  'store-copy-log.hh',
  'store-delete.hh',
  'store-gc.hh',
  'store-repair.hh',
  'store.hh',
  'upgrade-nix.hh',
  'verify.hh',
  'why-depends.hh',
  # keep-sorted end
)

nix = executable(
  'nix',
  files('main.cc'),
  nix_sources,
  legacy_sources,
  nix_settings_headers,
  nix_headers,
  legacy_headers,
  include_directories : legacy_include_directories,
  cpp_pch : cpp_pch,
  install : true,
  install_rpath : libdir,
  dependencies : [
    liblix,
    boehm,
    mimalloc,
    capnp_rpc,
    nlohmann_json,
    kj,
    libatomic,
    libasanoptions,
  ],
  cpp_pch : cpp_pch,
  override_options: [
    'unity=off'
  ],
)

nix_symlinks = [
  'nix-build',
  'nix-channel',
  'nix-collect-garbage',
  'nix-copy-closure',
  'nix-daemon',
  'nix-env',
  'nix-hash',
  'nix-instantiate',
  'nix-prefetch-url',
  'nix-shell',
  'nix-store',
  'lix',
]

foreach linkname : nix_symlinks
  install_symlink(
    linkname,
    # TODO(Qyriad): should these continue to be relative symlinks?
    pointing_to : 'nix',
    install_dir : bindir,
    # The 'runtime' tag is what executables default to, which we want to emulate here.
    install_tag : 'runtime'
  )
endforeach

install_symlink(
  'build-remote',
  pointing_to : bindir / 'nix',
  install_dir : libexecdir / 'nix',
  # The 'runtime' tag is what executables default to, which we want to emulate here.
  install_tag : 'runtime'
)

install_symlink(
  'builtin-builder',
  pointing_to : bindir / 'nix',
  install_dir : libexecdir / 'lix',
  # The 'runtime' tag is what executables default to, which we want to emulate here.
  install_tag : 'runtime'
)
