#
# OUTLINE:
#
# The top-level meson.build file (this file) handles general logic for build options,
# generation of config.h (which is put in the build directory, not the source root
# like the previous, autoconf-based build system did), the mechanism for header
# generation, and the few global C++ compiler arguments that are added to all targets in Lix.
#
# src/meson.build coordinates each of Lix's subcomponents (the lib dirs in ./src),
# which each have their own meson.build. Lix's components depend on each other,
# so each of `src/lib{util,store,fetchers,expr,main,cmd}/meson.build` rely on variables
# set in earlier `meson.build` files. Each of these also defines the install targets for
# their headers.
#
# src/meson.build also collects the miscellaneous source files that are in further subdirectories
# that become part of the final Nix command (things like `src/nix-build/*.cc`).
#
# Finally, src/nix/meson.build defines the Nix command itself, relying on all prior meson files.
#
# Unit tests are setup in tests/unit/meson.build, under the test suite "check".
#
# Functional tests are a bit more complicated. Generally they're defined in
# tests/functional/meson.build, and rely on helper scripts meson/setup-functional-tests.py
# and meson/run-test.py. Scattered around also are configure_file() invocations, which must
# be placed in specific directories' meson.build files to create the right directory tree
# in the build directory.

project('lix', 'cpp',
  version : run_command('bash', '-c', 'echo -n $(cat ./.version)$VERSION_SUFFIX', check : true).stdout().strip(),
  default_options : [
    'cpp_std=c++2a',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'debug=true',
    'optimization=2',
    'errorlogs=true', # Please print logs for tests that fail
  ],
)

fs = import('fs')

prefix = get_option('prefix')
# For each of these paths, assume that it is relative to the prefix unless
# it is already an absolute path (which is the default for store-dir, state-dir, and log-dir).
path_opts = [
  # Meson built-ins.
  'datadir',
  'bindir',
  'mandir',
  'libdir',
  'includedir',
  'libexecdir',
  # Homecooked Lix directories.
  'store-dir',
  'state-dir',
  'log-dir',
  'profile-dir',
]
# For your grepping pleasure, this loop sets the following variables that aren't mentioned
# literally above:
# store_dir
# state_dir
# log_dir
# profile_dir
foreach optname : path_opts
  varname = optname.replace('-', '_')
  path = get_option(optname)
  if fs.is_absolute(path)
    set_variable(varname, path)
  else
    set_variable(varname, prefix / path)
  endif
endforeach

# sysconfdir doesn't get anything installed to directly, and is only used to
# tell Lix where to look for nix.conf, so it doesn't get appended to prefix.
sysconfdir = get_option('sysconfdir')
if not fs.is_absolute(sysconfdir)
  sysconfdir = '/' / sysconfdir
endif

# All of this has to go before the rest of the dependency checking,
# so that internal-api-docs can be built with -Denable-build=false

enable_docs = get_option('enable-docs')
enable_internal_api_docs = get_option('internal-api-docs')

doxygen = find_program('doxygen', required : enable_internal_api_docs, native : true)
bash = find_program('bash', native : true)

rapidcheck_meson = dependency('rapidcheck', required : enable_internal_api_docs)

if enable_internal_api_docs.enabled()
  message('subdiring()')
  subdir('doc/internal-api')
endif

if not get_option('enable-build')
  subdir_done()
endif

enable_tests = get_option('enable-tests')

tests_args = []

if get_option('tests-color')
  tests_args += '--gtest_color=yes'
endif

if get_option('tests-brief')
  tests_args += '--gtest_brief=1'
endif


cxx = meson.get_compiler('cpp')

# Translate some historical and Mesony CPU names to Lixy CPU names.
# FIXME(Qyriad): the 32-bit x86 code is not tested right now, because cross compilation for Lix
# to those architectures is currently broken for other reasons, namely:
# - nixos-23.11's x86_64-linux -> i686-linux glibc does not build (also applies to cppnix)
# - nixpkgs-unstable (as of 2024/04)'s boehmgc is not compatible with our patches
# It's also broken in cppnix, though.
host_cpu = host_machine.cpu_family()
if host_cpu in ['x86', 'i686', 'i386']
  # Meson considers 32-bit x86 CPUs to be "x86", and does not consider 64-bit
  # x86 CPUs to be "x86" (instead using "x86_64", which needs no translation).
  host_cpu = 'i686'
elif host_cpu == 'amd64'
  # This should not be needed under normal circumstances, but someone could pass a --cross-file
  # that sets the cpu_family to this.
  host_cpu = 'x86_64'
