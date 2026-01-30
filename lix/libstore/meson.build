foreach header : [ 'schema.sql' ]
  liblix_generated_headers += custom_target(
    command : [ 'bash', '-c', 'echo \'R"__NIX_STR(\' | cat - @INPUT@ && echo \')__NIX_STR"\'' ],
    input : header,
    output : '@PLAINNAME@.gen.hh',
    capture : true,
    install : true,
    install_dir : includedir / 'lix/libstore',
  )
endforeach

libstore_rpc = []

libstore_rpc += custom_target(
  command : [
    capnpc_wrapper,
    '--language=c++',
    '--src-prefix=@CURRENT_SOURCE_DIR@',
    '--outdir=@OUTDIR@',
    '--depfile=@DEPFILE@',
    '-I@SOURCE_ROOT@',
    '@INPUT@',
  ],
  input : files(
    # keep-sorted start
    'types.capnp',
    # keep-sorted end
  ),
  output : [
    '@PLAINNAME@.h',
    '@PLAINNAME@.c++',
  ],
  depfile : '@PLAINNAME@.d',
)

# rpc definitions only here to get relative paths of generated files right
subdir('build')

foreach i : libstore_rpc
  foreach rpc_src : i.to_list()
    if rpc_src.full_path().endswith('.h')
      liblix_generated_headers += [ rpc_src ]
    else
      liblix_sources += [ rpc_src ]
    endif
  endforeach
endforeach

file_transfer_setting_definitions = files(
  # keep-sorted start
  'file-transfer-settings/download-attempts.md',
  'file-transfer-settings/http-connections.md',
  'file-transfer-settings/http2.md',
  'file-transfer-settings/http3.md',
  'file-transfer-settings/initial-connect-timeout.md',
  'file-transfer-settings/max-connect-timeout.md',
  'file-transfer-settings/stalled-download-timeout.md',
  'file-transfer-settings/user-agent-suffix.md',
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
  input : file_transfer_setting_definitions,
  output : 'file-transfer-settings.gen.inc',
  install : true,
  install_dir : includedir / 'lix/libstore',
)

libstore_setting_definitions = files(
  # keep-sorted start
  'settings/allow-symlinked-store.md',
  'settings/allowed-impure-host-deps.md',
  'settings/always-allow-substitutes.md',
  'settings/auto-allocate-uids.md',
  'settings/auto-optimise-store.md',
  'settings/build-dir.md',
  'settings/build-hook.md',
  'settings/build-poll-interval.md',
  'settings/build-users-group.md',
  'settings/builders-use-substitutes.md',
  'settings/builders.md',
  'settings/builtin-builder-sandbox-paths.md',
  'settings/compress-build-log.md',
  'settings/cores.md',
  'settings/darwin-log-sandbox-violations.md',
  'settings/diff-hook.md',
  'settings/download-speed.md',
  'settings/enable-core-dumps.md',
  'settings/extra-platforms.md',
  'settings/fallback.md',
  'settings/fsync-metadata.md',
  'settings/gc-reserved-space.md',
  'settings/hashed-mirrors.md',
  'settings/id-count.md',
  'settings/ignored-acls.md',
  'settings/keep-build-log.md',
  'settings/keep-derivations.md',
  'settings/keep-env-derivations.md',
  'settings/keep-failed.md',
  'settings/keep-going.md',
  'settings/keep-outputs.md',
  'settings/log-lines.md',
  'settings/max-build-log-size.md',
  'settings/max-free.md',
  'settings/max-jobs.md',
  'settings/max-silent-time.md',
  'settings/max-substitution-jobs.md',
  'settings/min-free-check-interval.md',
  'settings/min-free.md',
  'settings/nar-buffer-size.md',
  'settings/narinfo-cache-negative-ttl.md',
  'settings/narinfo-cache-positive-ttl.md',
  'settings/netrc-file.md',
  'settings/pasta-path.md',
  'settings/plugin-files.md',
  'settings/post-build-hook.md',
  'settings/pre-build-hook.md',
  'settings/print-missing.md',
  'settings/require-drop-supplementary-groups.md',
  'settings/require-sigs.md',
  'settings/run-diff-hook.md',
  'settings/sandbox-build-dir.md',
  'settings/sandbox-dev-shm-size.md',
  'settings/sandbox-fallback.md',
  'settings/sandbox-paths.md',
  'settings/sandbox.md',
  'settings/secret-key-files.md',
  'settings/ssl-cert-file.md',
  'settings/start-id.md',
  'settings/store.md',
  'settings/substitute.md',
  'settings/substituters.md',
  'settings/sync-before-registering.md',
  'settings/system-features.md',
  'settings/system.md',
  'settings/tarball-ttl.md',
  'settings/temp-dir.md',
  'settings/timeout.md',
  'settings/trusted-public-keys.md',
  'settings/trusted-substituters.md',
  'settings/use-cgroups.md',
  'settings/use-sqlite-wal.md',
  'settings/use-xdg-base-directories.md',
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
  input : libstore_setting_definitions,
  output : 'libstore-settings.gen.inc',
  install : true,
  install_dir : includedir / 'lix/libstore',
)

