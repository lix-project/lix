#!/usr/bin/env python3

"""
This script is a helper for this project's Meson buildsystem, to generate
manpages as mdbook markdown for nix3 CLI commands. It is an analogue to an
inline sequence of bash commands in the autoconf+Make buildsystem, which works
around a limitation in `nix eval --write-to`, in that it refuses to write to any
directory that already exists.

Basically, this script is a glorified but hopefully-more-robust version of:
$ rm -rf $output
$ nix eval --write-to $output.tmp --expr 'import doc/manual/generate-manpage.nix true
    (builtins.readFile ./generate-manpage.nix)'
$ mv $output.tmp $output
"""

import argparse
import os.path
import shlex
import shutil
import subprocess
import sys
import tempfile

name = 'generate-manpage.py'

def log(*args, **kwargs):
    kwargs['file'] = sys.stderr
    return print(f'{name}:', *args, **kwargs)

def main():
    parser = argparse.ArgumentParser(name)
    parser.add_argument('--nix', required=True, help='Full path to the nix binary to use')
    parser.add_argument('-o', '--output', required=True, help='Output directory')
    parser.add_argument('--generator', required=True, help='Path to generate-manpage.nix')
    parser.add_argument('--cli-json', required=True, help='Path to the nix.json output from Nix')
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tempdir:

        temp_out = os.path.join(tempdir, 'new-cli')

        nix_args = [
            args.nix,
            '--experimental-features',
            'nix-command',
            'eval',
            '-I', 'nix/corepkgs=corepkgs',
            '--store', 'dummy://',
            '--impure',
            '--raw',
            '--write-to', temp_out,
            '--expr',
            f'import {args.generator} true (builtins.readFile {args.cli_json})',
        ]

        log('generating nix3 man pages with', shlex.join(nix_args))

        subprocess.check_call(nix_args)

        shutil.copytree(temp_out, args.output, dirs_exist_ok=True)

sys.exit(main())
