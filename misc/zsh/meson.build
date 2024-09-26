foreach script : [ [ 'completion.zsh', '_nix' ], [ 'run-help-nix' ] ]
  fs.copyfile(
    script[0],
    script.get(1, script[0]),
    install : true,
    install_dir : datadir / 'zsh/site-functions',
    install_mode : 'rw-r--r--',
  )
endforeach
