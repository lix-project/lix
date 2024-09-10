legacy_include_directories = include_directories('.')

legacy_sources = files(
  # `build-remote` is not really legacy (it powers all remote builds), but it's
  # not a `nix3` command.
  'build-remote.cc',
  'dotgraph.cc',
  'graphml.cc',
  'nix-build.cc',
  'nix-channel.cc',
  'nix-collect-garbage.cc',
  'nix-copy-closure.cc',
  'nix-env.cc',
  'nix-env.hh',
  'nix-instantiate.cc',
  'nix-store.cc',
  'user-env.cc',
)

legacy_headers = files(
  'build-remote.hh',
  'nix-build.hh',
  'nix-channel.hh',
  'nix-collect-garbage.hh',
  'nix-copy-closure.hh',
  'nix-instantiate.hh',
  'nix-store.hh',
)

legacy_generated_headers = [
  gen_header.process('buildenv.nix', preserve_path_from: meson.current_source_dir()),
  gen_header.process('unpack-channel.nix', preserve_path_from: meson.current_source_dir()),
]

fs.copyfile('unpack-channel.nix')
