test_confdata = {
  'bindir': bindir,
  'coreutils': fs.parent(coreutils.full_path()),
  'lsof': lsof.full_path(),
  'dot': dot.found() ? dot.full_path() : '',
  'bash': bash.full_path(),
  'sandbox_shell': busybox.found() ? busybox.full_path() : '',
  'PACKAGE_VERSION': meson.project_version(),
  'system': host_system,
}

# Just configures `common/vars-and-functions.sh.in`.
# Done as a subdir() so Meson places it under `common` in the build directory as well.
subdir('common')

config_nix_in = configure_file(
  input : 'config.nix.in',
  output : 'config.nix',
  configuration : test_confdata,
)

# Just configures `ca/config.nix.in`. Done as a subdir() for the same reason as above.
subdir('ca')
# Just configures `dyn-drv/config.nix.in`. Same as above.
subdir('dyn-drv')

subdir('plugins')
subdir('test-libstoreconsumer')

functional_tests_scripts = [
  'init.sh',
  'test-infra.sh',
  'ca/build.sh',
  'ca/build-cache.sh',
  'ca/concurrent-builds.sh',
  'ca/derivation-json.sh',
  'ca/duplicate-realisation-in-closure.sh',
  'ca/eval-store.sh',
  'ca/gc.sh',
  'ca/import-derivation.sh',
  'ca/new-build-cmd.sh',
  'ca/nix-copy.sh',
  'ca/nix-run.sh',
  'ca/nix-shell.sh',
  'ca/post-hook.sh',
  'ca/recursive.sh',
  'ca/repl.sh',
  'ca/selfref-gc.sh',
  'ca/signatures.sh',
  'ca/substitute.sh',
  'ca/why-depends.sh',
  'dyn-drv/text-hashed-output.sh',
  'dyn-drv/recursive-mod-json.sh',
  'dyn-drv/build-built-drv.sh',
  'dyn-drv/eval-outputOf.sh',
  'dyn-drv/dep-built-drv.sh',
  'dyn-drv/old-daemon-error-hack.sh',
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
  'flakes/flake-registry.sh',
  'flakes/flake-in-submodule.sh',
  'gc.sh',
  'nix-collect-garbage-d.sh',
  'remote-store.sh',
  'legacy-ssh-store.sh',
  'lang.sh',
  'lang-test-infra.sh',
  'experimental-features.sh',
  'fetchMercurial.sh',
  'gc-auto.sh',
  'user-envs.sh',
  'user-envs-migration.sh',
  'binary-cache.sh',
  'multiple-outputs.sh',
  'nix-build.sh',
  'gc-concurrent.sh',
  'repair.sh',
  'fixed.sh',
  'export-graph.sh',
  'timeout.sh',
  'fetchGitRefs.sh',
  'gc-runtime.sh',
  'tarball.sh',
  'fetchGit.sh',
  'fetchurl.sh',
  'fetchPath.sh',
  'fetchTree-file.sh',
  'simple.sh',
  'referrers.sh',
  'optimise-store.sh',
  'substitute-with-invalid-ca.sh',
  'signing.sh',
  'hash.sh',
  'gc-non-blocking.sh',
  'check.sh',
  'nix-shell.sh',
  'check-refs.sh',
  'build-remote-input-addressed.sh',
  'secure-drv-outputs.sh',
  'restricted.sh',
  'fetchGitSubmodules.sh',
  'flakes/search-root.sh',
  'readfile-context.sh',
  'nix-channel.sh',
  'recursive.sh',
  'dependencies.sh',
  'check-reqs.sh',
  'build-remote-content-addressed-fixed.sh',
  'build-remote-content-addressed-floating.sh',
  'build-remote-trustless-should-pass-0.sh',
  'build-remote-trustless-should-pass-1.sh',
  'build-remote-trustless-should-pass-2.sh',
  'build-remote-trustless-should-pass-3.sh',
  'build-remote-trustless-should-fail-0.sh',
  'nar-access.sh',
  'impure-eval.sh',
  'pure-eval.sh',
  'eval.sh',
  'repl.sh',
  'binary-cache-build-remote.sh',
  'search.sh',
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
  'post-hook.sh',
  'function-trace.sh',
  'flakes/config.sh',
  'fmt.sh',
  'eval-store.sh',
  'why-depends.sh',
  'derivation-json.sh',
  'import-derivation.sh',
  'nix_path.sh',
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
  'impure-derivations.sh',
  'path-from-hash-part.sh',
  'toString-path.sh',
  'read-only-store.sh',
  'nested-sandboxing.sh',
  'debugger.sh',
  'test-libstoreconsumer.sh',
  'extra-sandbox-profile.sh',
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
