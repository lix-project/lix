#!/usr/bin/env nix-shell
#!nix-shell -i python3 -p python3 -p hyperfine -p "if stdenv.isLinux then linuxPackages.perf else null"

import argparse
import subprocess
import os
import json
import tempfile
import platform
import shlex
import textwrap
import dataclasses

flake_args = ["--extra-experimental-features", "nix-command flakes"]
cases = {
    "search": lambda build: [
        f"{build}/bin/nix",
        *flake_args,
        "search",
        "--no-eval-cache",
        "github:nixos/nixpkgs/e1fa12d4f6c6fe19ccb59cac54b5b3f25e160870",
        "hello",
    ],
    "rebuild": lambda build: [
        f"{build}/bin/nix",
        *flake_args,
        "eval",
        "--raw",
        "--impure",
        "--expr",
        textwrap.dedent("""
            (import <nixpkgs/nixos> {
                configuration = ./bench/nixpkgs/nixos/modules/installer/cd-dvd/installation-cd-graphical-calamares-plasma6.nix;
            }).config.system.build.toplevel
        """).replace("\n", " "),
    ],
    "rebuild_lh": lambda build: [
        "GC_INITIAL_HEAP_SIZE=10g",
        *cases['rebuild'](build),
    ],
    "parse": lambda build: [
        f"{build}/bin/nix",
        *flake_args,
        "eval",
        "-f",
        "bench/nixpkgs/pkgs/development/haskell-modules/hackage-packages.nix",
    ],
}

arg_parser = argparse.ArgumentParser()
# FIXME(jade, gilice): it is a reasonable use case to want to run a benchmark run
# on just one build. However, since we are using hyperfine in comparison
# mode, we would have to combine the JSON ourselves to support that, which
# would probably be better done by writing a benchmarking script in
# not-bash.
arg_parser.add_argument(
    'builds',
    nargs='+',
    help="At least two build directories to compare, containing bin/nix",
)
arg_parser.add_argument(
    '--cases',
    type=str,
    help="A comma-separated list of cases you want to run. Defaults to running all",
)
arg_parser.add_argument(
    '--mode',
    nargs='+',
    choices=[ "walltime", "memory" ] + [ "icount" ] if platform.system() == 'Linux' else [], # perf doesn't run on Darwin
    default=[ "walltime" ],
)
arg_parser.add_argument(
    '--daemon',
    action='store_true',
    help='Run a temporary daemon for the benchmark instead of using a local store directly',
)
args = arg_parser.parse_args()
if len(args.builds) < 1:
    raise ValueError("need at least one build directory to benchmark")

benchmarks: list[str] = []
if args.cases is None:
    benchmarks = list(cases.keys())
else:
    for case in args.cases.split(","):
        if case not in cases:
            raise ValueError(f"no such case: {case}")
        benchmarks.append(case)

def make_full_command(build, case):
    cmd = " ".join(map(shlex.quote, cases[case](build)))
    if args.daemon:
        return " ".join([
            f"{build}/bin/nix --extra-experimental-features nix-command daemon &",
            "trap 'kill %1' EXIT;",
            f"NIX_REMOTE=daemon {cmd}",
        ])
    else:
        return cmd

def bench_walltime(env):
    for case in benchmarks:
        for build in args.builds:
            subprocess.run([
                "taskset", "-c", "2,3",
                "chrt", "-f","50",
                *[
                    "hyperfine", "--warmup", "2", "--runs", "10",
                    "--export-json", f"bench/bench-{case}-{build}.json",
                    "--export-markdown", f"bench/bench-{case}-{build}.md",
                    "--", make_full_command(build, case),
                ],
            ], env=env, check=True)

    print("Benchmarks summary\n---\n")
    for case in benchmarks:
        results = []
        for build in args.builds:
            with open(f"bench/bench-{case}-{build}.json") as fd:
                results.append(json.load(fd)["results"][0])
        for result in results:
            print(result["command"])
            print("-" * min(80,len(result["command"])))
            def attr_rounded(attr):
                return f"{result[attr]:.3f}"
            print("  mean:    ", attr_rounded("mean"), "±", attr_rounded("stddev"))
            print("            user:",  attr_rounded("user"), "| system", attr_rounded("system"))
            print("  median:  ", attr_rounded("median"))
            print("  range:   ", attr_rounded("min") + "s.." + attr_rounded("max")+"s")
            print("  relative:", f"{result["mean"]/results[0]["mean"]:.3f}")
            print("\n")


