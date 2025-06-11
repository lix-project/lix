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
functional2_env.set('NIX_BIN_DIR', bindir)
functional2_env.set('BUILD_TEST_SHELL', build_test_shell)
functional2_env.set('system', host_system)

test(
  'functional2',
  python,
  args : [
    '-m', 'pytest',
    '-v',
    # Print out summaries of **f**ailed, **E**rrored and **s**kipped tests
    '-r', 'fEs',
    xdist_opts,
    meson.current_source_dir()
  ],
  env : functional2_env,
  # FIXME: Although we can trivially use TAP here with pytest-tap, due to a meson bug, it is unusable.
  # (failure output does not get displayed to the console. at all. someone should go fix it):
  # https://github.com/mesonbuild/meson/issues/11185
  # protocol : 'tap',
  suite : 'installcheck',
  timeout : 300,
  # Do not cut output of this runner on failure!
  # As TAP is currently not supported (see above), we do need all lines to actually know
  # why and how the failing tests actually failed
  verbose: true,
)
