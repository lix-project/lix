ls = find_program('ls', native : true)

test_confdata = {
  'bindir': bindir,
  'ls': fs.parent(ls.full_path()),
  'lsof': lsof.full_path(),
  'dot': dot.found() ? dot.full_path() : '',
  'bash': bash.full_path(),
  'sandbox_shell': busybox.found() ? busybox.full_path() : '',
  'PACKAGE_VERSION': meson.project_version(),
  'system': host_system,
  'python': python.full_path(),
  'runinpty': runinpty.full_path(),
}

# Just configures `common/vars-and-functions.sh.in`.
# Done as a subdir() so Meson places it under `common` in the build directory as well.
subdir('common')

config_nix_in = configure_file(
  input : 'config.nix.in',
  output : 'config.nix',
  configuration : test_confdata,
)

# Just configures `nix-shell/config.nix.in`. Done as a subdir() for the same reason as above.
subdir('nix-shell')

subdir('plugins')
subdir('test-libstoreconsumer')

functional_tests_scripts = [
  'init.sh',
  'test-infra.sh',
  'flakes/flakes.sh',
  'flakes/develop.sh',
  'flakes/develop-r8854.sh',
  'flakes/run.sh',
  'flakes/mercurial.sh',
  'flakes/circular.sh',
  'flakes/init.sh',
  'flakes/inputs.sh',
  'flakes/follow-paths.sh',
  'flakes/bundle.sh',
  'flakes/check.sh',
  'flakes/unlocked-override.sh',
  'flakes/absolute-paths.sh',
  'flakes/build-paths.sh',
  'flakes/flake-in-submodule.sh',
  'flakes/flake-metadata.sh',
  'flakes/flake-registry.sh',
  'flakes/subdir-flake.sh',
  'gc.sh',
  'nix-collect-garbage-d.sh',
  'nix-collect-garbage-dry-run.sh',
  'remote-store.sh',
  'legacy-ssh-store.sh',
  'lang.sh',
  'lang-test-infra.sh',
  'experimental-features.sh',
  'fetchMercurial.sh',
  'sandboxed-certificate-authorities.sh',
  'unsandboxed-certificate-authorities.sh',
  'gc-auto.sh',
  'user-envs.sh',
  'binary-cache.sh',
  'binary-cache-regression-fj647.sh',
  'multiple-outputs.sh',
  'nix-build.sh',
  'gc-concurrent.sh',
  'repair.sh',
  'repair-chroot.sh',
  'fixed.sh',
  'export-graph.sh',
  'fetchGitRefs.sh',
  'gc-runtime.sh',
  'tarball.sh',
  'fetchers.sh',
  'fetchGit.sh',
  'fetchGitLocked.sh',
  'fetchurl.sh',
  'fetchPath.sh',
  'fetchTree-file.sh',
  'simple.sh',
  'referrers.sh',
  'optimise-store.sh',
  'substitute-with-invalid-ca.sh',
  'signing.sh',
  'gc-non-blocking.sh',
  'check.sh',
  'nix-shell/basic.sh',
  'nix-shell/structured-attrs.sh',
  'nix-shell/regression-533.sh',
  'check-refs.sh',
  'build-remote-input-addressed.sh',
  'secure-drv-outputs.sh',
  'restricted.sh',
  'fetchGitSubmodules.sh',
  'flakes/search-root.sh',
  'readfile-context.sh',
  'nix-channel.sh',
  'dependencies.sh',
  'check-reqs.sh',
  'build-remote-content-addressed-fixed.sh',
  'build-remote-trustless-should-pass-0.sh',
  'build-remote-trustless-should-pass-1.sh',
  'build-remote-trustless-should-pass-2.sh',
  'build-remote-trustless-should-pass-3.sh',
  'build-remote-trustless-should-fail-0.sh',
  'build-jobless.sh',
  'nar-access.sh',
  'impure-eval.sh',
  'eval.sh',
  'repl.sh',
  'binary-cache-build-remote.sh',
  'logging.sh',
  'export.sh',
  'config.sh',
  'add.sh',
  'local-store.sh',
  'filter-source.sh',
  'misc.sh',
  'dump-db.sh',
  'linux-sandbox.sh',
  'supplementary-groups.sh',
  'build-dry.sh',
  'structured-attrs.sh',
  'shell.sh',
  'brotli.sh',
  'zstd.sh',
  'compression-levels.sh',
  'nix-copy-ssh.sh',
  'nix-copy-ssh-ng.sh',
  'pre-hook.sh',
  'post-hook.sh',
  'function-trace.sh',
  'flakes/config.sh',
  'fmt.sh',
  'eval-store.sh',
  'derivation-json.sh',
  'import-derivation.sh',
  'case-hack.sh',
  'placeholders.sh',
  'ssh-relay.sh',
  'build.sh',
  'build-delete.sh',
  'output-normalization.sh',
  'selfref-gc.sh',
  'db-migration.sh',
  'bash-profile.sh',
  'pass-as-file.sh',
  'nix-profile.sh',
  'suggestions.sh',
  'store-ping.sh',
  'fetchClosure.sh',
  'completions.sh',
  'flakes/show.sh',
  'path-from-hash-part.sh',
  'toString-path.sh',
  'read-only-store.sh',
  'nested-sandboxing.sh',
  'debugger.sh',
  'test-libstoreconsumer.sh',
  'extra-sandbox-profile.sh',
  'substitute-truncated-nar.sh',
  'regression-484.sh',
  'regression-reference-checks.sh',
  'redirected-filter-source.sh',
  'daemon-trust.sh',
  'output-cycles.sh',
]

# Plugin tests require shared libraries support.
if get_option('default_library') != 'static'
  functional_tests_scripts += ['plugins.sh']
endif

# TODO(Qyriad): this will hopefully be able to be removed when we remove the autoconf+Make
# buildsystem. See the comments at the top of setup-functional-tests.py for why this is here.
meson.add_install_script(
  python,
  meson.project_source_root() / 'meson/setup-functional-tests.py',
)

foreach script : functional_tests_scripts
  # Turns, e.g., `tests/functional/flakes/show.sh` into a Meson test target called
  # `functional-flakes-show`.
  name = 'functional-@0@'.format(fs.replace_suffix(script, '')).replace('/', '-')

  extra_deps = []
  if script == 'plugins.sh'
    extra_deps += [
      # Set in tests/functional/plugins/meson.build
      libplugintest,
      libplugintestfail,
    ]
  elif script == 'test-libstoreconsumer.sh'
    extra_deps += [
      # Set in tests/functional/test-libstoreconsumer/meson.build
      libstoreconsumer_tester,
    ]
  endif

  test(
    name,
    python,
    args: [
      meson.project_source_root() / 'meson/run-test.py',
      script,
    ],
    suite : 'installcheck',
    env : {
      'MESON_BUILD_ROOT': meson.project_build_root(),
    },
    # some tests take 15+ seconds even on an otherwise idle machine, on a loaded machine
    # this can easily drive them to failure. give them more time, 5min rather than 30sec
    timeout : 300,
    # Used for target dependency/ordering tracking, not adding compiler flags or anything.
    depends : extra_deps,
  )
endforeach

subdir('repl_characterization')
