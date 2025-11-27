#!@python@
# ruff: noqa: SIM112 # ignore lowercase env variable names for capnpc as we have them in lower case as arguments

import argparse
import capnp
from pathlib import Path
import os
import subprocess
import sys

if lang := os.environ.get("lix_capnp_lang"):
    outputs = os.environ["lix_capnp_outputs"].split()
    old_cwd = os.environ["lix_capnp_old_cwd"]
    schema = capnp.load("@capnp_include@/capnp/schema.capnp", imports=["@capnp_include@"])
    request = schema.CodeGeneratorRequest.read(sys.stdin)

    subprocess.run([lang], input=request.as_builder().to_bytes()).check_returncode()

    base_dir = Path.cwd()
    os.chdir(old_cwd)

    include = [str(Path(p).resolve()) for p in os.environ["lix_capnp_include"].split(":")]

    if depfile := os.environ["lix_capnp_depfile"]:
        deps = ""
        for input_file in request.requestedFiles:
            deps += " ".join(f"{input_file.filename}.{o}" for o in outputs)
            deps += ":"
            for dep in input_file.imports:
                if dep.name.startswith("/"):
                    for candidate in (Path(i + dep.name) for i in include):
                        if candidate.exists():
                            deps += " " + str(candidate)
                            break
                else:
                    msg = "not handling relative includes"
                    raise RuntimeError(msg)
            deps += "\n\n"
        Path(depfile).write_text(deps)
else:
    parser = argparse.ArgumentParser()
    parser.add_argument("--language")
    parser.add_argument("--outdir")
    parser.add_argument("--src-prefix")
    parser.add_argument("--depfile", default="")
    parser.add_argument("-I", "--include", action="append", default=["@capnp_include@"])
    parser.add_argument("inputs", nargs="+")
    args = parser.parse_args()

    for infile in args.inputs:
        os.environ["lix_capnp_lang"] = f"capnpc-{args.language}"
        os.environ["lix_capnp_include"] = ":".join(args.include)
        os.environ["lix_capnp_depfile"] = args.depfile
        os.environ["lix_capnp_old_cwd"] = str(Path.cwd())
        if args.language == "c++":
            os.environ["lix_capnp_outputs"] = "c++ h"
        else:
            raise RuntimeError("unknown language " + args.language)
        subprocess.run(
            [
                "@capnp@",
                "compile",
                f"-o{sys.argv[0]}:{args.outdir}",
                f"--src-prefix={args.src_prefix}",
                *(f"-I{i}" for i in args.include),
                infile,
            ]
        ).check_returncode()
