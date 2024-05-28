src = [
  'nix-eval-jobs.cc',
  'eval-args.cc',
  'drv.cc',
  'buffered-io.cc',
  'worker.cc'
]

cc = meson.get_compiler('cpp')

autotool_config = configuration_data()
# nix defines this with autotools
if cc.has_member('struct dirent', 'd_type', prefix: '#include <dirent.h>')
  autotool_config.set('HAVE_STRUCT_DIRENT_D_TYPE', 1)
endif

configure_file(
  input: 'autotools-config.h.in',
  output: 'autotools-config.h',
  configuration: autotool_config
)

executable('nix-eval-jobs', src,
           dependencies : [
             nix_main_dep,
             nix_store_dep,
             nix_expr_dep,
             nix_cmd_dep,
             boost_dep,
             nlohmann_json_dep,
             threads_dep
           ],
           install: true,
           cpp_args: ['-std=c++2a', '--include', 'autotools-config.h'])
