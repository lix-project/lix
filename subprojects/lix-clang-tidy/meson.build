project('lix-clang-tidy', ['cpp', 'c'],
  meson_version : '>=1.7.0',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++20']
)

llvm = dependency('Clang', version: '>= 16', modules: ['libclang'])
sources = files(
  'CharPtrCast.cc',
  'DisallowedDecls.cc',
  'ForeignExceptions.cc',
  'HasPrefixSuffix.cc',
  'LixClangTidyChecks.cc',
  'NeverAsync.cc',
)

lix_clang_tidy = shared_module('lix-clang-tidy', sources,
  dependencies: llvm,
  # overrides build_by_default, see https://github.com/mesonbuild/meson/issues/13498
  install : get_option('build-by-default'),
  build_by_default : get_option('build-by-default')
)
