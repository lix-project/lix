xdist_opts = [
  # auto number of workers, max 12 jobs
  '-n', 'auto', '--maxprocesses=12',
  # group tests by module or class; ensures that any setup work occurs as little as possible
  '--dist=loadscope',
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
