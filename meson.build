#
# OUTLINE:
#
# The top-level meson.build file (this file) handles general logic for build options,
# the mechanism for header generation, and the few global C++ compiler
# arguments that are added to all targets in Lix.
#
# lix/meson.build coordinates each of Lix's subcomponents (the lib dirs in ./lix),
# which each have their own meson.build. Lix's components depend on each other,
# so each of `lix/lib{util,store,fetchers,expr,main,cmd}/meson.build` rely on variables
# set in earlier `meson.build` files. Each of these also defines the install targets for
# their headers.
#
# lix/meson.build also collects the miscellaneous source files that are in further subdirectories
# that become part of the final Nix command (things like `lix/nix-build/*.cc`).
#
# Finally, lix/nix/meson.build defines the Nix command itself, relying on all prior meson files.
#
# lix-doc is built with Meson in lix-doc/meson.build, and linked into libcmd in
# lix/libcmd/meson.build. When building outside the Nix sandbox, Meson will use the .wrap
# files in subprojects/ to download and extract the dependency crates into subprojects/.
# When building inside the Nix sandbox, Lix's derivation in package.nix uses a
# fixed-output derivation to fetch those crates in advance instead, and then symlinks
# them into subprojects/ with the same names that Meson uses when downloading them
# itself -- perfect for --wrap-mode=nodownload, which mesonConfigurePhase uses.
#
# Unit tests are setup in tests/unit/meson.build, under the test suite "check".
#
# Functional tests are a bit more complicated. Generally they're defined in
# tests/functional/meson.build, and rely on helper scripts meson/setup-functional-tests.py
# and meson/run-test.py. Scattered around also are configure_file() invocations, which must
# be placed in specific directories' meson.build files to create the right directory tree
# in the build directory.

