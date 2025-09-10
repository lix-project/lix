nix_generated_headers = [
  gen_header.process(meson.project_source_root() / 'doc/manual/generate-manpage.nix'),
  gen_header.process('get-env.sh'),
]

# lix/nix/profile.cc includes lix/nix/profile.md, which includes "doc/files/profiles.md.gen.hh".
# Unfortunately, https://github.com/mesonbuild/meson/issues/2320.
# "docs/files" isn't a directory hierarchy that already exists somewhere in this source tree,
# and Meson refuses to create targets with specific directory paths.
# So run_command() it is.
# NOTE(Qyriad): This corresponds to the previous buildsystem's `src/nix/doc/files/%.md` rule,
# which as far as I can tell was only used for this file.
run_command(
  installcmd,
  '-D',
  meson.project_source_root() / 'doc/manual/src/command-ref/files/profiles.md',
  meson.current_build_dir() / 'doc/files/profiles.md',
  check : true,
)
nix_generated_headers += gen_header.process(
  meson.current_build_dir() / 'doc/files/profiles.md',
  preserve_path_from : meson.current_build_dir(),
)

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
  'main.cc',
  'make-content-addressed.cc',
  'nar.cc',
  'optimise-store.cc',
  'path-from-hash-part.cc',
  'path-info.cc',
  'ping-store.cc',
  'prefetch.cc',
  'profile.cc',
  'realisation.cc',
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
  'realisation.hh',
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
  nix_sources,
  legacy_sources,
  nix_settings_headers,
  nix_generated_headers,
  nix_headers,
  legacy_headers,
  legacy_generated_headers,
  include_directories : legacy_include_directories,
  dependencies : [
    libasanoptions,
    liblixutil,
    liblixstore,
    liblixfetchers,
    liblixexpr,
    liblixmain,
    liblixcmd,
    boehm,
    capnp_rpc,
    nlohmann_json,
    kj,
    libatomic,
  ],
  cpp_pch : cpp_pch,
  install : true,
  # FIXME(Qyriad): is this right?
  install_rpath : libdir,
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
