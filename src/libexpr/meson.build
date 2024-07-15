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

libexpr_sources = files(
  'attr-path.cc',
  'attr-set.cc',
  'eval-cache.cc',
  'eval-error.cc',
  'eval-settings.cc',
  'eval.cc',
  'function-trace.cc',
  'get-drvs.cc',
  'json-to-value.cc',
  'nixexpr.cc',
  'parser/parser.cc',
  'paths.cc',
  'primops.cc',
  'print-ambiguous.cc',
  'print.cc',
  'search-path.cc',
  'value-to-json.cc',
  'value-to-xml.cc',
  'flake/config.cc',
  'flake/flake.cc',
  'flake/flakeref.cc',
  'flake/lockfile.cc',
  'primops/context.cc',
  'primops/fetchClosure.cc',
  'primops/fetchMercurial.cc',
  'primops/fetchTree.cc',
  'primops/fromTOML.cc',
  'value/context.cc',
)

libexpr_headers = files(
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
  'gc-small-vector.hh',
  'get-drvs.hh',
  'gc-alloc.hh',
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
  'value/context.hh',
  'value-to-json.hh',
  'value-to-xml.hh',
  'value.hh',
)

libexpr = library(
  'lixexpr',
  libexpr_sources,
  libexpr_generated_headers,
  dependencies : [
    liblixutil,
    liblixstore,
    liblixfetchers,
    boehm,
    boost,
    toml11,
    nlohmann_json,
  ],
  # for shared.hh
  include_directories : [
    '../libmain',
  ],
  cpp_pch : cpp_pch,
  install : true,
  # FIXME(Qyriad): is this right?
  install_rpath : libdir,
)

install_headers(
  libexpr_headers,
  subdir : 'lix/libexpr',
  preserve_path : true,
)

liblixexpr = declare_dependency(
  include_directories : include_directories('.'),
  link_with : libexpr,
)

# FIXME: remove when https://git.lix.systems/lix-project/lix/issues/359 is fixed.
if is_static
  liblixexpr_mstatic = declare_dependency(
    include_directories : include_directories('.'),
    link_whole : libexpr,
  )
else
  liblixexpr_mstatic = liblixexpr
endif

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
  },
)
