if is_linux
  check_namespace_support = executable(
    'check-namespace-support',
    files('check-namespace-support.cc'),
    install : true,
    install_dir : libexecdir / 'lix',
  )
endif

kill_user = executable(
  'kill-user',
  files('kill-user.cc'),
  install : true,
  install_dir : libexecdir / 'lix',
)

run_diff_hook = executable(
  'run-diff-hook',
  files('run-diff-hook.cc'),
  install : true,
  install_dir : libexecdir / 'lix',
)

run_pager = executable(
  'run-pager',
  files('run-pager.cc'),
  install : true,
  install_dir : libexecdir / 'lix',
)

unix_bind_connect = executable(
  'unix-bind-connect',
  files('unix-bind-connect.cc'),
  install : true,
  install_dir : libexecdir / 'lix',
)
