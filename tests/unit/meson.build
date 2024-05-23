# NOTE(Qyriad): This file is one big slab of boilerplate.
# Lix's current unit test organization is scattered and entagled.
# Each of the test-support libraries could theoretically be a somewhat self-contained
# subdir(), but literally nothing else can. Each of the tests have dependencies on other
# test support libraries, and so do their support libraries.
# Each of the tests have dependencies on their own andother test support libraries,
# and so do their support libraries, and they have entangled dependencies on Lix's mainline
# lib* targets as well.
# The only boilerplate reduction I really could do here is throw everything in big nested dictionaries
# and dynamically generate and refer to targets based on abstracted specs, but without user-defined
# functions, the result would be way less readable than just a bit of copypasta.
# It's only ~200 lines; better to just refactor the tests themselves which we'll want to do anyway.

libutil_test_support_sources = files(
  'libutil-support/tests/cli-literate-parser.cc',
  'libutil-support/tests/hash.cc',
  'libutil-support/tests/terminal-code-eater.cc',
)
libutil_test_support = library(
  'lixutil-test-support',
  libutil_test_support_sources,
  dependencies : [
    liblixutil,
    # TODO(Qyriad): libutil tests really should not depend on libexpr...
    liblixexpr,
    rapidcheck,
    boehm,
  ],
  include_directories : include_directories('libutil-support', '../../src'),
)
liblixutil_test_support = declare_dependency(
  include_directories : include_directories('libutil-support'),
  link_with : libutil_test_support,
)

libutil_tests_sources = files(
  'libutil/canon-path.cc',
  'libutil/chunked-vector.cc',
  'libutil/closure.cc',
  'libutil/compression.cc',
  'libutil/config.cc',
  'libutil/escape-string.cc',
  'libutil/git.cc',
  'libutil/hash.cc',
  'libutil/hilite.cc',
  'libutil/json-utils.cc',
  'libutil/logging.cc',
  'libutil/lru-cache.cc',
  'libutil/paths-setting.cc',
  'libutil/pool.cc',
  'libutil/references.cc',
  'libutil/suggestions.cc',
  'libutil/tests.cc',
  'libutil/url.cc',
  'libutil/url-name.cc',
  'libutil/xml-writer.cc',
)

libutil_tester = executable(
  'liblixutil-tests',
  libutil_tests_sources,
  dependencies : [
    rapidcheck,
    gtest,
    boehm,
    liblixutil,
    liblixexpr,
    liblixutil_test_support,
    nlohmann_json,
  ],
)

test(
  'libutil-unit-tests',
  libutil_tester,
  args : tests_args,
  env : {
    '_NIX_TEST_UNIT_DATA': meson.project_source_root() / 'tests/unit/libutil/data',
  },
  suite : 'check',
  protocol : 'gtest',
  verbose : true,
)

libstore_test_support_sources = files(
  'libstore-support/tests/derived-path.cc',
  'libstore-support/tests/outputs-spec.cc',
  'libstore-support/tests/path.cc',
  'libstore-support/tests/test-data.hh',
)

libstore_test_support = library(
  'lixstore-test-support',
  libstore_test_support_sources,
  dependencies : [
    liblixutil_test_support,
    liblixutil,
    liblixstore,
    rapidcheck,
    boehm,
  ],
  include_directories : include_directories(
    'libstore-support',
  ),
)
liblixstore_test_support = declare_dependency(
  include_directories : include_directories('libstore-support'),
  link_with : libstore_test_support,
)

libstore_tests_sources = files(
  'libstore/common-protocol.cc',
  'libstore/derivation.cc',
  'libstore/derived-path.cc',
  'libstore/downstream-placeholder.cc',
  'libstore/filetransfer.cc',
  'libstore/machines.cc',
  'libstore/nar-info-disk-cache.cc',
  'libstore/outputs-spec.cc',
  'libstore/path.cc',
  'libstore/references.cc',
  'libstore/serve-protocol.cc',
  'libstore/worker-protocol.cc',
)

libstore_tester = executable(
  'liblixstore-tests',
  libstore_tests_sources,
  dependencies : [
    liblixstore_test_support,
    liblixutil_test_support,
    liblixstore,
    liblixutil,
    rapidcheck,
    gtest,
    nlohmann_json,
  ],
)

test(
  'libstore-unit-tests',
  libstore_tester,
  args : tests_args,
  env : {
    '_NIX_TEST_UNIT_DATA': meson.project_source_root() / 'tests/unit/libstore/data',
  },
  suite : 'check',
  protocol : 'gtest',
  verbose : true,
)

libexpr_test_support_sources = files(
  'libexpr-support/tests/value/context.cc',
)

libexpr_test_support = library(
  'lixexpr-test-support',
  libexpr_test_support_sources,
  dependencies : [
    liblixstore_test_support,
    liblixstore,
    liblixutil,
    liblixexpr,
    rapidcheck,
  ],
  include_directories : include_directories(
    'libexpr-support',
  ),
)
liblixexpr_test_support = declare_dependency(
  include_directories : include_directories('libexpr-support'),
  link_with : libexpr_test_support,
)

libexpr_tests_sources = files(
  'libexpr/derived-path.cc',
  'libexpr/error_traces.cc',
  'libexpr/flakeref.cc',
  'libexpr/json.cc',
  'libexpr/primops.cc',
  'libexpr/search-path.cc',
  'libexpr/trivial.cc',
  'libexpr/value/context.cc',
  'libexpr/value/print.cc',
)

libexpr_tester = executable(
  'liblixexpr-tests',
  libexpr_tests_sources,
  dependencies : [
    liblixexpr_test_support,
    liblixstore_test_support,
    liblixstore,
    liblixutil,
    liblixexpr,
    liblixfetchers,
    rapidcheck,
    boehm,
    gtest,
    nlohmann_json,
  ],
)

test(
  'libexpr-unit-tests',
  libexpr_tester,
  args : tests_args,
  env : {
    '_NIX_TEST_UNIT_DATA': meson.project_source_root() / 'tests/unit/libexpr/data',
  },
  suite : 'check',
  protocol : 'gtest',
  verbose : true,
)

libcmd_tester = executable(
  'liblixcmd-tests',
  files('libcmd/args.cc'),
  dependencies : [
    liblixcmd,
    liblixutil,
    liblixmain,
    liblixexpr,
    liblixstore,
    gtest,
    boost,
  ],
)

test(
  'libcmd-unit-tests',
  libcmd_tester,
  args : tests_args,
  env : {
    # No special meaning here, it's just a file laying around that is unlikely to go anywhere
    # any time soon.
    '_NIX_TEST_UNIT_DATA': meson.project_source_root() / 'src/nix-env/buildenv.nix',
  },
  suite : 'check',
  protocol : 'gtest',
)
