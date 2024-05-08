libstore_generated_headers = []
foreach header : [ 'schema.sql', 'ca-specific-schema.sql' ]
  libstore_generated_headers += custom_target(
    command : [ 'bash', '-c', 'echo \'R"__NIX_STR(\' | cat - @INPUT@ && echo \')__NIX_STR"\'' ],
    input : header,
    output : '@PLAINNAME@.gen.hh',
    capture : true,
    install : true,
    install_dir : includedir / 'lix/libstore',
  )
endforeach

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
  libstore_generated_headers += embedded_sandbox_shell_gen
endif

libstore_sources = files(
  'binary-cache-store.cc',
  'build-result.cc',
  'common-protocol.cc',
  'content-address.cc',
  'crypto.cc',
  'daemon.cc',
  'derivations.cc',
  'derived-path-map.cc',
  'derived-path.cc',
  'downstream-placeholder.cc',
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
  'uds-remote-store.cc',
  'worker-protocol.cc',
  'build/derivation-goal.cc',
  'build/drv-output-substitution-goal.cc',
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
)


libstore_headers = files(
  'binary-cache-store.hh',
  'build/derivation-goal.hh',
  'build/drv-output-substitution-goal.hh',
  'build/goal.hh',
  'build/hook-instance.hh',
  'build/local-derivation-goal.hh',
  'build/personality.hh',
  'build/substitution-goal.hh',
  'build/worker.hh',
  'build-result.hh',
  'builtins/buildenv.hh',
  'builtins.hh',
  'common-protocol-impl.hh',
  'common-protocol.hh',
  'content-address.hh',
  'crypto.hh',
  'daemon.hh',
  'derivations.hh',
  'derived-path-map.hh',
  'derived-path.hh',
  'downstream-placeholder.hh',
  'filetransfer.hh',
  'fs-accessor.hh',
  'gc-store.hh',
  'globals.hh',
  'indirect-root-store.hh',
  'length-prefixed-protocol-helper.hh',
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
  'ssh-store-config.hh',
  'ssh.hh',
  'store-api.hh',
  'store-cast.hh',
  'uds-remote-store.hh',
  'worker-protocol-impl.hh',
  'worker-protocol.hh',
)

if host_machine.system() == 'linux'
  libstore_sources += files('platform/linux.cc')
  libstore_headers += files('platform/linux.hh')
elif host_machine.system() == 'darwin'
  libstore_sources += files('platform/darwin.cc')
  libstore_headers += files('platform/darwin.hh')
else
  libstore_sources += files('platform/fallback.cc')
  libstore_headers += files('platform/fallback.hh')
endif

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

if busybox.found()
  cpp_str_defines += {
    'SANDBOX_SHELL': busybox.full_path()
  }
endif

cpp_args = []

foreach name, value : cpp_str_defines
  cpp_args += [
    '-D' + name + '=' + '"' + value + '"'
  ]
endforeach

libstore = library(
  'lixstore',
  libstore_generated_headers,
  libstore_sources,
  dependencies : [
    libarchive,
    liblixutil, # Internal.
    seccomp,
    sqlite,
    sodium,
    curl,
    openssl,
    aws_sdk,
    aws_s3,
    aws_sdk_transfer,
    nlohmann_json,
  ],
  cpp_args : cpp_args,
  install : true,
  # FIXME(Qyriad): is this right?
  install_rpath : libdir,
)

install_headers(libstore_headers, subdir : 'lix/libstore', preserve_path : true)

# Used by libfetchers.
liblixstore = declare_dependency(
  include_directories : include_directories('.'),
  link_with : libstore,
)

# FIXME: not using the pkg-config module because it creates way too many deps
# while meson migration is in progress, and we want to not include boost here
configure_file(
  input : 'lix-store.pc.in',
  output : 'lix-store.pc',
  install_dir : libdir / 'pkgconfig',
  configuration : {
    'prefix' : prefix,
    'libdir' : libdir,
    'includedir' : includedir,
    'PACKAGE_VERSION' : meson.project_version(),
  },
)
