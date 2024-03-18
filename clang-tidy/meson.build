project('nix-clang-tidy', ['cpp', 'c'],
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++20'])

llvm = dependency('Clang', version: '>= 14', modules: ['libclang'])
sources = ['HasPrefixSuffix.cc', 'NixClangTidyChecks.cc']
shared_module('nix-clang-tidy', sources,
  dependencies: llvm)
