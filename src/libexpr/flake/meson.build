libexpr_generated_headers += custom_target(
  command : [ 'bash', '-c', 'echo \'R"__NIX_STR(\' | cat - @INPUT@ && echo \')__NIX_STR"\'' ],
  input : 'call-flake.nix',
  output : '@PLAINNAME@.gen.hh',
  capture : true,
  install : true,
  install_dir : includedir / 'lix/libexpr/flake',
)
