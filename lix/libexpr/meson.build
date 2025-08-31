libexpr_generated_headers = [
  gen_header.process('primops/derivation.nix', preserve_path_from : meson.current_source_dir()),
]
foreach header : [ 'imported-drv-to-derivation.nix', 'fetchurl.nix' ]
  libexpr_generated_headers += custom_target(
    command : [ 'bash', '-c', 'echo \'R"__NIX_STR(\' | cat - @INPUT@ && echo \')__NIX_STR"\'' ],
    input : header,
    output : '@PLAINNAME@.gen.hh',
    capture : true,
    install : true,
    install_dir : includedir / 'lix/libexpr',
  )
endforeach
subdir('flake')

libexpr_setting_definitions = files(
  # keep-sorted start
  'settings/allow-import-from-derivation.md',
  'settings/allow-unsafe-native-code-during-evaluation.md',
  'settings/allowed-uris.md',
  'settings/debugger-on-trace.md',
  'settings/eval-cache.md',
  'settings/eval-system.md',
  'settings/ignore-try.md',
  'settings/max-call-depth.md',
  'settings/nix-path.md',
  'settings/pure-eval.md',
  'settings/repl-overlays.md',
  'settings/restrict-eval.md',
  'settings/trace-function-calls.md',
  'settings/trace-verbose.md',
  # keep-sorted end
)
libexpr_settings_header = custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_settings.py',
    '--kernel', host_machine.system(),
    '--header', '@OUTPUT@',
    '--experimental-features', '@SOURCE_ROOT@/lix/libutil/experimental-features',
    '@INPUT@',
  ],
  input : libexpr_setting_definitions,
  output : 'libexpr-settings.gen.inc',
  install : true,
  install_dir : includedir / 'lix/libexpr',
)

builtin_definitions = files(
  # keep-sorted start
  'builtins/abort.md',
  'builtins/add.md',
  'builtins/addDrvOutputDependencies.md',
  'builtins/all.md',
  'builtins/any.md',
  'builtins/attrNames.md',
  'builtins/attrValues.md',
  'builtins/baseNameOf.md',
  'builtins/bitAnd.md',
  'builtins/bitOr.md',
  'builtins/bitXor.md',
  'builtins/break.md',
  'builtins/catAttrs.md',
  'builtins/ceil.md',
  'builtins/compareVersions.md',
  'builtins/concatLists.md',
  'builtins/concatMap.md',
  'builtins/concatStringsSep.md',
  'builtins/deepSeq.md',
  'builtins/dirOf.md',
  'builtins/div.md',
  'builtins/elem.md',
  'builtins/elemAt.md',
  'builtins/fetchClosure.md',
  'builtins/fetchGit.md',
  'builtins/fetchTarball.md',
  'builtins/fetchTree.md',
  'builtins/fetchurl.md',
  'builtins/filter.md',
  'builtins/filterSource.md',
  'builtins/findFile.md',
  'builtins/flakeRefToString.md',
  'builtins/floor.md',
  'builtins/foldlStrict.md',
  'builtins/fromJSON.md',
  'builtins/fromTOML.md',
  'builtins/functionArgs.md',
  'builtins/genList.md',
  'builtins/genericClosure.md',
  'builtins/getAttr.md',
  'builtins/getContext.md',
  'builtins/getEnv.md',
  'builtins/getFlake.md',
  'builtins/groupBy.md',
  'builtins/hasAttr.md',
  'builtins/hasContext.md',
  'builtins/hashFile.md',
  'builtins/hashString.md',
  'builtins/head.md',
  'builtins/import.md',
  'builtins/intersectAttrs.md',
  'builtins/isAttrs.md',
  'builtins/isBool.md',
  'builtins/isFloat.md',
  'builtins/isFunction.md',
  'builtins/isInt.md',
  'builtins/isList.md',
  'builtins/isNull.md',
  'builtins/isPath.md',
  'builtins/isString.md',
  'builtins/length.md',
  'builtins/lessThan.md',
  'builtins/listToAttrs.md',
  'builtins/map.md',
  'builtins/mapAttrs.md',
  'builtins/match.md',
  'builtins/mul.md',
  'builtins/parseDrvName.md',
  'builtins/parseFlakeRef.md',
  'builtins/partition.md',
  'builtins/path.md',
  'builtins/pathExists.md',
  'builtins/placeholder.md',
  'builtins/readDir.md',
  'builtins/readFile.md',
  'builtins/readFileType.md',
  'builtins/removeAttrs.md',
  'builtins/replaceStrings.md',
  'builtins/seq.md',
  'builtins/sort.md',
  'builtins/split.md',
  'builtins/splitVersion.md',
  'builtins/storePath.md',
  'builtins/stringLength.md',
  'builtins/sub.md',
  'builtins/substring.md',
  'builtins/tail.md',
  'builtins/throw.md',
  'builtins/toFile.md',
  'builtins/toJSON.md',
  'builtins/toPath.md',
  'builtins/toString.md',
  'builtins/toXML.md',
  'builtins/trace.md',
  'builtins/traceVerbose.md',
  'builtins/tryEval.md',
  'builtins/typeOf.md',
  'builtins/unsafeDiscardOutputDependency.md',
  'builtins/unsafeGetAttrPos.md',
  'builtins/zipAttrsWith.md',
  # keep-sorted end
)
builtins_gen = custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_builtins.py',
    '--header', '@OUTPUT0@',
    '--docs', '@OUTPUT1@',
    '--experimental-features', '@SOURCE_ROOT@/lix/libutil/experimental-features',
    '@INPUT@'
  ],
  input : builtin_definitions,
  output : [
    'register-builtins.gen.inc',
    'builtins.md',
  ],
)
register_builtins_header = builtins_gen[0]
builtins_md = builtins_gen[1]

