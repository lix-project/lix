#!/usr/bin/env python3
# Deletes the PCH arguments from a compilation database, to workaround nixpkgs
# stdenv having a cc-wrapper that is impossible to use for anything except cc
# itself, for example, clang-tidy.

import json
import shlex


def process_compdb(compdb: list[dict]) -> list[dict]:

    def munch_command(args: list[str]) -> list[str]:
        out = []
        eat_next = False
        for i, arg in enumerate(args):
            if arg in ['-fpch-preprocess', '-fpch-instantiate-templates']:
                # -fpch-preprocess as used with gcc, -fpch-instantiate-templates as used by clang
                continue
            elif arg == '-include-pch' or (arg == '-include' and args[i + 1] == 'precompiled-headers.hh'):
                # -include-pch some-pch (clang), or -include some-pch (gcc)
                eat_next = True
                continue
            if not eat_next:
                out.append(arg)
            eat_next = False
        return out

    def chomp(item: dict) -> dict:
        item = item.copy()
        item['command'] = shlex.join(munch_command(shlex.split(item['command'])))
        return item

    def cmdfilter(item: dict) -> bool:
        file = item['file']
        return (
            not file.endswith('precompiled-headers.hh')
            and not file.endswith('.rs')
        )

    return [chomp(x) for x in compdb if cmdfilter(x)]


def main():
    import argparse
    ap = argparse.ArgumentParser(
        description='Delete pch arguments from compilation database')
    ap.add_argument('input',
                    type=argparse.FileType('r'),
                    help='Input json file')
    ap.add_argument('output',
                    type=argparse.FileType('w'),
                    help='Output json file')
    args = ap.parse_args()

    input_json = json.load(args.input)
    json.dump(process_compdb(input_json), args.output, indent=2)


if __name__ == '__main__':
    main()
