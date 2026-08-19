legacy_include_directories = include_directories('.')

legacy_sources = files(
  # `build-remote` is not really legacy (it powers all remote builds), but it's
  # not a `nix3` command.
  'build-remote.cc',
  'builtin-builder.cc',
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
  'builtin-builder.hh',
  'nix-build.hh',
  'nix-channel.hh',
  'nix-collect-garbage.hh',
  'nix-copy-closure.hh',
  'nix-instantiate.hh',
  'nix-store.hh',
)
