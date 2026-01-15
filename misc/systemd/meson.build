lix_daemon_socket_units = []

# legacy sockets are handled specially for graceful fallback if nixos module
# updates are not applied in lockstep with the socket split updates for lix.
lix_daemon_socket_units += [ 'nix-daemon.socket' ]
configure_file(
  input : 'daemon.socket.in',
  output : lix_daemon_socket_units[-1],
  install : true,
  install_dir : prefix / 'lib/systemd/system',
  install_mode : 'rw-r--r--',
  configuration : {
    'storedir' : store_dir,
    'localstatedir' : state_dir,
    'bindir' : bindir,
    'protocol' : 'legacy',
    'kind' : 'combined',
    'path' : 'socket',
    'dirmode' : '0755',
  },
)
configure_file(
  input : 'daemon@.service.in',
  output : 'nix-daemon@.service',
  install : true,
  install_dir : prefix / 'lib/systemd/system',
  install_mode : 'rw-r--r--',
  configuration : {
    'storedir' : store_dir,
    'localstatedir' : state_dir,
    'bindir' : bindir,
    'conflicts' : ' '.join(lix_daemon_socket_units),
    'protocol' : 'legacy',
    'kind' : 'combined',
  },
)

foreach config : [ 'nix-daemon.service' ]
  configure_file(
    input : config + '.in',
    output : config,
    install : true,
    install_dir : prefix / 'lib/systemd/system',
    install_mode : 'rw-r--r--',
    configuration : {
      'storedir' : store_dir,
      'localstatedir' : state_dir,
      'bindir' : bindir,
      'conflicts' : ' '.join(lix_daemon_socket_units),
    },
  )
endforeach

configure_file(
  input : 'nix-daemon.conf.in',
  output : 'nix-daemon.conf',
  install : true,
  install_dir : prefix / 'lib/tmpfiles.d',
  install_mode : 'rw-r--r--',
  configuration : {
    'localstatedir' : state_dir,
  },
)
