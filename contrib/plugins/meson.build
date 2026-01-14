# Darwin: don't link liblix* into plugins (host process provides them at runtime).
# Explicitly link curl so it binds to Nix-store libcurl, not /usr/lib/libcurl.
if is_darwin
  plugin_deps = [
    liblix.partial_dependency(includes : true, compile_args : true),
    curl,
  ]
else
  plugin_deps = [liblix, curl]
endif

plugin_mtls_store = shared_module(
  'plugin_mtls_store',
  'plugin_mtls_store.cc',
  dependencies : plugin_deps,
  install : false,
  build_by_default : true,
  link_args : is_darwin ? shared_module_link_args : strict_shared_module_link_args,
)
