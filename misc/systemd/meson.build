# legacy sockets are handled specially for graceful fallback if nixos module
# updates are not applied in lockstep with the socket split updates for lix.
#
# specs are (protocol, socket path, unit name, unit description, slice name)
lix_daemon_sockets = [
  ['lix-xp-1',        'lix-xp-1/socket', 'lix-daemon-xp1', 'experimental RPC', 'xp1'],
  ['legacy-combined', 'socket',          'nix-daemon',     'legacy',           'legacy'],
]

lix_daemon_socket_units = []

foreach spec : lix_daemon_sockets
  lix_daemon_socket_units += [ '@0@.socket'.format(spec[2]) ]
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
      'protocol' : spec[0],
      'desc' : spec[3],
      'path' : spec[1],
      'dirmode' : '0755',
    },
  )

  configure_file(
    input : 'daemon@.service.in',
    output : '@0@@.service'.format(spec[2]),
    install : true,
    install_dir : prefix / 'lib/systemd/system',
    install_mode : 'rw-r--r--',
    configuration : {
      'storedir' : store_dir,
      'localstatedir' : state_dir,
      'bindir' : bindir,
      'protocol' : spec[0],
      'desc' : spec[3],
      'slice' : spec[4],
    },
  )
endforeach

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
