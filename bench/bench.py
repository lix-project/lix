#!/usr/bin/env nix-shell
#!nix-shell -i python3 -p python3 -p hyperfine -p "if stdenv.isLinux then linuxPackages.perf else null"

import argparse
import subprocess
import os
import json
import tempfile
import platform

flake_args = ["--extra-experimental-features","'nix-command flakes'"]
# hyperfine has its own variable substitution, so we use that and pass build="{BUILD}" here.
# perf doesn't have variable substitution, so we call these with build being the actual build directory.
cases = {
    "search": lambda build: [f"{build}/bin/nix", *flake_args, "search", "--no-eval-cache", "github:nixos/nixpkgs/e1fa12d4f6c6fe19ccb59cac54b5b3f25e160870", "hello"],
    "rebuild": lambda build: [f"{build}/bin/nix", *flake_args, "eval", "--raw", "--impure", "--expr", "'with import <nixpkgs/nixos> {}; system'"],
    "rebuild_lh": lambda build: ["GC_INITIAL_HEAP_SIZE=10g", f"{build}/bin/nix", *flake_args, "eval", "--raw", "--impure", "--expr", "'with import <nixpkgs/nixos> {}; system'"],
    "parse": lambda build: [f"{build}/bin/nix", *flake_args, "eval", "-f", "bench/nixpkgs/pkgs/development/haskell-modules/hackage-packages.nix"],
}

arg_parser = argparse.ArgumentParser()
# FIXME(jade, gilice): it is a reasonable use case to want to run a benchmark run
# on just one build. However, since we are using hyperfine in comparison
# mode, we would have to combine the JSON ourselves to support that, which
# would probably be better done by writing a benchmarking script in
# not-bash.
arg_parser.add_argument('builds', nargs='+', help="At least two build directories to compare, containing bin/nix")
arg_parser.add_argument('--cases', type=str, help="A comma-separated list of cases you want to run. Defaults to running all")
available_modes = [ "walltime" ] + [ "icount" ] if platform.system() == 'Linux' else [] # perf doesn't run on Darwin
arg_parser.add_argument('--mode', choices=available_modes, default="walltime")
args = arg_parser.parse_args()
if len(args.builds) < 2:
    raise ValueError("need at least two build directories to compare")

benchmarks: list[str] = []
if args.cases is None:
    benchmarks = list(cases.keys())
else:
    for case in args.cases.split(","):
        if case not in cases: raise ValueError(f"no such case: {case}")
        benchmarks.append(case)

def bench_walltime(env):
    hyperfine_args = ["--parameter-list", "BUILD", ','.join(args.builds), "--warmup", "2", "--runs", "10"]
    for case in benchmarks:
        case_command = cases[case]("{BUILD}") # see the comment on cases
        subprocess.run([
            "taskset", "-c", "2,3",
            "chrt", "-f","50",
            "hyperfine", *hyperfine_args, "--export-json", f"bench/bench-{case}.json", "--export-markdown", f"bench/bench-{case}.md", "--", " ".join(case_command)
        ], env=env, check=True)

    print("Benchmarks summary\n---\n")
    for case in benchmarks:
        fd = open(f"bench/bench-{case}.json")
        result_json = json.load(fd)
        fd.close()
        for result in result_json["results"]:
            print(result["command"])
            print("-" * min(80,len(result["command"])))
            attr_rounded = lambda attr: f"{result[attr]:.3f}"
            print("  mean:    ", attr_rounded("mean"), "±", attr_rounded("stddev"))
            print("            user:",  attr_rounded("user"), "| system", attr_rounded("system"))
            print("  median:  ", attr_rounded("median"))
            print("  range:   ", attr_rounded("min") + "s.." + attr_rounded("max")+"s")
            print("  relative:", f"{result["mean"]/result_json["results"][0]["mean"]:.3f}")
            print("\n")


def bench_icount(env):
    perf_results_for: dict[str, list[tuple[str, float]]] = {}
    for case in benchmarks:
        for build in args.builds:
            case_command = cases[case](build)
            # the perf stat -j output (incorrectly) localizes numbers, which will trip up the json parser.
            env["LC_ALL"]="C"
            commandline = [
                "perf", "stat", "-o", f"bench/perf-{case}.json", "-j", "sh", "-c", " ".join(case_command)
            ]
            subprocess.run(commandline, env=env, check=True, stdout=subprocess.DEVNULL) # warmup run
            subprocess.run(commandline, env=env, check=True, stdout=subprocess.DEVNULL)
            perf_fd = open(f"bench/perf-{case}.json")
            perf_data = [json.loads(x) for x in perf_fd.readlines()]
            perf_fd.close()

            instr = next(x for x in perf_data if x["event"] in ["instructions", "instructions:u"]) # an implementation of a find_first iterator
            if case not in perf_results_for: perf_results_for[case] = []
            perf_results_for[case].append((" ".join(case_command), float(instr["counter-value"])))

    print("Benchmarks summary\n---\n")
    for (case, entries) in perf_results_for.items():
        for entry in entries:
            cmd,instr = entry
            print(cmd)
            print("-" * min(80,len(cmd)))
            print("  instructions:         ", int(instr))
            print("  relative instructions:", int(instr)/perf_results_for[case][0][1])
            print("\n")


with tempfile.TemporaryDirectory() as tmp_dir:
    subprocess.run([
        "nix", "build",
        "--extra-experimental-features", "nix-command flakes",
        "--impure", "--expr",'(builtins.getFlake "git+file:.").inputs.nixpkgs.outPath',
        "-o","bench/nixpkgs"
    ], check=True)
    subenv = os.environ.copy()
    subenv["NIX_CONF_DIR"] = "/var/empty"
    subenv["NIX_REMOTE"] = tmp_dir
    subenv["NIX_PATH"] = "nixpkgs=bench/nixpkgs:nixos-config=bench/configuration.nix"

    if args.mode == "walltime":
        bench_walltime(subenv)
    else:
        bench_icount(subenv)
