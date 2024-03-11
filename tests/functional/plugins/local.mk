libraries += libplugintest libplugintestfail

libplugintest_DIR := $(d)

libplugintest_SOURCES := $(d)/plugintest.cc

libplugintest_ALLOW_UNDEFINED := 1

libplugintest_EXCLUDE_FROM_LIBRARY_LIST := 1

libplugintest_CXXFLAGS := -I src/libutil -I src/libstore -I src/libexpr -I src/libfetchers

libplugintestfail_DIR := $(d)

libplugintestfail_SOURCES := $(d)/plugintestfail.cc

libplugintestfail_ALLOW_UNDEFINED := 1

libplugintestfail_EXCLUDE_FROM_LIBRARY_LIST := 1

libplugintestfail_CXXFLAGS := -I src/libutil -I src/libstore -I src/libexpr -I src/libfetchers -DMISSING_REFERENCE

# Make sure that the linker strictly evaluates all symbols on .so load on Linux
# so it will definitely fail to load as expected.
ifdef HOST_LINUX
  libplugintestfail_LDFLAGS += -z now
endif