elif host_cpu in ['armv6', 'armv7']
  host_cpu += 'l'
endif

host_system = host_machine.cpu_family() + '-' + host_machine.system()
message('canonical Nix system name:', host_system)

is_linux = host_machine.system() == 'linux'
is_darwin = host_machine.system() == 'darwin'
is_x64 = host_machine.cpu_family() == 'x86_64'

# Per-platform arguments that you should probably pass to shared_module() invocations.
# Something like add_project_arguments() can't be scoped on only shared modules, so this
# variable is here instead.
# This corresponds to the $(1)_ALLOW_UNDEFINED option from the Make buildsystem.
# Mostly this is load-bearing on the plugin tests defined in tests/functional/plugins/meson.build.
shared_module_link_args = []
if is_darwin
  shared_module_link_args += ['-undefined', 'suppress', '-flat_namespace']
elif is_linux
  # -Wl,-z,defs is the equivalent, but a comment in the Make buildsystem says that breaks
  # Clang sanitizers on Linux.
  # FIXME(Qyriad): is that true?
endif
deps = [ ]
configdata = { }

#
# Dependencies
#

boehm = dependency('bdw-gc', required : get_option('gc'))
if boehm.found()
  deps += boehm
endif
configdata += {
  'HAVE_BOEHMGC': boehm.found().to_int(),
}

boost = dependency('boost', required : true, modules : ['context', 'coroutine', 'container'])
deps += boost

# cpuid only makes sense on x86_64
cpuid_required = is_x64 ? get_option('cpuid') : false
cpuid = dependency('libcpuid', 'cpuid', required : cpuid_required)
configdata += {
  'HAVE_LIBCPUID': cpuid.found().to_int(),
}
deps += cpuid

# seccomp only makes sense on Linux
seccomp_required = is_linux ? get_option('seccomp-sandboxing') : false
seccomp = dependency('libseccomp', 'seccomp', required : seccomp_required, version : '>=2.5.5')
if is_linux and not seccomp.found()
  warning('Sandbox security is reduced because libseccomp has not been found! Please provide libseccomp if it supports your CPU architecture.')
endif
configdata += {
  'HAVE_SECCOMP': seccomp.found().to_int(),
}

libarchive = dependency('libarchive', required : true)
deps += libarchive

brotli = [
  dependency('libbrotlicommon', required : true),
  dependency('libbrotlidec', required : true),
  dependency('libbrotlienc', required : true),
]
deps += brotli

openssl = dependency('libcrypto', 'openssl', required : true)
deps += openssl

aws_sdk = dependency('aws-cpp-sdk-core', required : false)
aws_sdk_transfer = dependency('aws-cpp-sdk-transfer', required : aws_sdk.found())
if aws_sdk.found()
  # The AWS pkg-config adds -std=c++11.
  # https://github.com/aws/aws-sdk-cpp/issues/2673
  aws_sdk = aws_sdk.partial_dependency(
    compile_args : false,
    includes : true,
    link_args : true,
    links : true,
    sources : true,
  )
  deps += aws_sdk
  s = aws_sdk.version().split('.')
  configdata += {
    'AWS_VERSION_MAJOR': s[0].to_int(),
    'AWS_VERSION_MINOR': s[1].to_int(),
    'AWS_VERSION_PATCH': s[2].to_int(),
  }
  aws_sdk_transfer = aws_sdk_transfer.partial_dependency(
    compile_args : false,
    includes : true,
    link_args : true,
    links : true,
    sources : true,
  )
endif

aws_s3 = dependency('aws-cpp-sdk-s3', required : false)
if aws_s3.found()
  # The AWS pkg-config adds -std=c++11.
  # https://github.com/aws/aws-sdk-cpp/issues/2673
  aws_s3 = aws_s3.partial_dependency(
    compile_args : false,
    includes : true,
    link_args : true,
    links : true,
    sources : true,
  )
  deps += aws_s3
endif

configdata += {
  'ENABLE_S3': aws_s3.found().to_int(),
}

sqlite = dependency('sqlite3', 'sqlite', version : '>=3.6.19', required : true)
deps += sqlite

sodium = dependency('libsodium', 'sodium', required : true)
deps += sodium

curl = dependency('libcurl', 'curl', required : true)
deps += curl

editline = dependency('libeditline', 'editline', version : '>=1.14', required : true)
deps += editline

lowdown = dependency('lowdown', version : '>=0.9.0', required : true)
deps += lowdown

