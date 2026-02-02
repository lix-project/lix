liblix_sources += files(
  # keep-sorted start
  'abstract-config.cc',
  'archive.cc',
  'args.cc',
  'async-io.cc',
  'c-calls.cc',
  'canon-path.cc',
  'cgroup.cc',
  'compression.cc',
  'compute-levels.cc',
  'config.cc',
  'current-process.cc',
  'deprecated-features.cc',
  'english.cc',
  'environment-variables.cc',
  'error-trace.cc',
  'error.cc',
  'escape-char.cc',
  'escape-string.cc',
  'experimental-features.cc',
  'file-descriptor.cc',
  'file-system.cc',
  'fmt.cc',
  'git.cc',
  'hash.cc',
  'hilite.cc',
  'io-buffer.cc',
  'json-utils.cc',
  'log-format.cc',
  'logging-rpc.cc',
  'logging.cc',
  'monitor-fd.cc',
  'namespaces.cc',
  'position.cc',
  'print-elided.cc',
  'processes.cc',
  'references.cc',
  'regex.cc',
  'serialise-async.cc',
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
  'abstract-config.hh',
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
  'c-calls.hh',
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
  'error-trace.hh',
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
  'io-buffer.hh',
  'json-fwd.hh',
  'json.hh',
  'linear-map.hh',
  'log-format.hh',
  'logging-rpc.hh',
  'logging.hh',
  'lru-cache.hh',
  'manually-drop.hh',
  'monitor-fd.hh',
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
  'rpc-fwd.hh',
  'rpc.hh',
  'serialise-async.hh',
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
  'types-rpc.hh',
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
  'experimental-features/cgroups.md',
  'experimental-features/coerce-integers.md',
  'experimental-features/daemon-trust-override.md',
  'experimental-features/flake-self-attrs.md',
  'experimental-features/flakes.md',
  'experimental-features/lix-custom-sub-commands.md',
  'experimental-features/nix-command.md',
  'experimental-features/pipe-operator.md',
  'experimental-features/read-only-local-store.md',
  'experimental-features/repl-automation.md',
  # keep-sorted end
)

deprecated_feature_definitions = files(
  # keep-sorted start
  'deprecated-features/ancient-let.md',
  'deprecated-features/broken-string-escape.md',
  'deprecated-features/broken-string-indentation.md',
  'deprecated-features/cr-line-endings.md',
  'deprecated-features/floating-without-zero.md',
  'deprecated-features/nix-path-shadow.md',
  'deprecated-features/nul-bytes.md',
  'deprecated-features/or-as-identifier.md',
  'deprecated-features/rec-set-dynamic-attrs.md',
  'deprecated-features/rec-set-merges.md',
  'deprecated-features/rec-set-overrides.md',
  'deprecated-features/shadow-internal-symbols.md',
  'deprecated-features/tokens-no-whitespace.md',
  'deprecated-features/url-literals.md',
  # keep-sorted end
)

experimental_features_gen = custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_extra_features.py',
    '--header', '@OUTPUT0@',
    '--impl-header', '@OUTPUT1@',
    '--shortlist', '@OUTPUT2@',
    '--docs', '@OUTPUT3@',
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
liblix_generated_headers += [
  experimental_features_gen[0],
  experimental_features_gen[1],
]
experimental_features_shortlist_md = experimental_features_gen[2]
experimental_feature_descriptions_md = experimental_features_gen[3]

deprecated_features_gen = custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_extra_features.py',
    '--deprecated',
    '--header', '@OUTPUT0@',
    '--impl-header', '@OUTPUT1@',
    '--shortlist', '@OUTPUT2@',
    '--docs', '@OUTPUT3@',
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
liblix_generated_headers += [
  deprecated_features_gen[0],
  deprecated_features_gen[1],
]
deprecated_features_shortlist_md = deprecated_features_gen[2]
deprecated_feature_descriptions_md = deprecated_features_gen[3]

archive_setting_definitions = files(
  'archive-settings/preallocate-contents.md',
  'archive-settings/use-case-hack.md',
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
  input : archive_setting_definitions,
  output : 'archive-settings.gen.inc',
)

feature_setting_definitions = files(
  'feature-settings/deprecated-features.md',
  'feature-settings/experimental-features.md',
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
  input : feature_setting_definitions,
  output : 'feature-settings.gen.inc',
  install : true,
  install_dir : includedir / 'lix/libutil',
)

logging_setting_definitions = files(
  # keep-sorted start
  'logging-settings/log-format.md',
  'logging-settings/show-trace.md',
  # keep-sorted end
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
  input : logging_setting_definitions,
  output : 'logging-settings.gen.inc',
  install : true,
  install_dir : includedir / 'lix/libutil',
)

libutil_rpc_schemas = files(
  # keep-sorted start
  'logging.capnp',
  'types.capnp',
  # keep-sorted end
)

foreach rpc : libutil_rpc_schemas
  rpc = custom_target(
    command : [
      capnpc_wrapper,
      '--language=c++',
      '--src-prefix=@CURRENT_SOURCE_DIR@',
      '--outdir=@OUTDIR@',
      '--depfile=@DEPFILE@',
      '-I@SOURCE_ROOT@',
      '@INPUT@',
    ],
    input : rpc,
    output : [
      '@PLAINNAME@.h',
      '@PLAINNAME@.c++',
    ],
    install : true,
    install_dir : [includedir / 'lix/libutil', false],
    depfile : '@PLAINNAME@.d',
  )

  liblix_generated_headers += rpc[0]
  liblix_sources += rpc[1]
endforeach

install_headers(libutil_headers, subdir : 'lix/libutil', preserve_path : true)
