project('nix-eval-jobs', 'cpp',
  version : '2.93.0-dev',
  license : 'GPL-3.0',
  default_options : [
    'debug=true',
    'optimization=2',
    'cpp_std=c++20',
  ],
)

nix_main_dep = dependency('lix-main', required: true)
nix_store_dep = dependency('lix-store', required: true)
nix_expr_dep = dependency('lix-expr', required: true)
nix_cmd_dep = dependency('lix-cmd', required: true)
nix_util_dep = dependency('lix-util', required: true)
threads_dep = dependency('threads', required: true)
nlohmann_json_dep = dependency('nlohmann_json', required: true)
boost_dep = dependency('boost', required: true)

subdir('src')