# HACK(Qyriad): rapidcheck's pkg-config doesn't include the libs lol
# Note: technically we 'check' for rapidcheck twice, for the internal-api-docs handling above,
# but Meson will cache the result of the first one, and the required : arguments are different.
rapidcheck_meson = dependency('rapidcheck', required : enable_tests)
rapidcheck = declare_dependency(dependencies : rapidcheck_meson, link_args : ['-lrapidcheck'])
deps += rapidcheck

gtest = [
  dependency('gtest', required : enable_tests),
  dependency('gtest_main', required : enable_tests),
  dependency('gmock', required : enable_tests),
  dependency('gmock_main', required : enable_tests),
]
deps += gtest

toml11 = dependency('toml11', version : '>=3.7.0', required : true, method : 'cmake')
deps += toml11

nlohmann_json = dependency('nlohmann_json', required : true)
deps += nlohmann_json

# lix-doc is a Rust project provided via buildInputs and unfortunately doesn't have any way to be detected.
# Just declare it manually to resolve this.
#
# FIXME: build this with meson in the future after we drop Make (with which we
# *absolutely* are not going to make it work)
lix_doc = declare_dependency(link_args : [ '-llix_doc' ])
deps += lix_doc

#
# Build-time tools
#
coreutils = find_program('coreutils', native : true)
dot = find_program('dot', required : false, native : true)
pymod = import('python')
python = pymod.find_installation('python3')

if enable_docs
  mdbook = find_program('mdbook', native : true)
endif

# Used to workaround https://github.com/mesonbuild/meson/issues/2320 in src/nix/meson.build.
installcmd = find_program('install', native : true)

enable_embedded_sandbox_shell = get_option('enable-embedded-sandbox-shell')
if enable_embedded_sandbox_shell
  # This one goes in config.h
  # The path to busybox is passed as a -D flag when compiling libstore.
  # Idk why, ask the old buildsystem.
  configdata += {
    'HAVE_EMBEDDED_SANDBOX_SHELL': 1,
  }
endif

sandbox_shell = get_option('sandbox-shell')
# Consider it required if we're on Linux and the user explicitly specified a non-default value.
sandbox_shell_required = sandbox_shell != 'busybox' and host_machine.system() == 'linux'
# NOTE(Qyriad): package.nix puts busybox in buildInputs for Linux.
# Most builds should not require setting this.
busybox = find_program(sandbox_shell, required : sandbox_shell_required, native : false)
if not busybox.found() and host_machine.system() == 'linux' and sandbox_shell_required
  warning('busybox not found and other sandbox shell was specified')
  warning('a sandbox shell is recommended on Linux -- configure with -Dsandbox-shell=/path/to/shell to set')
endif
# FIXME(Qyriad): the autoconf system checks that busybox has the "standalone" feature, indicating
# that busybox sh won't run busybox applets as builtins (which would break our sandbox).

lsof = find_program('lsof', native : true)
bison = find_program('bison', native : true)
flex = find_program('flex', native : true)

# This is how Nix does generated headers...
# other instances of header generation use a very similar command.
# FIXME(Qyriad): do we really need to use the shell for this?
gen_header_sh = 'echo \'R"__NIX_STR(\' | cat - @INPUT@ && echo \')__NIX_STR"\''
gen_header = generator(
  bash,
  arguments : [ '-c', gen_header_sh ],
  capture : true,
  output : '@PLAINNAME@.gen.hh',
)

#
# Configuration
#

run_command('ln', '-s',
  meson.project_build_root() / '__nothing_link_target',
  meson.project_build_root() / '__nothing_symlink',
  check : true,
)
can_link_symlink = run_command('ln',
  meson.project_build_root() / '__nothing_symlink',
  meson.project_build_root() / '__nothing_hardlink',
  check : false,
).returncode() == 0
run_command('rm', '-f',
  meson.project_build_root() / '__nothing_symlink',
  meson.project_build_root() / '__nothing_hardlink',
  check : true,
)
summary('can hardlink to symlink', can_link_symlink, bool_yn : true)
configdata += { 'CAN_LINK_SYMLINK': can_link_symlink.to_int() }


# Check for each of these functions, and create a define like `#define HAVE_LCHOWN 1`.
check_funcs = [
  'lchown',
  'lutimes',
  'pipe2',
  'posix_fallocate',
  'statvfs',
  'strsignal',
  'sysconf',
]
foreach funcspec : check_funcs
  define_name = 'HAVE_' + funcspec.underscorify().to_upper()
  define_value = cxx.has_function(funcspec).to_int()
  configdata += {
    define_name: define_value,
  }
