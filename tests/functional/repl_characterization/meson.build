repl_characterization_tester_sources = files(
  'repl_characterization.cc',
  'test-session.cc',
)

repl_characterization_tester = executable(
  'test-repl-characterization',
  repl_characterization_tester_sources,
  dependencies : [
    liblixutil,
    liblixutil_test_support,
    sodium,
    editline,
    boost,
    lowdown,
    gtest,
  ],
  cpp_args : [
    f'-DNIX_BIN_DIR="@bindir@"',
  ],
  build_by_default : false,
)

# TODO(Qyriad): better understand the repl characterization tests' preconditions
# so we can run this with the gtest protocol, without run-test.py.
test(
  # test-repl-characterization.sh expects the tester executable to have this name,
  # so this name it shall have.
  'repl-characterization-tests',
  python,
  args : [
    meson.project_source_root() / 'meson/run-test.py',
    'test-repl-characterization.sh',
  ],
  depends : [repl_characterization_tester],
  env : {
    '_NIX_TEST_UNIT_DATA': meson.current_build_dir() / 'data',
    'MESON_BUILD_ROOT': meson.project_build_root(),
  },
  suite : 'installcheck',
)
