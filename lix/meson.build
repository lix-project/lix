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

builtin_dep_closure_set = []
foreach path : builtin_dep_closure
  # replace all `)"` substrings with `))" R"("`, ie split the `)"` into two chars and
  # have the preprocessor concatenate our literals at the break. this way we can just
  # use raw strings to represent all paths without worrying about any other escaping.
  builtin_dep_closure_set += [ 'R"(' + path.replace(')"', '))" R"("') + ')"' ]
endforeach

configdata.set('HAVE_STRUCT_DIRENT_D_TYPE', 1) # FIXME: actually check this for solaris
configdata.set('LIX_MAJOR', lix_major)
configdata.set('LIX_MINOR', lix_minor)
configdata.set('LIX_PATCH', lix_patch)
configdata.set_quoted('PACKAGE_NAME', meson.project_name())
configdata.set_quoted('PACKAGE_VERSION', meson.project_version())
configdata.set_quoted('PACKAGE_TARNAME', meson.project_name())
configdata.set_quoted('PACKAGE_STRING', meson.project_name() + ' ' + meson.project_version())
configdata.set_quoted('SYSTEM', host_system)
configdata.set_quoted('LIX_LIBEXEC_DIR', libexecdir / 'lix')
configdata.set('BUILTIN_DEP_CLOSURE', ', '.join(builtin_dep_closure_set))

config_h = configure_file(
  configuration : configdata,
  output : 'config.h',
)

install_headers(config_h, subdir : 'lix')

# we only collect sources and generated headers here. real headers are collected and
# installed by each subdir due to how meson generates paths for headers it installs.
liblix_sources = []
liblix_generated_headers = embedded_sandbox_headers

lix_rs_files = files(
  '../Cargo.toml',
  '../Cargo.lock',
)
subdir('lix-rs')

# Subcomponents: these link into artifacts themselves, and have interdependencies.
subdir('lix-doc')

subdir('libutil')
subdir('libstore')
subdir('libfetchers')
subdir('libexpr')
subdir('libmain')
subdir('libcmd')

# libexec utilities should depend only on libutil, but meh.
subdir('libexec')

# FIXME: not using the pkg-config module because it creates way too many deps
# while meson migration is in progress, and we want to not include boost here
foreach lib : [ 'util', 'store', 'fetchers', 'expr', 'main', 'cmd' ]
  configure_file(
    input : f'lib@lib@/lix-@lib@.pc.in',
    output : f'lix-@lib@.pc',
    install_dir : libdir / 'pkgconfig',
    configuration : {
      'prefix' : prefix,
      'libdir' : libdir,
      'includedir' : includedir,
      'PACKAGE_VERSION' : meson.project_version(),
    },
  )
endforeach

dependencies = [
  aws_s3,
  aws_sdk,
  aws_sdk_transfer,
  boehm,
  boost,
  brotli,
  capnp_rpc,
  cpuid,
  curl,
  editline,
  kj,
  libarchive,
  libatomic,
  lowdown,
  ncurses,
  nlohmann_json,
  openssl,
  seccomp,
  sqlite,
  toml11,
]

if host_machine.system() == 'freebsd'
  dependencies += [ libprocstat ]
endif

lixrs_cargo_env = {
  'MESON_BUILD_DIR': meson.project_build_root(),
}

lixrs = custom_target(
  'liblixrs',
  input : lix_rs_files,
  output : 'liblixrs.a',
  command : [
    cargo,
    '-Z', 'unstable-options',
    'build',
    '--artifact-dir=@0@'.format(meson.current_build_dir()),
    '-p',
    'lix',
    f'--target=@host_triple@',
    cargo_jobs_args,
  ],
  console : true,
  env : lixrs_cargo_env,
)

if meson.can_run_host_binaries()
  test(
    'lix-rs-tests',
    cargo,
    args : [
      'test',
      '--all', '--all-targets',
      '--no-fail-fast',
      cargo_jobs_args,
    ],
    suite : 'check',
    env : lixrs_cargo_env,
    timeout : 300,
  )
endif

liblix_cpp_static = static_library(
  'lix_cpp_static',
  liblix_sources,
  liblix_generated_headers,
  dependencies : [
    dependencies,
  ],
  include_directories : '..',
  cpp_args : cpp_args,
  cpp_pch : cpp_pch,
)

if is_darwin
  lixrs_link_args = [ '-Wl,-force_load,lix/liblixrs.a' ]
else
  lixrs_link_args = [ '-Wl,-L,lix,--whole-archive,-l,lixrs,--no-whole-archive' ]
endif

if is_static
  liblix = declare_dependency(
    include_directories : include_directories('..'),
    dependencies : dependencies,
    link_whole : [ liblix_cpp_static ],
    link_args : lixrs_link_args,
    sources : [
      custom_target(
        input : [ lixrs ],
        output : 'lixrs-dep-propagator.gen.hh',
        command : [ 'bash', '-c', 'touch "$1"', 'touch', '@OUTPUT@' ],
      ),
    ],
  )
else
  liblix_all_shared = shared_library(
    'lix',
    custom_target(
      input : [ lixrs ],
      output : 'lixrs-dep-propagator.gen.cc',
      command : [ 'bash', '-c', 'touch "$1"', 'touch', '@OUTPUT@' ],
    ),
    dependencies : [
      dependencies,
      require_dylib_rtti,
    ],
    link_whole : [ liblix_cpp_static ],
    link_args : lixrs_link_args,
    # `..` makes the `lix/` subtree of the repo available for includes,
    # mirroring our header directory structure once they are installed.
    include_directories : [ '..' ],
    cpp_args : cpp_args,
    cpp_pch : cpp_pch,
    install : true,
    # FIXME(Qyriad): is this right?
    install_rpath : libdir,
  )

  liblix = declare_dependency(
    include_directories : include_directories('..'),
    dependencies : dependencies,
    link_with : [ liblix_all_shared ],
  )
endif

meson.override_dependency('lix', liblix)

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