project('lix', 'cpp', 'rust',
  # NOTE: when bumping this, remember to also bump meson_version in subprojects.
  meson_version : '>=1.7.0',
  version : run_command('bash', '-c', 'echo -n $(jq -r .version < ./version.json)$VERSION_SUFFIX', check : true).stdout().strip(),
  default_options : [
    'cpp_std=c++23',
    'rust_std=2021',
    'warning_level=2',
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

is_static = get_option('default_library') == 'static'

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

enable_nix_eval_jobs = get_option('nix-eval-jobs')
enable_tests = get_option('enable-tests')

tests_args = []

if get_option('tests-color')
  tests_args += '--gtest_color=yes'
endif

if get_option('tests-brief')
  tests_args += '--gtest_brief=1'
endif


cxx = meson.get_compiler('cpp')

# tag define to tell us we are compiling lix itself and can thus make
# non-namespaced aliases of a utility defines visible. this spares us
# writing LIX_TRY_AWAIT, LIX_RUN_ASYNC_IN, and others in our own code
# without being forced to hide the headers we take these defines from
add_project_arguments(
  '-DLIX_UR_COMPILER_UWU',
  language : 'cpp',
)


# clangd breaks when GCC is using precompiled headers lmao
# https://git.lix.systems/lix-project/lix/issues/374
should_pch = get_option('enable-pch-std')
summary('PCH C++ stdlib', should_pch, bool_yn : true)
if should_pch
  # Unlike basically everything else that takes a file, Meson requires the arguments to
  # cpp_pch : to be strings and doesn't accept files(). So absolute path it is.
  cpp_pch = [meson.project_source_root() / 'lix/pch/precompiled-headers.hh']

  # Saves about 400s (30% at time of writing) from compile time on-cpu, mostly
  # by removing instantiations of nlohmann from every single damned compilation
  # unit.
  # There is no equivalent in gcc.
  if cxx.get_id() == 'clang'
    add_project_arguments(
      '-fpch-instantiate-templates',
      language : 'cpp',
    )
  endif
else
  cpp_pch = []
endif

# gcc 12 is known to miscompile some coroutine-based code quite horribly,
# causing (among other things) copies of move-only objects and the double
# frees one would expect when the objects are unique_ptrs. these problems
# often show up as memory corruption when nesting generators (since we do
# treat generators like owned memory) and will cause inexplicable crashs.
#
# gcc 13 does not compile capnp coroutine code correctly. a newer version
# may fix this. (cf. https://gcc.gnu.org/bugzilla/show_bug.cgi?id=102051)
# we allow gcc 13 here anyway because CI uses it for clang-tidy, and when
# the compiler crashes outright if won't produce any bad binaries either.
assert(
  cxx.get_id() != 'gcc' or cxx.version().version_compare('>=13'),
  'GCC is known to miscompile coroutines, use clang.'
)
if cxx.get_id() == 'gcc'
  warning('GCC is known to crash while building coroutines, use clang.')
endif


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
elif host_cpu == 'arm'
  # Too generic, we use more specific ones
  host_cpu = host_machine.cpu()
elif host_cpu in ['mips', 'mips64']
  # LE is suffixed with 'el'
  if host_machine.endian() == 'little'
    host_cpu += 'el'
  endif
elif host_cpu == 'ppc'
  host_cpu = 'powerpc'
  if host_machine.endian() == 'little'
    host_cpu += 'le'
  endif
elif host_cpu == 'ppc64'
  host_cpu = 'powerpc64'
  if host_machine.endian() == 'little'
    host_cpu += 'le'
  endif
endif

host_system = host_cpu + '-' + host_machine.system()
message('canonical Nix system name:', host_system)

is_linux = host_machine.system() == 'linux'
is_darwin = host_machine.system() == 'darwin'
is_freebsd = host_machine.system() == 'freebsd'
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
configdata = { }

#
# Dependencies
#

gc_opt = get_option('gc').disable_if(
  'address' in get_option('b_sanitize'),
  error_message: 'gc does far too many memory crimes for ASan'
)
boehm = dependency('bdw-gc', required : gc_opt, version : '>=8.2.6', include_type : 'system')
configdata += {
  'HAVE_BOEHMGC': boehm.found().to_int(),
}

boost = dependency('boost', required : true, include_type : 'system')
kj = dependency('kj-async', required : true, include_type : 'system')
capnp_rpc = dependency('capnp-rpc', required : true, include_type : 'system')

# cpuid only makes sense on x86_64
cpuid_required = is_x64 ? get_option('cpuid') : false
cpuid = dependency('libcpuid', 'cpuid', required : cpuid_required, include_type : 'system')
configdata += {
  'HAVE_LIBCPUID': cpuid.found().to_int(),
}

# seccomp only makes sense on Linux
seccomp_required = is_linux ? get_option('seccomp-sandboxing') : false
seccomp = dependency('libseccomp', 'seccomp', required : seccomp_required, version : '>=2.5.5', include_type : 'system')
if is_linux and not seccomp.found()
  warning('Sandbox security is reduced because libseccomp has not been found! Please provide libseccomp if it supports your CPU architecture.')
endif
configdata += {
  'HAVE_SECCOMP': seccomp.found().to_int(),
}

# fcntl(F_GETPATH) returns the path of an fd on macOS and BSDs
configdata += {
  'HAVE_F_GETPATH': cxx.has_header_symbol('fcntl.h', 'F_GETPATH').to_int(),
}

libarchive = dependency('libarchive', required : true, include_type : 'system')

brotli = [
  dependency('libbrotlicommon', required : true, include_type : 'system'),
  dependency('libbrotlidec', required : true, include_type : 'system'),
  dependency('libbrotlienc', required : true, include_type : 'system'),
]

openssl = dependency('libcrypto', 'openssl', required : true, include_type : 'system')
ncurses = dependency('ncurses', required : true, include_type : 'system')

# FIXME: confirm we actually support such old versions of aws-sdk-cpp
aws_sdk = dependency('aws-cpp-sdk-core', required : false, version : '>=1.8', include_type : 'system')
aws_sdk_transfer = dependency(
  'aws-cpp-sdk-transfer',
  required : aws_sdk.found(),
  fallback : ['aws_sdk', 'aws_cpp_sdk_transfer_dep'],
  include_type : 'system',
)
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
  aws_sdk_transfer = aws_sdk_transfer.partial_dependency(
    compile_args : false,
    includes : true,
    link_args : true,
    links : true,
    sources : true,
  )
endif

aws_s3 = dependency(
  'aws-cpp-sdk-s3',
  required : aws_sdk.found(),
  fallback : ['aws_sdk', 'aws_cpp_sdk_s3_dep'],
  include_type : 'system',
)
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
endif

configdata += {
  'ENABLE_S3': aws_s3.found().to_int(),
}

sqlite = dependency('sqlite3', 'sqlite', version : '>=3.6.19', required : true, include_type : 'system')

curl = dependency('libcurl', 'curl', required : true, include_type : 'system')

editline = dependency('libeditline', 'editline', version : '>=1.14', required : true, include_type : 'system')

lowdown = dependency('lowdown', version : '>=0.9.0', required : true, include_type : 'system')

# TODO(sterni): drop the corresponding #ifdef after NixOS 25.05 is EOL which still distributes lowdown < 1.4.0
if lowdown.version().version_compare('>= 1.4.0')
  add_project_arguments('-DLOWDOWN_SEPARATE_TERM_OPTS', language: 'cpp')
endif

# HACK(Qyriad): rapidcheck's pkg-config doesn't include the libs lol
# Note: technically we 'check' for rapidcheck twice, for the internal-api-docs handling above,
# but Meson will cache the result of the first one, and the required : arguments are different.
rapidcheck_meson = dependency('rapidcheck', required : enable_tests, include_type : 'system')
rapidcheck = declare_dependency(dependencies : rapidcheck_meson, link_args : ['-lrapidcheck'])

gtest = [
  dependency('gtest', required : enable_tests, include_type : 'system'),
  dependency('gtest_main', required : enable_tests, include_type : 'system'),
  dependency('gmock', required : enable_tests, include_type : 'system'),
  dependency('gmock_main', required : enable_tests, include_type : 'system'),
]

toml11 = dependency('toml11', version : '>=4.0.0', required : true, method : 'cmake', include_type : 'system')

pegtl = dependency(
  'pegtl',
  version : '>=3.2.7',
  required : true,
  method : 'cmake',
  modules : [ 'taocpp::pegtl' ],
  include_type : 'system',
)

nlohmann_json = dependency('nlohmann_json', required : true, include_type : 'system')

if is_freebsd
  libprocstat = declare_dependency(link_args : [ '-lprocstat' ])
endif

libatomic_test_program = '''
#include <atomic>
int main() {
  std::atomic<uint8_t> w1;
  std::atomic<uint16_t> w2;
  std::atomic<uint32_t> w4;
  std::atomic<uint64_t> w8;
  return ++w1 + ++w2 + ++w4 + ++w8;
}
'''

libatomic = dependency('atomic', required : false)

# Some platforms like 32-Bit PowerPC need libatomic because they're lacking 64-Bit hardware atomic instructions
# and compilers don't handle this automatically (yet).
# See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=81358 and https://clang.llvm.org/docs/Toolchain.html#atomics-library
if not cxx.links(libatomic_test_program, name : 'test if simple atomic program links')
  libatomic = dependency('atomic', required : true)
endif

#
# Build-time tools
#
dot = find_program('dot', required : false, native : true)
pymod = import('python')
python = pymod.find_installation('python3')

if enable_docs
  mdbook = find_program('mdbook', native : true)
endif

# Used to workaround https://github.com/mesonbuild/meson/issues/2320 in lix/nix/meson.build.
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
build_test_shell = get_option('build-test-shell')
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

# Trace points for dtrace and bpftrace
dtrace_feature = get_option('dtrace-probes')
if dtrace_feature.allowed()
  dtrace_exe = find_program('dtrace', native : false, required : false)
  dtrace_header_present = cxx.has_header('sys/sdt.h')
  dtrace_feature = dtrace_feature.enable_auto_if(dtrace_exe.found() and dtrace_header_present)
endif

if get_option('disable-fibers')
  warning('building Lix without fiber support. performance will suffer.')
  configdata += {
    'HAVE_THREADBARE_LIBC': 1,
  }
endif

if dtrace_feature.enabled()
  dtrace_feature.require(dtrace_exe.found() and dtrace_header_present,
    error_message : 'trace probes require both the dtrace command and sys/sdt.h header to be available')

  # NOTE: glib seems to have had to hack some stuff up that we are not hacking
  # up. I don't know why.
  # https://github.com/GNOME/glib/blob/03f7c1fbf3a3784cb4c3604f83ca3645e9225577/meson.build#L2420-L2434

  # Darwin doesn't require an object generator for dtrace, but it's preferable
  # to still have the build structured the same, so we generate empty object
  # files with clang.
  if is_darwin
    dtrace_object_gen = generator(bash,
      output : '@BASENAME@.o',
      arguments : [
        '-c',
        'exec "$@"',
        '--',
        cxx.cmd_array(),
        '-xc++',
        '-c', '/dev/null',
        '-o', '@OUTPUT@'
      ])
  else
    dtrace_object_gen = generator(dtrace_exe,
      output : '@BASENAME@.o',
      arguments : ['-G', '-s', '@INPUT@', '-o', '@OUTPUT@'])
  endif
  dtrace_header_gen = generator(dtrace_exe,
    output : '@BASENAME@.gen.hh',
    arguments : ['-h', '-s', '@INPUT@', '-o', '@OUTPUT@'])
endif
configdata += {
  'HAVE_DTRACE': dtrace_feature.enabled().to_int(),
}

pasta_path = get_option('pasta-path')
# we can't check the pasta version because passt misuses stdio (it calls _exit()
# after printing the version, which will never print the version unless run from
# a terminal). pasta isn't mandatory yet due to high fetcher breakage potential.
# we *will* enable it in our own packaging, but distributions are not forced to.
pasta = find_program(pasta_path, required : false, native : false)

lsof = find_program('lsof', native : true)

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
if is_linux or is_freebsd
  # musl does not have close_range as of 2024-08-10
  # patch: https://www.openwall.com/lists/musl/2024/08/01/9
  check_funcs += [ 'close_range' ]
endif
foreach funcspec : check_funcs
  define_name = 'HAVE_' + funcspec.underscorify().to_upper()
  define_value = cxx.has_function(funcspec).to_int()
  configdata += {
    define_name: define_value,
  }
endforeach

# FIXME: not using the pkg-config module because it creates way too many deps
# while meson migration is in progress, and we want to not include boost here
configure_file(
  input : 'lix/lix-base.pc.in',
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
  #
  # n.b. config.h is generated inside lix/meson.build so it goes in build/lix
  # for consistency with the installed header being lix/config.h
  '-include', 'lix/config.h',
  '-Wno-unused-parameter',
  '-Wno-deprecated-declarations',
  '-Wno-missing-field-initializers',
  '-Wimplicit-fallthrough',
  '-Werror=switch',
  '-Werror=switch-enum',
  '-Werror=unused-result',
  '-Wdeprecated-copy',
  '-Wignored-qualifiers',
  '-Werror=suggest-override',
  # don't let nlohmann automatically cast enums to ints. we don't set this
  # inside libutil headers to not force this behavior on users of libutil.
  '-DJSON_DISABLE_ENUM_SERIALIZATION=1',
  language : 'cpp',
)

# We turn off the production UBSan if the slower dev UBSan is requested, to
# give better diagnostics.
if cxx.get_id() in ['gcc', 'clang'] and 'undefined' not in get_option('b_sanitize')
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
# Clang's default of -no-shared-libsan on Linux causes link errors; on macOS it defaults to shared.
# GCC defaults to shared libsan so is fine.
if cxx.get_id() == 'clang' and get_option('b_sanitize') != ''
  add_project_link_arguments('-shared-libsan', language : 'cpp')
endif

# Clang gets grumpy about missing libasan symbols if -shared-libasan is not
# passed when building shared libs, at least on Linux
if cxx.get_id() == 'clang' and 'address' in get_option('b_sanitize')
  add_project_link_arguments('-shared-libasan', language : 'cpp')
endif

add_project_link_arguments('-pthread', language : 'cpp')

if cxx.get_linker_id() in ['ld.bfd', 'ld.gold']
  add_project_link_arguments('-Wl,--no-copy-dt-needed-entries', language : 'cpp')
endif

if is_freebsd
  # FreeBSD's `environ` is defined in `crt1.o`, not `libc.so`,
  # so the linker thinks it's undefined
  add_project_link_arguments('-Wl,-z,undefs', language: 'cpp')
endif

# Generate Chromium tracing files for each compiled file, which enables
# maintainers/buildtime_report.sh BUILD-DIR to simply work in clang builds.
#
# They can also be manually viewed at https://ui.perfetto.dev
if get_option('profile-build').require(cxx.get_id() == 'clang').enabled()
  add_project_arguments('-ftime-trace', language: 'cpp')
endif

if cxx.get_id() in ['clang', 'gcc']
  add_project_arguments([
      # Meson uses out of source builds, conventionally usually in a subdirectory
      # of the source tree (e.g. meson setup ./build). This means that unlike in
      # the previous Make buildsystem, all compilation sources are passed as a relative
      # parent, e.g. `cc -o lix/libexpr/nixexpr.cc.o ../lix/libexpr/nixexpr.cc`.
      # These paths show up when debugging, and in asserts, which look both look strange
      # and confuse debuggers.
      # So let's just tell GCC and Clang that ../lix really means lix.
      '-ffile-prefix-map=../lix=lix',
    ],
    language : 'cpp',
  )
endif

if is_darwin
  fs.copyfile(
    'misc/launchd/org.nixos.nix-daemon.plist.in',
    'org.nixos.nix-daemon.plist',
    install : true,
    install_dir : prefix / 'Library/LaunchDaemons',
  )
endif

capnpc_wrapper = custom_target(
  'capnpc_wrapper',
  command : ['cp', '@INPUT@', '@OUTPUT@'],
  input : configure_file(
    input : 'lix/code-generation/capnpc_wrapper.py',
    output : 'capnpc_wrapper.configured',
    configuration : {
      'capnp_include' : capnp_rpc.get_variable(pkgconfig : 'includedir'),
      'capnp' : find_program('capnp', required : true, native : true).full_path(),
      'python' : python.full_path(),
    },
  ),
  output : 'capnpc_wrapper',
)

subdir('lix')
subdir('scripts')
subdir('misc')

if enable_docs
  subdir('doc/manual')
endif

if enable_tests
  subdir('tests/unit')
  subdir('tests/functional')
  subdir('tests/functional2')
endif

subdir('meson/clang-tidy')

subproject('nix-eval-jobs', required : enable_nix_eval_jobs)
