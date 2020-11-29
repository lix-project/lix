src = [
  'hydra-eval-jobs.cc',
]

executable('hydra-eval-jobs', src,
           dependencies : [
             nix_main_dep,
             nix_store_dep,
             nix_expr_dep,
             boost_dep,
             nlohmann_json_dep,
             threads_dep
           ],
           install: true,
           cpp_args: ['-std=c++17', '-fvisibility=hidden'])