endforeach

config_h = configure_file(
  configuration : {
    'PACKAGE_NAME': '"' + meson.project_name() + '"',
    'PACKAGE_VERSION': '"' + meson.project_version() + '"',
    'PACKAGE_TARNAME': '"' + meson.project_name() + '"',
    'PACKAGE_STRING': '"' + meson.project_name() + ' ' + meson.project_version() + '"',
    'HAVE_STRUCT_DIRENT_D_TYPE': 1, # FIXME: actually check this for solaris
    'SYSTEM': '"' + host_system + '"',
  } + configdata,
  output : 'config.h',
)

install_headers(config_h, subdir : 'lix')

# FIXME: not using the pkg-config module because it creates way too many deps
# while meson migration is in progress, and we want to not include boost here
configure_file(
  input : 'src/lix-base.pc.in',
  output : 'lix-base.pc',
  install_dir : libdir / 'pkgconfig',
  configuration : {
    'prefix' : prefix,
    'libdir' : libdir,
    'includedir' : includedir,
    'PACKAGE_VERSION' : meson.project_version(),
  },
)

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config.h',
  '-Wno-deprecated-declarations',
  '-Wimplicit-fallthrough',
  '-Werror=switch',
  '-Werror=switch-enum',
  '-Wdeprecated-copy',
  '-Wignored-qualifiers',
  # Enable assertions in libstdc++ by default. Harmless on libc++. Benchmarked
  # at ~1% overhead in `nix search`.
  #
  # FIXME: remove when we get meson 1.4.0 which will default this to on for us:
  # https://mesonbuild.com/Release-notes-for-1-4-0.html#ndebug-setting-now-controls-c-stdlib-assertions
  '-D_GLIBCXX_ASSERTIONS=1',
  language : 'cpp',
)

if cxx.get_id() in ['gcc', 'clang']
  # 2024-03-24: jade benchmarked the default sanitize reporting in clang and got
  # a regression of about 10% on hackage-packages.nix with clang. So we are trapping instead.
  #
  # This has an unmeasurably low overhead in Nix evaluation benchmarks.
  #
  # N.B. Meson generates a completely nonsense warning here:
  # https://github.com/mesonbuild/meson/issues/9822
  # Both of these args cannot be written in the default meson configuration.
  # b_sanitize=signed-integer-overflow is ignored, and
  # -fsanitize-undefined-trap-on-error is not representable.
  sanitize_args = ['-fsanitize=signed-integer-overflow', '-fsanitize-undefined-trap-on-error']
  add_project_arguments(sanitize_args, language: 'cpp')
  add_project_link_arguments(sanitize_args, language: 'cpp')
endif

add_project_link_arguments('-pthread', language : 'cpp')
if cxx.get_linker_id() in ['ld.bfd', 'ld.gold']
  add_project_link_arguments('-Wl,--no-copy-dt-needed-entries', language : 'cpp')
endif

# Generate Chromium tracing files for each compiled file, which enables
# maintainers/buildtime_report.sh BUILD-DIR to simply work in clang builds.
#
# They can also be manually viewed at https://ui.perfetto.dev
if get_option('profile-build').require(meson.get_compiler('cpp').get_id() == 'clang').enabled()
  add_project_arguments('-ftime-trace', language: 'cpp')
endif

if cxx.get_id() in ['clang', 'gcc']
  add_project_arguments([
      # Meson uses out of source builds, conventionally usually in a subdirectory
      # of the source tree (e.g. meson setup ./build). This means that unlike in
      # the previous Make buildsystem, all compilation sources are passed as a relative
      # parent, e.g. `cc -o src/libexpr/nixexpr.cc.o ../src/libexpr/nixexpr.cc`.
      # These paths show up when debugging, and in asserts, which look both look strange
      # and confuse debuggers.
      # So let's just tell GCC and Clang that ../src really means src.
      '-ffile-prefix-map=../src=src',
    ],
    language : 'cpp',
  )
endif

if is_darwin
  configure_file(
    input : 'misc/launchd/org.nixos.nix-daemon.plist.in',
    output : 'org.nixos.nix-daemon.plist',
    copy : true,
    install_dir : prefix / 'Library/LaunchDaemons',
  )
endif

subdir('src')
subdir('scripts')
subdir('misc')

if enable_docs
  subdir('doc/manual')
endif

if enable_tests
  subdir('tests/unit')
  subdir('tests/functional')
endif
