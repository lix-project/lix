# 2024-03-24: jade benchmarked the default sanitize reporting in clang and got
# a regression of about 10% on hackage-packages.nix with clang. So we are trapping instead.
#
# This has an overhead of 0-4% on gcc and unmeasurably little on clang, in
# Nix evaluation benchmarks.
DEFAULT_SANITIZE_FLAGS = -fsanitize=signed-integer-overflow -fsanitize-undefined-trap-on-error
GLOBAL_CXXFLAGS += -Wno-deprecated-declarations -Werror=switch $(DEFAULT_SANITIZE_FLAGS)
GLOBAL_LDFLAGS += $(DEFAULT_SANITIZE_FLAGS)
# Allow switch-enum to be overridden for files that do not support it, usually because of dependency headers.
ERROR_SWITCH_ENUM = -Werror=switch-enum

$(foreach i, config.h $(wildcard src/lib*/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))

$(GCH): src/libutil/util.hh config.h

GCH_CXXFLAGS = -I src/libutil