if dtrace_feature.enabled()
  liblix_generated_headers += dtrace_header_gen.process('trace-probes.d')
  liblix_sources += dtrace_object_gen.process('trace-probes.d')
endif

liblix_sources += files(
  # keep-sorted start
  'binary-cache-store.cc',
  'build-result.cc',
  'build/child.cc',
  'build/derivation-goal.cc',
  'build/entry-points.cc',
  'build/goal.cc',
  'build/hook-instance.cc',
  'build/local-derivation-goal.cc',
  'build/personality.cc',
  'build/substitution-goal.cc',
  'build/worker.cc',
  'builtins/buildenv.cc',
  'builtins/fetchurl.cc',
  'builtins/unpack-channel.cc',
  'common-protocol.cc',
  'content-address.cc',
  'crypto.cc',
  'daemon.cc',
  'derivations.cc',
  'derived-path.cc',
  'dummy-store.cc',
  'export-import.cc',
  'filetransfer.cc',
  'gc.cc',
  'globals.cc',
  'http-binary-cache-store.cc',
  'legacy-ssh-store.cc',
  'local-binary-cache-store.cc',
  'local-fs-store.cc',
  'local-store.cc',
  'lock.cc',
  'log-store.cc',
  'machines.cc',
  'make-content-addressed.cc',
  'misc.cc',
  'names.cc',
  'nar-accessor.cc',
  'nar-info-disk-cache.cc',
  'nar-info.cc',
  'optimise-store.cc',
  'outputs-spec.cc',
  'parsed-derivations.cc',
  'path-info.cc',
  'path-references.cc',
  'path-tree.cc',
  'path-with-outputs.cc',
  'path.cc',
  'pathlocks.cc',
  'platform.cc',
  'profiles.cc',
  'realisation.cc',
  'remote-fs-accessor.cc',
  'remote-store.cc',
  's3-binary-cache-store.cc',
  'serve-protocol.cc',
  'sqlite.cc',
  'ssh-store.cc',
  'ssh.cc',
  'store-api.cc',
  'temporary-dir.cc',
  'uds-remote-store.cc',
  'worker-protocol.cc',
  # keep-sorted end
)


libstore_headers = files(
  # keep-sorted start
  'binary-cache-store.hh',
  'build-result.hh',
  'build/child.hh',
  'build/derivation-goal.hh',
  'build/goal.hh',
  'build/hook-instance.hh',
  'build/local-derivation-goal.hh',
  'build/personality.hh',
  'build/substitution-goal.hh',
  'build/worker.hh',
  'builtins.hh',
  'builtins/buildenv.hh',
  'common-protocol-impl.hh',
  'common-protocol.hh',
  'content-address.hh',
  'crypto.hh',
  'daemon.hh',
  'derivations.hh',
  'derived-path.hh',
  'dummy-store.hh',
  'filetransfer.hh',
  'fs-accessor.hh',
  'gc-store.hh',
  'globals.hh',
  'http-binary-cache-store.hh',
  'indirect-root-store.hh',
  'legacy-ssh-store.hh',
  'length-prefixed-protocol-helper.hh',
  'local-binary-cache-store.hh',
  'local-fs-store.hh',
  'local-store.hh',
  'lock.hh',
  'log-store.hh',
  'machines.hh',
  'make-content-addressed.hh',
  'names.hh',
  'nar-accessor.hh',
  'nar-info-disk-cache.hh',
  'nar-info.hh',
  'outputs-spec.hh',
  'parsed-derivations.hh',
  'path-info.hh',
  'path-references.hh',
  'path-regex.hh',
  'path-tree.hh',
  'path-with-outputs.hh',
  'path.hh',
  'pathlocks.hh',
  'profiles.hh',
  'realisation.hh',
  'remote-fs-accessor.hh',
  'remote-store-connection.hh',
  'remote-store.hh',
  's3-binary-cache-store.hh',
  's3.hh',
  'serve-protocol-impl.hh',
  'serve-protocol.hh',
  'sqlite.hh',
  'ssh-store.hh',
  'ssh.hh',
  'store-api.hh',
  'store-cast.hh',
  'temporary-dir.hh',
  'types-rpc.hh',
  'uds-remote-store.hh',
  'worker-protocol-impl.hh',
  'worker-protocol.hh',
  # keep-sorted end
)

if host_machine.system() == 'linux'
  liblix_sources += files('platform/linux.cc')
  libstore_headers += files('platform/linux.hh')
elif host_machine.system() == 'darwin'
  liblix_sources += files('platform/darwin.cc')
  libstore_headers += files('platform/darwin.hh')
elif host_machine.system() == 'freebsd'
  liblix_sources += files('platform/freebsd.cc')
  libstore_headers += files('platform/freebsd.hh')
else
  liblix_sources += files('platform/fallback.cc')
  libstore_headers += files('platform/fallback.hh')
endif

install_headers(libstore_headers, subdir : 'lix/libstore', preserve_path : true)
