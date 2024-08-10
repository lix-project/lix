subdir('bash')
subdir('fish')
subdir('zsh')

subdir('systemd')
subdir('flake-registry')

runinpty = configure_file(
  copy : true,
  input : meson.current_source_dir() / 'runinpty.py',
  output : 'runinpty.py',
)
