project('nix-eval-jobs', 'cpp',
  version : '2.93.0-dev',
  license : 'GPL-3.0',
  default_options : [
    'debug=true',
    'optimization=2',
    'cpp_std=c++23',
  ],
)

lix_dep = dependency('lix', required: true)
threads_dep = dependency('threads', required: true)
nlohmann_json_dep = dependency('nlohmann_json', required: true)
boost_dep = dependency('boost', required: true)

subdir('src')
