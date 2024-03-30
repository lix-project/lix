project('lix-clang-tidy', ['cpp', 'c'],
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++20'])

llvm = dependency('Clang', version: '>= 14', modules: ['libclang'])
sources = files(
  'HasPrefixSuffix.cc',
  'LixClangTidyChecks.cc',
  'FixIncludes.cc',
)

shared_module('lix-clang-tidy', sources,
  dependencies: llvm)
