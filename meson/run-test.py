#!/usr/bin/env python3

"""
This script is a helper for this project's Meson buildsystem to run Lix's
functional tests. It is an analogue to mk/run-test.sh in the autoconf+Make
buildsystem.

These tests are run in the installCheckPhase in Lix's derivation, and as such
expect to be run after the project has already been "installed" to some extent.
Look at meson/setup-functional-tests.py for more details.
"""

import argparse
from pathlib import Path
import os
import shutil
import subprocess
import sys

name = 'run-test.py'

if 'MESON_BUILD_ROOT' not in os.environ:
    raise ValueError(f'{name}: this script must be run from the Meson build system')

def main():

    tests_dir = Path(os.path.join(os.environ['MESON_BUILD_ROOT'], 'tests/functional'))

    parser = argparse.ArgumentParser(name)
    parser.add_argument('target', help='the script path relative to tests/functional to run')
    args = parser.parse_args()

    target = Path(args.target)
    # The test suite considers the test's name to be the path to the test relative to
    # `tests/functional`, but without the file extension.
    # e.g. for `tests/functional/flakes/develop.sh`, the test name is `flakes/develop`
    test_name = target.with_suffix('').as_posix()
    if not target.is_absolute():
        target = tests_dir.joinpath(target).resolve()

    assert target.exists(), f'{name}: test {target} does not exist; did you run `meson install`?'

    bash = os.environ.get('BASH', shutil.which('bash'))
    if bash is None:
        raise ValueError(f'{name}: bash executable not found and BASH environment variable not set')

    test_environment = os.environ | {
        'TEST_NAME': test_name,
        # mk/run-test.sh did this, but I don't know if it has any effect since it seems
        # like the tests that interact with remote stores set it themselves?
        'NIX_REMOTE': '',
    }

    # Initialize testing.
    init_result = subprocess.run([bash, '-e', 'init.sh'], cwd=tests_dir, env=test_environment)
    if init_result.returncode != 0:
        print(f'{name}: internal error initializing {args.target}', file=sys.stderr)
        print('[ERROR]')
        # Meson interprets exit code 99 as indicating an *error* in the testing process.
        return 99

    # Run the test itself.
    test_result = subprocess.run([bash, '-e', target.name], cwd=target.parent, env=test_environment)

    if test_result.returncode == 0:
        print('[PASS]')
    elif test_result.returncode == 99:
        print('[SKIP]')
        # Meson interprets exit code 77 as indicating a skipped test.
        return 77
    else:
        print('[FAIL]')

    return test_result.returncode

try:
    sys.exit(main())
except AssertionError as e:
    # This should mean that this test was run not-from-Meson, probably without
    # having run `meson install` first, which is not an bug in this script.
    print(e, file=sys.stderr)
    sys.exit(99)
except Exception as e:
    print(f'{name}: INTERNAL ERROR running test ({sys.argv}): {e}', file=sys.stderr)
    print(f'this is a bug in {name}')
    sys.exit(99)
