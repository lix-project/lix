#!/usr/bin/env python3
"""
Runs run-clang-tidy. A bit meta. Maybe it will replace run-clang-tidy one day
because the run-clang-tidy UX is so questionable.
"""

# I hereby dedicate this script to fuck you meson.
# I cannot simply write my code to invoke a subprocess in a meson file because
# Meson corrupts backslashes in command line args to subprocesses.
# This is allegedly for "Windows support", but last time I checked Windows
# neither needs nor wants you to corrupt its command lines.
# https://github.com/mesonbuild/meson/issues/1564

import multiprocessing
import subprocess
import os
import sys
from pathlib import Path


def default_concurrency():
    return min(multiprocessing.cpu_count(),
               int(os.environ.get("NIX_BUILD_CORES", "16")))


def go(exe: str, plugin_path: Path, compile_commands_json_dir: Path, jobs: int,
       paths: list[Path], werror: bool, fix: bool):
    args = [
        # XXX: This explicitly invokes it with python because of a nixpkgs bug
        # where clang-unwrapped does not patch interpreters in run-clang-tidy.
        # However, making clang-unwrapped depend on python is also silly, so idk.
        sys.executable,
        exe,
        '-quiet',
        '-load',
        plugin_path,
        '-p',
        compile_commands_json_dir,
        '-j',
        str(jobs),
        '-header-filter',
        r'lix/[^/]+/.*\.hh'
    ]
    if werror:
        args += ['-warnings-as-errors', '*']
    if fix:
        args += ['-fix']
    args += ['--']
    args += paths
    os.execvp(sys.executable, args)


def main():
    import argparse

    ap = argparse.ArgumentParser(description='Runs run-clang-tidy for you')
    ap.add_argument('--jobs',
                    '-j',
                    type=int,
                    default=default_concurrency(),
                    help='Parallel linting jobs to run')
    ap.add_argument('--plugin-path',
                    type=Path,
                    help='Path to the Lix clang-tidy plugin')
    # FIXME: maybe we should integrate this so it just fixes the compdb for you and throws it in a tempdir?
    ap.add_argument(
        '--compdb-path',
        type=Path,
        help=
        'Path to the directory containing the fixed-up compilation database from clean_compdb'
    )
    ap.add_argument('--werror',
                    action='store_true',
                    help='Warnings get turned into errors')
    ap.add_argument('--fix',
                    action='store_true',
                    help='Apply fixes for warnings')
    ap.add_argument('--run-clang-tidy-path',
                    default='run-clang-tidy',
                    help='Path to run-clang-tidy')
    ap.add_argument('paths', nargs='*', help='Source paths to check')
    args = ap.parse_args()

    go(args.run_clang_tidy_path, args.plugin_path, args.compdb_path, args.jobs,
       args.paths, args.werror, args.fix)


if __name__ == '__main__':
    main()
