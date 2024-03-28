# Subcomponents: these link into artifacts themselves, and have interdependencies.

subdir('libutil')
# Load-bearing order. libstore depends on libutil.
subdir('libstore')
# libfetchers depends on libstore
subdir('libfetchers')
# libexpr depends on all of the above
subdir('libexpr')
# libmain depends on libutil and libstore
subdir('libmain')
# libcmd depends on everything
subdir('libcmd')


# The rest of the subdirectories aren't separate components,
# just source files in another directory, so we process them here.

build_remote_sources = files(
  'build-remote/build-remote.cc',
)
nix_build_sources = files(
  'nix-build/nix-build.cc',
)
nix_channel_sources = files(
  'nix-channel/nix-channel.cc',
)
unpack_channel_gen = gen_header.process('nix-channel/unpack-channel.nix')
nix_collect_garbage_sources = files(
  'nix-collect-garbage/nix-collect-garbage.cc',
)
nix_copy_closure_sources = files(
  'nix-copy-closure/nix-copy-closure.cc',
)
nix_env_buildenv_gen = gen_header.process('nix-env/buildenv.nix')
nix_env_sources = files(
  'nix-env/nix-env.cc',
  'nix-env/user-env.cc',
)
nix_instantiate_sources = files(
  'nix-instantiate/nix-instantiate.cc',
)
nix_store_sources = files(
  'nix-store/dotgraph.cc',
  'nix-store/graphml.cc',
  'nix-store/nix-store.cc',
)

# Hurray for Meson list flattening!
nix2_commands_sources = [
  build_remote_sources,
  nix_build_sources,
  nix_channel_sources,
  unpack_channel_gen,
  nix_collect_garbage_sources,
  nix_copy_closure_sources,
  nix_env_buildenv_gen,
  nix_env_sources,
  nix_instantiate_sources,
  nix_store_sources,
]

# Finally, the nix command itself, which all of the other commands are implmented in terms of
# as a multicall binary.
subdir('nix')

# Just copies nix-channel/unpack-channel.nix to the build directory.
# Done as a subdir to get Meson to respect the path hierarchy.
subdir('nix-channel')
