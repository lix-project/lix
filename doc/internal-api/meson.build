internal_api_sources = [
  'lix/libcmd',
  'lix/libexpr',
  'lix/libexpr/flake',
  'tests/unit/libexpr',
  'tests/unit/libexpr/value',
  'tests/unit/libexpr/test',
  'tests/unit/libexpr/test/value',
  'lix/libexpr/value',
  'lix/libfetchers',
  'lix/libmain',
  'lix/libstore',
  'lix/libstore/build',
  'lix/libstore/builtins',
  'tests/unit/libstore',
  'tests/unit/libstore/test',
  'lix/libutil',
  'tests/unit/libutil',
  'tests/unit/libutil/test',
  'lix/nix',
  'lix/nix-env',
  'lix/nix-store',
]

# We feed Doxygen absolute paths so it can be invoked from any working directory.
internal_api_sources_absolute = []
foreach src : internal_api_sources
  internal_api_sources_absolute += '"' + (meson.project_source_root() / src) + '"'
endforeach

internal_api_sources_oneline = ' \\\n  '.join(internal_api_sources_absolute)

doxygen_cfg = configure_file(
  input : 'doxygen.cfg.in',
  output : 'doxygen.cfg',
  configuration : {
    'PACKAGE_VERSION': meson.project_version(),
    'RAPIDCHECK_HEADERS': rapidcheck_meson.get_variable('includedir'),
    'docdir' : meson.current_build_dir(),
    'INPUT_PATHS' : internal_api_sources_oneline,
    'PROJECT_SOURCE_ROOT' : meson.project_source_root(),
  },
)

internal_api_docs = custom_target(
  'internal-api-docs',
  command : [
    doxygen.full_path(),
    '@INPUT0@',
  ],
  input : [
    doxygen_cfg,
  ],
  output : 'html',
  install : true,
  install_dir : datadir / 'doc/nix/internal-api',
  build_always_stale : true,
)

alias_target('internal-api-html', internal_api_docs)
