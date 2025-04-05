libutil_sources = files(
  # keep-sorted start
  'archive.cc',
  'args.cc',
  'async-io.cc',
  'canon-path.cc',
  'cgroup.cc',
  'compression.cc',
  'compute-levels.cc',
  'config.cc',
  'current-process.cc',
  'deprecated-features.cc',
  'english.cc',
  'environment-variables.cc',
  'error.cc',
  'escape-char.cc',
  'escape-string.cc',
  'exit.cc',
  'experimental-features.cc',
  'file-descriptor.cc',
  'file-system.cc',
  'fmt.cc',
  'git.cc',
  'hash.cc',
  'hilite.cc',
  'json-utils.cc',
  'logging.cc',
  'monitor-fd.cc',
  'mount.cc',
  'namespaces.cc',
  'position.cc',
  'print-elided.cc',
  'processes.cc',
  'references.cc',
  'regex.cc',
  'serialise.cc',
  'shlex.cc',
  'signals.cc',
  'source-path.cc',
  'strings.cc',
  'suggestions.cc',
  'tarfile.cc',
  'terminal.cc',
  'thread-name.cc',
  'thread-pool.cc',
  'unix-domain-socket.cc',
  'url-name.cc',
  'url.cc',
  'users.cc',
  'xml-writer.cc',
  # keep-sorted end
)

libutil_headers = files(
  # keep-sorted start
  'abstract-setting-to-json.hh',
  'ansicolor.hh',
  'apply-config-options.hh',
  'archive.hh',
  'args.hh',
  'args/root.hh',
  'async-collect.hh',
  'async-io.hh',
  'async-semaphore.hh',
  'async.hh',
  'backed-string-view.hh',
  'box_ptr.hh',
  'canon-path.hh',
  'cgroup.hh',
  'charptr-cast.hh',
  'checked-arithmetic.hh',
  'chunked-vector.hh',
  'closure.hh',
  'comparator.hh',
  'compression.hh',
  'compute-levels.hh',
  'concepts.hh',
  'config-impl.hh',
  'config.hh',
  'current-process.hh',
  'deprecated-features.hh',
  'english.hh',
  'environment-variables.hh',
  'error.hh',
  'escape-char.hh',
  'escape-string.hh',
  'exit.hh',
  'experimental-features.hh',
  'file-descriptor.hh',
  'file-system.hh',
  'finally.hh',
  'fmt.hh',
  'generator.hh',
  'git.hh',
  'hash.hh',
  'hilite.hh',
  'input-accessor.hh',
  'json-fwd.hh',
  'json.hh',
  'logging.hh',
  'lru-cache.hh',
  'manually-drop.hh',
  'monitor-fd.hh',
  'mount.hh',
  'namespaces.hh',
  'notifying-counter.hh',
  'pool.hh',
  'position.hh',
  'print-elided.hh',
  'processes.hh',
  'ref.hh',
  'references.hh',
  'regex-combinators.hh',
  'regex.hh',
  'repair-flag.hh',
  'result.hh',
  'serialise.hh',
  'shlex.hh',
  'signals.hh',
  'source-path.hh',
  'split.hh',
  'strings.hh',
  'suggestions.hh',
  'sync.hh',
  'tarfile.hh',
  'terminal.hh',
  'thread-name.hh',
  'thread-pool.hh',
  'topo-sort.hh',
  'tracepoint.hh',
  'types.hh',
  'unix-domain-socket.hh',
  'url-name.hh',
  'url-parts.hh',
  'url.hh',
  'users.hh',
  'variant-wrapper.hh',
  'xml-writer.hh',
  # keep-sorted end
)

experimental_feature_definitions = files(
  # keep-sorted start
  'experimental-features/auto-allocate-uids.md',
  'experimental-features/ca-derivations.md',
  'experimental-features/cgroups.md',
  'experimental-features/daemon-trust-override.md',
  'experimental-features/dynamic-derivations.md',
  'experimental-features/fetch-closure.md',
  'experimental-features/flakes.md',
  'experimental-features/impure-derivations.md',
  'experimental-features/lix-custom-sub-commands.md',
  'experimental-features/nix-command.md',
  'experimental-features/no-url-literals.md',
  'experimental-features/parse-toml-timestamps.md',
  'experimental-features/pipe-operator.md',
  'experimental-features/read-only-local-store.md',
  'experimental-features/recursive-nix.md',
  'experimental-features/repl-automation.md',
  # keep-sorted end
)

deprecated_feature_definitions = files(
  # keep-sorted start
  'deprecated-features/ancient-let.md',
  'deprecated-features/cr-line-endings.md',
  'deprecated-features/nul-bytes.md',
  'deprecated-features/rec-set-overrides.md',
  'deprecated-features/shadow-internal-symbols.md',
  'deprecated-features/url-literals.md',
  # keep-sorted end
)

