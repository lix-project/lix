# Cursed, but I don't think there's another way to get this environment variable.
lix_suffix = run_command('bash', '-c', 'echo -n "$VERSION_SUFFIX"', check : true).stdout().strip()
lix_version_parts = meson.project_version().split('.')
lix_major = lix_version_parts[0]
lix_minor = lix_version_parts[1]
lix_patch = lix_version_parts[2].replace(lix_suffix, '')

# These variables (aside from LSOF) are created pseudo-dynamically, near the beginning of
# the top-level meson.build. Aside from prefix itself, each of these was
# made into an absolute path by joining it with prefix, unless it was already
# an absolute path (which is the default for store-dir, state-dir, and log-dir).
cpp_str_defines = {
  'LSOF': lsof.full_path(),
  'NIX_PREFIX':    prefix,
  'NIX_STORE_DIR': store_dir,
  'NIX_DATA_DIR':  datadir,
  'NIX_STATE_DIR': state_dir / 'nix',
  'NIX_LOG_DIR':   log_dir,
  'NIX_CONF_DIR':  sysconfdir / 'nix',
  'NIX_BIN_DIR':   bindir,
  'NIX_MAN_DIR':   mandir,
}

embedded_sandbox_headers = []

if enable_embedded_sandbox_shell
  hexdump = find_program('hexdump', required : true, native : true)
  embedded_sandbox_shell_gen = custom_target(
    'embedded-sandbox-shell.gen.hh',
    command : [
      hexdump,
      '-v',
      '-e',
      '1/1 "0x%x," "\n"'
    ],
    input : busybox.full_path(),
    output : 'embedded-sandbox-shell.gen.hh',
    capture : true,
    feed : true,
  )
  embedded_sandbox_headers += embedded_sandbox_shell_gen
  cpp_str_defines += {
    'SANDBOX_SHELL': '__embedded_sandbox_shell__'
  }
elif busybox.found()
  cpp_str_defines += {
    'SANDBOX_SHELL': busybox.full_path()
  }
endif

if pasta.found()
  cpp_str_defines += {
    'PASTA_PATH': pasta.full_path(),
  }
endif

cpp_args = []

foreach name, value : cpp_str_defines
  cpp_args += [
    '-D' + name + '=' + '"' + value + '"'
  ]
endforeach

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

# We manage libutil's Rust components here, because subdir(rust-monocrate)
# needs libutil's Rust components, and subdir(libutil) needs rust-monocrate.
# FIXME?: Should libutil-rs just be in a separate directory from libutil, then?
libutil_rs_sources = files(
  'libutil/lib.rs',
)
libutil_rs = static_library(
  'lixutil_rs',
  sources : libutil_rs_sources,
  rust_args : [
    # Will be empty if we're linking statically.
    rust_dynamic_args,
    # Will no-op if we're linking statically.
    '-C', f'link-arg=-Wl,@soname_arg@,liblixutil_rs.@dylib_suffix@',
  ],
)
libutil_rs_gen_h = custom_target(
  'libutil-rs.gen.hh',
  input : [
    files('libutil/cbindgen.toml'),
    libutil_rs_sources,
  ],
  output : ['libutil-rs.gen.hh'],
  command : [
    cbindgen,
    '--config',
    '@INPUT0@',
    '--output',
    '@OUTPUT0@',
    '--',
    '@INPUT1@',
  ],
)
# For rust-monocrate.
liblixutil_rs_internal = declare_dependency(
  link_with : libutil_rs,
)

subdir('rust-monocrate')

# NOW we can create the dependency object anything that actually wants to use liblixutil_rs,
# because liblixutil_rs is secretly rust-monocrate in a trenchcoat.
# `library('lixutil')` and `executable('liblixutil-tests')` depend on this.
liblixutil_rs = declare_dependency(
  link_with : rust_monocrate,
  sources : [libutil_rs_gen_h],
)

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
