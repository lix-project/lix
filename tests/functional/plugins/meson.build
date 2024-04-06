libplugintest = shared_module(
  'plugintest',
  'plugintest.cc',
  dependencies : [
    liblixutil,
    liblixstore,
    liblixexpr,
    liblixfetchers,
  ],
  build_by_default : false,
  link_args : shared_module_link_args,
)

libplugintestfail_link_args = []
if is_linux
  libplugintestfail_link_args = ['-Wl,-z,now']
endif

libplugintestfail = shared_module(
  'plugintestfail',
  'plugintestfail.cc',
  dependencies : [
    liblixutil,
    liblixstore,
    liblixexpr,
    liblixfetchers,
  ],
  cpp_args : ['-DMISSING_REFERENCE'],
  link_args : shared_module_link_args + libplugintestfail_link_args,
  build_by_default : false,
)
