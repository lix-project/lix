doxygen_cfg = configure_file(
  input : 'doxygen.cfg.in',
  output : 'doxygen.cfg',
  configuration : {
    'PACKAGE_VERSION': meson.project_version(),
    'RAPIDCHECK_HEADERS': rapidcheck_meson.get_variable('includedir'),
    'docdir' : meson.current_build_dir(),
  },
)

internal_api_docs = custom_target(
  'internal-api-docs',
  command : [
    bash,
    # Meson can you please just give us a `workdir` argument to custom targets...
    '-c',
    # We have to prefix the doxygen_cfg path with the project build root
    # because of the cd in front.
    'cd @0@ && @1@ @2@/@INPUT0@'.format(
      meson.project_source_root(),
      doxygen.full_path(),
      meson.project_build_root(),
    ),
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
