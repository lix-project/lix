#!/usr/bin/env bash
# Meson will call this with an absolute path to Bash.
# The shebang is just for convenience.

# The parser and lexer tab are generated via custom Meson targets in src/libexpr/meson.build,
# but Meson doesn't support marking only part of a target for install. The generation creates
# both headers (parser-tab.hh, lexer-tab.hh) and source files (parser-tab.cc, lexer-tab.cc),
# and we definitely want the former installed, but not the latter. This script is added to
# Meson's install steps to correct this, as the logic for it is just complex enough to
# warrant separate and careful handling, because both Meson's configured include directory
# may or may not be an absolute path, and DESTDIR may or may not be set at all, but can't be
# manipulated in Meson logic.

set -euo pipefail

echo "cleanup-install: removing Meson-placed C++ sources from dest includedir"

if [[ "${1/--help/}" != "$1" ]]; then
	echo "cleanup-install: this script should only be called from the Meson build system"
	exit 1
fi

# Ensure the includedir was passed as the first argument
# (set -u will make this fail otherwise).
includedir="$1"
# And then ensure that first argument is a directory that exists.
if ! [[ -d "$1" ]]; then
	echo "cleanup-install: this script should only be called from the Meson build system"
	echo "argv[1] (${1@Q}) is not a directory"
	exit 2
fi

# If DESTDIR environment variable is set, prepend it to the include dir.
# Unfortunately, we cannot do this on the Meson side. We do have an environment variable
# `MESON_INSTALL_DESTDIR_PREFIX`, but that will not refer to the include directory if
# includedir has been set separately, which Lix's split-output derivation does.
# We also cannot simply do an inline bash conditional like "${DESTDIR:=}" or similar,
# because we need to specifically *join* DESTDIR and includedir with a slash, and *not*
# have a slash if DESTDIR isn't set at all, since $includedir could be a relative directory.
# Finally, DESTDIR is only available to us as an environment variable in these install scripts,
# not in Meson logic.
# Therefore, our best option is to have Meson pass this script the configured includedir,
# and perform this dance with it and $DESTDIR.
if [[ -n "${DESTDIR:-}" ]]; then
	includedir="$DESTDIR/$includedir"
fi

# Intentionally not using -f.
# If these files don't exist then our assumptions have been violated and we should fail.
rm -v "$includedir/lix/libexpr/parser-tab.cc" "$includedir/lix/libexpr/lexer-tab.cc"
