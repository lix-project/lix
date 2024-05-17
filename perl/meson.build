project('lix-perl', 'cpp',
  version : run_command('bash', '-c', 'echo -n $(cat ../.version)$VERSION_SUFFIX', check : true).stdout().strip(),
  default_options : [
    'cpp_std=c++2a',
    # TODO(Qyriad): increase the warning level
    'debug=true',
    # FIXME(Qyriad): should this be -O2? The main nix build was switched to -O2 in 3c5234430
    'optimization=3',
  ],
)

fs = import('fs')

prefix = get_option('prefix')
libdir = get_option('libdir')
if not fs.is_absolute(libdir)
  libdir = prefix / libdir
endif

cxx = meson.get_compiler('cpp')

# Really version 5.8.0, but Perl's version string is of the form
# "This is perl 5, version 38, subversion 2", for 5.38.2, so as far
# as Meson is concerned, the version of Perl we need is 8 or greater.
perl = find_program('perl', version : '>=8')

# "compiler to convert Perl XS code into C code"
xsubpp = find_program('xsubpp')

perl_version = run_command(
  perl,
  '-e',
  'use Config; print $Config{version};',
  capture : true,
  check : true,
).stdout()
perl_arch_name = run_command(
  perl,
  '-e',
  'use Config; print $Config{archname};',
  capture : true,
  check : true,
).stdout()

perl_libdir = f'@libdir@/perl5/site_perl/@perl_version@/@perl_arch_name@'

perl_incdir = run_command(
  perl,
  '-e',
  'use Config; print $Config{archlibexp};',
  capture : true,
  check : true,
).stdout() + '/CORE'

perl_include = declare_dependency(
  # This must have is_system : true, or #include "config.h" will get perl's config.h
  # instead of Nix's.
  include_directories : include_directories(perl_incdir, is_system : true),
)

sodium = dependency('libsodium', 'sodium', required : true)

if cxx.get_linker_id() in ['ld.bfd', 'ld.gold']
  add_project_link_arguments('-Wl,--no-copy-dt-needed-entries', language : 'cpp')
endif

libstore = dependency('lixstore', 'lix-store', required : true)

subdir('lib/Nix')
