generate_manpage_gen = gen_header.process(meson.project_source_root() / 'doc/manual/generate-manpage.nix')

utils_gen = gen_header.process(meson.project_source_root() / 'doc/manual/utils.nix')

get_env_gen = gen_header.process('get-env.sh')

# src/nix/profile.cc includes src/nix/profile.md, which includes "doc/files/profiles.md.gen.hh".
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
profiles_md_gen = gen_header.process(
  meson.current_build_dir() / 'doc/files/profiles.md',
  preserve_path_from : meson.current_build_dir(),
)

nix_sources = files(
  'add-to-store.cc',
  'app.cc',
  'build.cc',
  'bundle.cc',
  'cat.cc',
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
  'config.cc',
  'sigs.cc',
  'store-copy-log.cc',
  'store-delete.cc',
  'store-gc.cc',
  'store-repair.cc',
  'store.cc',
  'upgrade-nix.cc',
  'verify.cc',
  'why-depends.cc',
)

nix = executable(
  'nix',
  nix_sources,
  generate_manpage_gen,
  utils_gen,
  get_env_gen,
  profiles_md_gen,
  nix2_commands_sources,
  dependencies : [
    liblixcmd,
    liblixutil,
    liblixstore,
    liblixexpr,
    liblixfetchers,
    liblixmain,
    boehm,
    nlohmann_json,
  ],
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
