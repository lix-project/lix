configure_file(
  input : 'nix-profile.sh.in',
  output : 'nix-profile.sh',
  configuration : {
    'localstatedir': state_dir,
  }
)

# https://github.com/mesonbuild/meson/issues/860
configure_file(
  input : 'nix-profile.sh.in',
  output : 'nix-profile.sh.in',
  copy : true,
)
