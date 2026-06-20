plugin_mtls_store = shared_module(
  'plugin_mtls_store',
  'plugin_mtls_store.cc',
  # don't link liblix* into plugins (host process provides them at runtime).
  # Explicitly link curl so it binds to Nix-store libcurl, not /usr/lib/libcurl.
  dependencies : [
    liblix.partial_dependency(includes : true, compile_args : true),
    curl,
  ],
  install : false,
  build_by_default : true,
  link_args : plugin_link_args,
)
