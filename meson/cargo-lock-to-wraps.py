#!/usr/bin/env python3

import argparse
import tomllib
import sys

DOWNLOAD_URI_FORMAT = 'https://crates.io/api/v1/crates/{crate}/{version}/download'

WRAP_TEMPLATE = """
[wrap-file]
method = cargo
directory = {crate}-{version}
source_url = {url}
source_filename = {crate}-{version}.tar.gz
source_hash = {hash}
""".lstrip()

parser = argparse.ArgumentParser()
parser.add_argument('lockfile', help='path to the Cargo lockfile to generate wraps from')
parser.add_argument('outdir', help="the 'subprojects' directory to write .wrap files to")

args = parser.parse_args()

with open(args.lockfile, 'rb') as f:
    lock_toml = tomllib.load(f)

for dependency in lock_toml['package']:
    try:
        hash = dependency['checksum']
    except KeyError:
        # The base package, e.g. lix-doc, won't have a checksum, and conveniently
        # the base package is also not something we want a wrap file for.
        # Doesn't that work out nicely?
        continue

    crate = dependency['name']
    version = dependency['version']

    url = DOWNLOAD_URI_FORMAT.format(crate=crate, version=version)

    wrap_text = WRAP_TEMPLATE.format(crate=crate, version=version, url=url, hash=hash)
    with open(f'{args.outdir}/{crate}-rs.wrap', 'w') as f:
        f.write(wrap_text)
