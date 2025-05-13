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
functional2_env.prepend('PATH', bindir)

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
)