def bench_icount(env):
    perf_results_for: dict[str, list[tuple[str, float]]] = {}
    for case in benchmarks:
        for build in args.builds:
            # the perf stat -j output (incorrectly) localizes numbers, which will trip up the json parser.
            env["LC_ALL"]="C"
            case_command = make_full_command(build, case)
            commandline = [
                "perf", "stat", "-o", f"bench/perf-{case}.json", "-j",
                "sh", "-c", case_command,
            ]
            print("running", case_command)
            subprocess.run(commandline, env=env, check=True, stdout=subprocess.DEVNULL) # warmup run
            subprocess.run(commandline, env=env, check=True, stdout=subprocess.DEVNULL)
            perf_fd = open(f"bench/perf-{case}.json")
            perf_data = [json.loads(x) for x in perf_fd.readlines()]
            perf_fd.close()

            instr = next(x for x in perf_data if x["event"] in ["instructions", "instructions:u"]) # an implementation of a find_first iterator
            if case not in perf_results_for:
                perf_results_for[case] = []
            perf_results_for[case].append((case_command, float(instr["counter-value"])))

    print("Benchmarks summary\n---\n")
    for (case, entries) in perf_results_for.items():
        for entry in entries:
            cmd,instr = entry
            print(cmd)
            print("-" * min(80,len(cmd)))
            print("  instructions:         ", int(instr))
            print("  relative instructions:", int(instr)/perf_results_for[case][0][1])
            print("\n")

@dataclasses.dataclass
class MemoryStatistics:
    envBytes: int
    listBytes: int
    setBytes: int
    valueBytes: int
    heapBytes: int
    heapSize: int

def bench_memory(env):
    path = "bench/bench-memory.json"
    env = env | {
        'NIX_SHOW_STATS': '1',
        'NIX_SHOW_STATS_PATH': path,
    }
    results: dict[str, list[tuple[str, MemoryStatistics]]] = {}
    for case in benchmarks:
        for build in args.builds:
            case_command = make_full_command(build, case)
            commandline = [ "sh", "-c", case_command ]
            print("running", case_command)
            subprocess.run(commandline, env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            with open(path) as fd:
                stats = json.load(fd)
            results.setdefault(case, []).append((case_command, MemoryStatistics(
                envBytes=stats['envs']['bytes'],
                listBytes=stats['list']['bytes'],
                setBytes=stats['sets']['bytes'],
                valueBytes=stats['values']['bytes'],
                heapSize=stats['gc']['heapSize'],
                heapBytes=stats['gc']['totalBytes'],
            )))

    print("Benchmarks summary\n---\n")
    for (case, entries) in results.items():
        for cmd, stats in entries:
            print(cmd)
            print("-" * min(80, len(cmd)))
            print(f"  env bytes:    {stats.envBytes  :15d}   |  {(stats.envBytes / entries[0][1].envBytes)    :.3f}x")
            print(f"  list bytes:   {stats.listBytes :15d}   |  {(stats.listBytes / entries[0][1].listBytes)  :.3f}x")
            print(f"  set bytes:    {stats.setBytes  :15d}   |  {(stats.setBytes / entries[0][1].setBytes)    :.3f}x")
            if not entries[0][1].valueBytes:
                print(f"  value bytes:  {0:15d}")
            else:
                print(f"  value bytes:  {stats.valueBytes:15d}   |  {(stats.valueBytes / entries[0][1].valueBytes):.3f}x")
            print(f"  heap alloc'd: {stats.heapBytes :15d}   |  {(stats.heapBytes / entries[0][1].heapBytes)  :.3f}x")
            print(f"  heap size:    {stats.heapSize  :15d}   |  {(stats.heapSize / entries[0][1].heapSize)    :.3f}x")
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
    subenv["NIX_PATH"] = ":".join([
        "nixpkgs=bench/nixpkgs",
    ])
    subenv["NIX_DAEMON_SOCKET_PATH"] = f"{tmp_dir}/daemon"

    for mode in args.mode:
        if mode == "walltime":
            bench_walltime(subenv)
        elif mode == "memory":
            bench_memory(subenv)
        else:
            bench_icount(subenv)
