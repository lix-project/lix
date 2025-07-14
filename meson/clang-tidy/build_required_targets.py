#!/usr/bin/env python3
import subprocess

def get_targets_of_rule(build_root: str, rule_name: str) -> list[str]:
    return subprocess.check_output(['ninja', '-C', build_root, '-t', 'targets', 'rule', rule_name]).decode().strip().splitlines()

def ninja_build(build_root: str, targets: list[str]):
    subprocess.check_call(['ninja', '-C', build_root, '--', *targets])

def main():
    import argparse
    ap = argparse.ArgumentParser(description='Builds required targets for clang-tidy')
    ap.add_argument('build_root', help='Ninja build root', type=str)

    args = ap.parse_args()

    targets = [
        t for t in get_targets_of_rule(args.build_root, 'CUSTOM_COMMAND')
        if t.endswith('.gen.hh')
    ] + [
        t for t in get_targets_of_rule(args.build_root, 'CUSTOM_COMMAND_DEP')
        if t.endswith('.capnp.h')
    ] + [
        t for t in get_targets_of_rule(args.build_root, 'CUSTOM_COMMAND')
        if t.endswith('.gen.inc')
    ]
    ninja_build(args.build_root, targets)

if __name__ == '__main__':
    main()
