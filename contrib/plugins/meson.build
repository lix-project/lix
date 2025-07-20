plugin_mtls_store = shared_module(
  'plugin_mtls_store',
  'plugin_mtls_store.cc',
  dependencies : [
    liblixutil,
    liblixstore,
    liblixexpr,
    liblixfetchers,
    curl,
  ],
  install : false,
  build_by_default : true,
  link_args : strict_shared_module_link_args,
)