builtin_constant_definitions = files(
  # keep-sorted start
  'builtin-constants/builtins.md',
  'builtin-constants/currentSystem.md',
  'builtin-constants/currentTime.md',
  'builtin-constants/false.md',
  'builtin-constants/langVersion.md',
  'builtin-constants/nixPath.md',
  'builtin-constants/nixVersion.md',
  'builtin-constants/null.md',
  'builtin-constants/storeDir.md',
  'builtin-constants/true.md',
  # keep-sorted end
)
builtin_constants_gen = custom_target(
  command : [
    python.full_path(),
    '@SOURCE_ROOT@/lix/code-generation/build_builtin_constants.py',
    '--header', '@OUTPUT0@',
    '--docs', '@OUTPUT1@',
    '@INPUT@',
  ],
  input : builtin_constant_definitions,
  output : [
    'register-builtin-constants.gen.inc',
    'builtin-constants.md',
  ],
)
register_builtin_constants_header = builtin_constants_gen[0]
builtin_constants_md = builtin_constants_gen[1]

libexpr_sources = files(
  # keep-sorted start
  'attr-path.cc',
  'attr-set.cc',
  'eval-cache.cc',
  'eval-error.cc',
  'eval-settings.cc',
  'eval.cc',
  'flake/config.cc',
  'flake/flake.cc',
  'flake/flakeref.cc',
  'flake/lockfile.cc',
  'function-trace.cc',
  'gc-alloc.cc',
  'get-drvs.cc',
  'json-to-value.cc',
  'nixexpr.cc',
  'parser/parser.cc',
  'primops.cc',
  'primops/context.cc',
  'primops/fetchClosure.cc',
  'primops/fetchMercurial.cc',
  'primops/fetchTree.cc',
  'primops/fromTOML.cc',
  'print-ambiguous.cc',
  'print.cc',
  'search-path.cc',
  'value-to-json.cc',
  'value-to-xml.cc',
  'value.cc',
  'value/context.cc',
  # keep-sorted end
)

libexpr_headers = files(
  # keep-sorted start
  'attr-path.hh',
  'attr-set.hh',
  'eval-cache.hh',
  'eval-error.hh',
  'eval-inline.hh',
  'eval-settings.hh',
  'eval.hh',
  'flake/flake.hh',
  'flake/flakeref.hh',
  'flake/lockfile.hh',
  'function-trace.hh',
  'gc-alloc.hh',
  'gc-small-vector.hh',
  'get-drvs.hh',
  'json-to-value.hh',
  'nixexpr.hh',
  'parser/change_head.hh',
  'parser/grammar.hh',
  'parser/state.hh',
  'pos-idx.hh',
  'pos-table.hh',
  'primops.hh',
  'print-ambiguous.hh',
  'print-options.hh',
  'print.hh',
  'repl-exit-status.hh',
  'search-path.hh',
  'symbol-table.hh',
  'value-to-json.hh',
  'value-to-xml.hh',
  'value.hh',
  'value/context.hh',
  # keep-sorted end
)

dependencies = [
  liblixutil,
  liblixstore,
  liblixfetchers,
  boehm,
  boost,
  kj,
  nlohmann_json,
  toml11,
]

libexpr_temp = library(
  is_static ? 'lixexpr_temp' : 'lixexpr',
  libexpr_sources,
  libexpr_settings_header,
  libexpr_generated_headers,
  register_builtins_header,
  register_builtin_constants_header,
  dependencies : dependencies,
  # for shared.hh
  include_directories : [
    '../libmain',
  ],
  cpp_pch : cpp_pch,
  install : not is_static,
  # FIXME(Qyriad): is this right?
  install_rpath : libdir,
)
# FIXME: remove when https://git.lix.systems/lix-project/lix/issues/359 is fixed.
# FIXME: replace by prelink when https://github.com/mesonbuild/meson/pull/14846 is widely available.
if is_static
  libexpr_prelink = custom_target(
    'lixexpr-prelink',
    output : 'lixexpr-prelink.o',
    input : libexpr_temp,
    command : [
      cxx.cmd_array(),
      '-r',
      '-o',
      '@OUTPUT@',
      is_darwin ? '-Wl,-force_load' : '-Wl,--whole-archive',
      '@INPUT@',
    ],
  )
  libexpr = library(
    'lixexpr',
    [libexpr_prelink],
    dependencies : dependencies,
    install : true,
  )
else
  libexpr = libexpr_temp
endif

install_headers(
  libexpr_headers,
  subdir : 'lix/libexpr',
  preserve_path : true,
)

liblixexpr = declare_dependency(
  include_directories : include_directories('../..'),
  sources : libexpr_settings_header,
  dependencies : [
    liblixutil,
    liblixfetchers,
    boehm,
    boost,
  ],
  link_with : libexpr,
)

meson.override_dependency('lix-expr', liblixexpr)

# FIXME: not using the pkg-config module because it creates way too many deps
# while meson migration is in progress, and we want to not include boost here
configure_file(
  input : 'lix-expr.pc.in',
  output : 'lix-expr.pc',
  install_dir : libdir / 'pkgconfig',
  configuration : {
    'prefix' : prefix,
    'libdir' : libdir,
    'includedir' : includedir,
    'PACKAGE_VERSION' : meson.project_version(),
    'BOEHM_IF_FOUND' : boehm.found() ? 'bdw-gc' : '',
  },
)
