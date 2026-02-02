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

if is_linux
  launch_builder_impl = 'linux'
elif is_darwin
  launch_builder_impl = 'darwin'
else
  launch_builder_impl = 'fallback'
endif

launch_builder = executable(
  'launch-builder',
  files(
    'launch-builder.cc',
    f'launch-builder-@launch_builder_impl@.cc',
  ),
  liblix_generated_headers,
  include_directories : [ '../..' ],
  dependencies : [
    capnp,
  ],
  install : true,
  install_dir : libexecdir / 'lix',
)

run_build_hook = executable(
  'run-build-hook',
  files('run-build-hook.cc'),
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
