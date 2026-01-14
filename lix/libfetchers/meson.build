liblix_sources += files(
  'attrs.cc',
  'cache.cc',
  'fetch-settings.cc',
  'fetch-to-store.cc',
  'fetchers.cc',
  'git.cc',
  'github.cc',
  'indirect.cc',
  'mercurial.cc',
  'path.cc',
  'registry.cc',
  'tarball.cc',
)

libfetchers_headers = files(
  'attrs.hh',
  'cache.hh',
  'fetch-settings.hh',
  'fetch-to-store.hh',
  'fetchers.hh',
  'registry.hh',
)

libfetchers_setting_definitions = files(
  'settings/accept-flake-config.md',
  'settings/access-tokens.md',
  'settings/allow-dirty.md',
  'settings/commit-lockfile-summary.md',
  'settings/flake-registry.md',
  'settings/use-registries.md',
  'settings/warn-dirty.md',
)
liblix_generated_headers += custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_settings.py',
    '--kernel', host_machine.system(),
    '--header', '@OUTPUT@',
    '--experimental-features', '@SOURCE_ROOT@/lix/libutil/experimental-features',
    '@INPUT@',
  ],
  input : libfetchers_setting_definitions,
  output : 'libfetchers-settings.gen.inc',
  install : true,
  install_dir : includedir / 'lix/libfetchers',
)

install_headers(libfetchers_headers, subdir : 'lix/libfetchers', preserve_path : true)
