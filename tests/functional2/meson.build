xdist_opts = [
  # auto number of workers, max 12 jobs
  '-n', 'auto', '--maxprocesses=12',
  # Distributes tests evenly at first and then, if workers run out of tests, they steal from a different worker
  # This is useful here, due to differing durations of tests, especially when tests check timeout functionality
  #
  # grouping by module/file is disadventageous here too, as all lang tests are executed from the same module
  # which would result in high run times for that single worker while everything else is bored
  '--dist=worksteal',
]

# surprisingly, this actually works even if PATH is set to something before
# meson gets hold of it. neat!
functional2_env = environment()
# set the bin dir for us to know where to find the lix binaries for our declarative path
# without leaking all of PATH or env
functional2_env.set('NIX_BIN_DIR', fs.parent(nix.full_path()))
if build_test_shell != ''
  functional2_env.set('BUILD_TEST_SHELL', build_test_shell)
endif
if build_test_env != ''
  functional2_env.set('BUILD_TEST_ENV', build_test_env)
endif
functional2_env.set('system', host_system)

test(
  'functional2',
  bash,
  args : [
    # FIXME: meson does not capture tap error reports; we duplicate them to stderr which is *does* capture,
    # --max-lines then lets us print most/all of them (cf https://github.com/mesonbuild/meson/issues/11185)
    files('meson-tap-wrap.sh'),
    python,
    '-m', 'pytest',
    '--tap',
    xdist_opts,
    meson.current_source_dir()
  ],
  env : functional2_env,
  depends : nix,
  protocol : 'tap',
  suite : 'installcheck',
  timeout : 300,
)
