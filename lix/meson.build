# Cursed, but I don't think there's another way to get this environment variable.
lix_suffix = run_command('bash', '-c', 'echo -n "$VERSION_SUFFIX"', check : true).stdout().strip()
lix_version_parts = meson.project_version().split('.')
lix_major = lix_version_parts[0]
lix_minor = lix_version_parts[1]
lix_patch = lix_version_parts[2].replace(lix_suffix, '')

config_h = configure_file(
  configuration : {
    'PACKAGE_NAME': '"' + meson.project_name() + '"',
    'PACKAGE_VERSION': '"' + meson.project_version() + '"',
    'LIX_MAJOR': lix_major,
    'LIX_MINOR': lix_minor,
    'LIX_PATCH': lix_patch,
    'PACKAGE_TARNAME': '"' + meson.project_name() + '"',
    'PACKAGE_STRING': '"' + meson.project_name() + ' ' + meson.project_version() + '"',
    'HAVE_STRUCT_DIRENT_D_TYPE': 1, # FIXME: actually check this for solaris
    'SYSTEM': '"' + host_system + '"',
  } + configdata,
  output : 'config.h',
)

install_headers(config_h, subdir : 'lix')

# Subcomponents: these link into artifacts themselves, and have interdependencies.
subdir('lix-doc')

subdir('rust-monocrate')

# liblix_doc is actually rust-monocrate in a trenchcoat.
liblix_doc = declare_dependency(
  link_with : rust_monocrate,
)

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
