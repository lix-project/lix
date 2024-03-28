#!/usr/bin/env python3

"""
So like. This script is cursed.
It's a helper for this project's Meson buildsystem for Lix's functional tests.
The functional tests are a bunch of bash scripts, that each expect to be run from the
directory from the directory that that script is in, and also expect modifications to have
happened to the source tree, and even splork files around. The last is against the spirit
of Meson (and personally annoying), to have build processes that aren't self-contained in the
out-of-source build directory, but more problematically we need configured files in the test
tree.
So. We copy the tests tree into the build directory.
Meson doesn't have a good way of doing this natively -- the best you could do is subdir()
into every directory in the tests tree and configure_file(copy : true) on every file,
but this won't copy symlinks as symlinks, which we need since the test suite has, well,
tests on symlinks.
However, the functional tests are normally run during Lix's derivation's installCheckPhase,
after Lix has already been "installed" somewhere. So in Meson we setup add this file as an
install script and copy everything in tests/functional to the build directory, preserving
things like symlinks, even broken ones (which are intentional).

TODO(Qyriad): when we remove the old build system entirely, we can instead fix the tests.
"""

from pathlib import Path
import os, os.path
import shutil
import sys
import traceback

name = 'setup-functional-tests.py'

if 'MESON_SOURCE_ROOT' not in os.environ or 'MESON_BUILD_ROOT' not in os.environ:
    raise ValueError(f'{name}: this script must be run from the Meson build system')

print(f'{name}: mirroring tests/functional to build directory')

tests_source = Path(os.environ['MESON_SOURCE_ROOT']) / 'tests/functional'
tests_build = Path(os.environ['MESON_BUILD_ROOT']) / 'tests/functional'

def main():

    os.chdir(tests_build)

    for src_dirpath, src_dirnames, src_filenames in os.walk(tests_source):
        src_dirpath = Path(src_dirpath)
        assert src_dirpath.is_absolute(), f'{src_dirpath=} is not absolute'

        # os.walk() gives us the absolute path to the directory we're currently in as src_dirpath.
        # We want to mirror from the perspective of `tests_source`.
        rel_subdir = src_dirpath.relative_to(tests_source)
        assert (not rel_subdir.is_absolute()), f'{rel_subdir=} is not relative'

        # And then join that relative path on `tests_build` to get the absolute
        # path in the build directory that corresponds to `src_dirpath`.
        build_dirpath = tests_build / rel_subdir
        assert build_dirpath.is_absolute(), f'{build_dirpath=} is not absolute'

        # More concretely, for the test file tests/functional/ca/build.sh:
        # - src_dirpath is `$MESON_SOURCE_ROOT/tests/functional/ca`
        # - rel_subidr is `ca`
        # - build_dirpath is `$MESON_BUILD_ROOT/tests/functional/ca`

        # `src_dirname` are directories underneath `src_dirpath`, and will be relative
        # to `src_dirpath`.
        for src_dirname in src_dirnames:
            # Take the name of the directory in the tests source and join it on `src_dirpath`
            # to get the full path to this specific directory in the tests source.
            src = src_dirpath / src_dirname
            # If there isn't *something* here, then our logic is wrong.
            # Path.exists(follow_symlinks=False) wasn't added until Python 3.12, so we use
            # os.path.lexists() here.
            assert os.path.lexists(src), f'{src=} does not exist'

            # Take the name of this directory and join it on `build_dirpath` to get the full
            # path to the directory in `build/tests/functional` that we need to create.
            dest = build_dirpath / src_dirname
            if src.is_symlink():
                src_target = src.readlink()
                dest.unlink(missing_ok=True)
                dest.symlink_to(src_target)
            else:
                dest.mkdir(parents=True, exist_ok=True)

        for src_filename in src_filenames:
            # os.walk() should be giving us relative filenames.
            # If it isn't, our path joins will be veeeery wrong.
            assert (not Path(src_filename).is_absolute()), f'{src_filename=} is not relative'

            src = src_dirpath / src_filename
            dst = build_dirpath / src_filename
            # Mildly misleading name -- unlink removes ordinary files as well as symlinks.
            dst.unlink(missing_ok=True)
            # shutil.copy2() best-effort preserves metadata.
            shutil.copy2(src, dst, follow_symlinks=False)


try:
    sys.exit(main())
except Exception as e:
    # Any error is likely a bug in this script.
    print(f'{name}: INTERNAL ERROR setting up functional tests: {e}', file=sys.stderr)
    print(traceback.format_exc())
    print(f'this is a bug in {name}')
    sys.exit(1)
