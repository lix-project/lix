#!@python@

import argparse
import capnp
from pathlib import Path
import os
import subprocess
import sys

if lang := os.environ.get('lix_capnp_lang'):
    outputs = os.environ['lix_capnp_outputs'].split()
    old_cwd = os.environ['lix_capnp_old_cwd']
    schema = capnp.load('@capnp_include@/capnp/schema.capnp', imports=['@capnp_include@'])
    request = schema.CodeGeneratorRequest.read(sys.stdin)

    subprocess.run([lang], input=request.as_builder().to_bytes()).check_returncode()

    base_dir = os.getcwd()
    os.chdir(old_cwd)

    include = [ str(Path(p).resolve()) for p in os.environ['lix_capnp_include'].split(':') ]

    if depfile := os.environ['lix_capnp_depfile']:
        deps = ""
        for input in request.requestedFiles:
            deps += " ".join(f"{input.filename}.{o}" for o in outputs)
            deps += ":"
            for dep in input.imports:
                if dep.name.startswith("/"):
                    for candidate in (Path(i + dep.name) for i in include):
                        if candidate.exists():
                            deps += " " + str(candidate)
                            break
                else:
                    raise RuntimeError("not handling relative includes")
            deps += "\n\n"
        Path(depfile).write_text(deps)
else:
    parser = argparse.ArgumentParser()
    parser.add_argument('--language')
    parser.add_argument('--outdir')
    parser.add_argument('--src-prefix')
    parser.add_argument('--depfile', default="")
    parser.add_argument('-I', '--include', action='append', default=['@capnp_include@'])
    parser.add_argument('inputs', nargs='+')
    args = parser.parse_args()

    for infile in args.inputs:
        os.environ['lix_capnp_lang'] = f"capnpc-{args.language}"
        os.environ['lix_capnp_include'] = ':'.join(args.include)
        os.environ['lix_capnp_depfile'] = args.depfile
        os.environ['lix_capnp_old_cwd'] = os.getcwd()
        if args.language == "c++":
            os.environ['lix_capnp_outputs'] = "c++ h"
        else:
            raise RuntimeError("unknown language " + args.language)
        subprocess.run([
            '@capnp@',
            'compile',
            f'-o{sys.argv[0]}:{args.outdir}',
            f'--src-prefix={args.src_prefix}',
            *(f"-I{i}" for i in args.include),
            infile
        ]).check_returncode()
