# Subcomponents: these link into artifacts themselves, and have interdependencies.
subdir('lix-doc')

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

# Static library that just sets default ASan options. It needs to be included
# in every executable.
asanoptions = static_library(
  'libasanoptions',
  files('asan-options/asan-options.cc'),
)
libasanoptions = declare_dependency(
  link_whole: asanoptions
)

# Legacy commands.
subdir('legacy')

# Finally, the nix command itself, which all of the other commands are implmented in terms of
# as a multicall binary.
subdir('nix')