experimental_features_gen = custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_experimental_features.py',
    '--header', '@OUTPUT0@',
    '--impl-header', '@OUTPUT1@',
    '--shortlist', '@OUTPUT2@',
    '--descriptions', '@OUTPUT3@',
    '@INPUT@',
  ],
  input : experimental_feature_definitions,
  output : [
    'experimental-features.gen.inc',
    'experimental-features-impl.gen.inc',
    'experimental-features-shortlist.md',
    'experimental-feature-descriptions.md',
  ],
  install : true,
  install_dir: [
    includedir / 'lix/libutil',
    false,
    false,
    false,
  ],
)
experimental_features_header = experimental_features_gen[0]
experimental_features_impl_header = experimental_features_gen[1]
experimental_features_shortlist_md = experimental_features_gen[2]
experimental_feature_descriptions_md = experimental_features_gen[3]

deprecated_features_gen = custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_experimental_features.py',
    '--deprecated',
    '--header', '@OUTPUT0@',
    '--impl-header', '@OUTPUT1@',
    '--shortlist', '@OUTPUT2@',
    '--descriptions', '@OUTPUT3@',
    '@INPUT@',
  ],
  input : deprecated_feature_definitions,
  output : [
    'deprecated-features.gen.inc',
    'deprecated-features-impl.gen.inc',
    'deprecated-features-shortlist.md',
    'deprecated-feature-descriptions.md',
  ],
  install : true,
  install_dir: [
    includedir / 'lix/libutil',
    false,
    false,
    false
  ],
)
deprecated_features_header = deprecated_features_gen[0]
deprecated_features_impl_header = deprecated_features_gen[1]
deprecated_features_shortlist_md = deprecated_features_gen[2]
deprecated_feature_descriptions_md = deprecated_features_gen[3]

libutil_settings_headers = []

archive_setting_definitions = files(
  'archive-settings/preallocate-contents.md',
  'archive-settings/use-case-hack.md',
)
libutil_settings_headers += custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_settings.py',
    '--kernel', host_machine.system(),
    '--header', '@OUTPUT@',
    '--experimental-features', '@SOURCE_ROOT@/lix/libutil/experimental-features',
    '@INPUT@',
  ],
  input : archive_setting_definitions,
  output : 'archive-settings.gen.inc',
)

feature_setting_definitions = files(
  'feature-settings/deprecated-features.md',
  'feature-settings/experimental-features.md',
)
libutil_settings_headers += custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_settings.py',
    '--kernel', host_machine.system(),
    '--header', '@OUTPUT@',
    '--experimental-features', '@SOURCE_ROOT@/lix/libutil/experimental-features',
    '@INPUT@',
  ],
  input : feature_setting_definitions,
  output : 'feature-settings.gen.inc',
  install : true,
  install_dir : includedir / 'lix/libutil',
)

logging_setting_definitions = files(
  'logging-settings/show-trace.md',
)
libutil_settings_headers += custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_settings.py',
    '--kernel', host_machine.system(),
    '--header', '@OUTPUT@',
    '--experimental-features', '@SOURCE_ROOT@/lix/libutil/experimental-features',
    '@INPUT@',
  ],
  input : logging_setting_definitions,
  output : 'logging-settings.gen.inc',
  install : true,
  install_dir : includedir / 'lix/libutil',
)

libutil = library(
  'lixutil',
  libutil_sources,
  experimental_features_header,
  experimental_features_impl_header,
  deprecated_features_header,
  deprecated_features_impl_header,
  libutil_settings_headers,
  dependencies : [
    aws_sdk,
    aws_s3,
    boehm,
    boost,
    cpuid,
    seccomp,
    libarchive,
    brotli,
    openssl,
    nlohmann_json,
    kj,
  ],
  include_directories : [ '../..' ],
  cpp_pch : cpp_pch,
  implicit_include_directories : true,
  install : true,
)

install_headers(libutil_headers, subdir : 'lix/libutil', preserve_path : true)

# FIXME: not using the pkg-config module because it creates way too many deps
# while meson migration is in progress, and we want to not include boost here
configure_file(
  input : 'lix-util.pc.in',
  output : 'lix-util.pc',
  install_dir : libdir / 'pkgconfig',
  configuration : {
    'prefix' : prefix,
    'libdir' : libdir,
    'includedir' : includedir,
    'PACKAGE_VERSION' : meson.project_version(),
  },
)

# Used by libstore and libfetchers.
liblixutil = declare_dependency(
  include_directories : include_directories('../..'),
  sources : [
    experimental_features_header,
    deprecated_features_header,
    libutil_settings_headers,
  ],
  dependencies: [
    # Everything has to link to kj if it uses libutil internally (ensured by
    # lix-base pkg-config externally)
    kj,
  ],
  link_with : libutil
)

# FIXME: remove when https://git.lix.systems/lix-project/lix/issues/359 is fixed.
if is_static
  liblixutil_mstatic = declare_dependency(
    include_directories : include_directories('../..'),
    sources : [
      experimental_features_header,
      deprecated_features_header,
      libutil_settings_headers,
    ],
    dependencies: [
      # Everything has to link to kj if it uses libutil internally (ensured by
      # lix-base pkg-config externally)
      kj,
    ],
    link_whole : libutil,
  )
else
  liblixutil_mstatic = liblixutil
endif
meson.override_dependency('lix-util', liblixutil_mstatic)
