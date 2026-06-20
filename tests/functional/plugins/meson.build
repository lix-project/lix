libplugintest = shared_module(
  'plugintest',
  'plugintest.cc',
  dependencies : [
    liblix.partial_dependency(includes : true, compile_args : true),
  ],
  build_by_default : false,
  link_args : plugin_link_args,
)

libplugintestfail_link_args = []
if is_linux
  libplugintestfail_link_args = ['-Wl,-z,now']
endif

libplugintestfail = shared_module(
  'plugintestfail',
  'plugintestfail.cc',
  dependencies : [
    liblix.partial_dependency(includes : true, compile_args : true),
  ],
  cpp_args : ['-DMISSING_REFERENCE'],
  link_args : plugin_link_args + libplugintestfail_link_args,
  build_by_default : false,
)
