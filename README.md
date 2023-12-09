# nix-eval-jobs

This project evaluates nix attribute sets in parallel with streamable json
output. This is useful for time and memory intensive evaluations such as NixOS
machines, i.e. in a CI context. The evaluation is done with a controllable
number of threads that are restarted when their memory consumption exceeds a
certain threshold.

To facilitate integration, nix-eval-jobs creates garbage collection roots for
each evaluated derivation (drv file, not the build) within the provided
attribute. This prevents race conditions between the nix garbage collection
service and user-started nix builds processes.

## Why using nix-eval-jobs?

- Faster evaluation by using threads
- Memory used for evaluation is reclaimed after nix-eval-jobs finish, so that
  the build can use it.
- Evaluation of jobs can fail individually

## Example

In the following example we evaluate the hydraJobs attribute of the
[patchelf](https://github.com/NixOS/patchelf) flake:

```console
$ nix-eval-jobs --gc-roots-dir gcroot --flake 'github:NixOS/patchelf#hydraJobs'
{"attr":"coverage","attrPath":["coverage"],"drvPath":"/nix/store/8hq9f09xa5s6g9m02lw0sw59kkkvj57c-patchelf-coverage-0.15.0.drv","name":"patchelf-coverage-0.15.0","outputs":{"out":"/nix/store/dwf255bdbfvvbiqak941r83zlvxyipcs-patchelf-coverage-0.15.0"},"system":"x86_64-linux"}
{"attr":"release","attrPath":["release"],"drvPath":"/nix/store/ip9dy4vlyha5a7kq4bnf4pxk0sfwjfda-patchelf-0.15.0.drv","name":"patchelf-0.15.0","outputs":{"out":"/nix/store/5z9ynn29asakf1b5736im2glcqpf6s2f-patchelf-0.15.0"},"system":"x86_64-linux"}
{"attr":"tarball","attrPath":["tarball"],"drvPath":"/nix/store/g1alnfi3mrkcb9blclr77fpyp35mpsdd-patchelf-tarball-0.15.0.drv","name":"patchelf-tarball-0.15.0","outputs":{"out":"/nix/store/iy0w42pffhjg6wy0w46r4cjc1yjk410y-patchelf-tarball-0.15.0"},"system":"x86_64-linux"}
```

The output here is newline-seperated json according to https://jsonlines.org.

The code is derived from [hydra's](https://github.com/nixos/hydra) eval-jobs
executable.

## Further options

```console
$ nix-eval-jobs --help
USAGE: nix-eval-jobs [options] expr

  --arg                  Pass the value *expr* as the argument *name* to Nix functions.
  --argstr               Pass the string *string* as the argument *name* to Nix functions.
  --check-cache-status   Check if the derivations are present locally or in any configured substituters (i.e. binary cache). The information will be exposed in the `isCached` field of the JSON output.
  --debug                Set the logging verbosity level to 'debug'.
  --eval-store           The Nix store to use for evaluations.
  --expr                 treat the argument as a Nix expression
  --flake                build a flake
  --gc-roots-dir         garbage collector roots directory
  --help                 show usage information
  --impure               allow impure expressions
  --include              Add *path* to the list of locations used to look up `<...>` file names.
  --log-format           Set the format of log output; one of `raw`, `internal-json`, `bar` or `bar-with-logs`.
  --max-memory-size      maximum evaluation memory size
  --meta                 include derivation meta field in output
  --option               Set the Nix configuration setting *name* to *value* (overriding `nix.conf`).
  --override-flake       Override the flake registries, redirecting *original-ref* to *resolved-ref*.
  --quiet                Decrease the logging verbosity level.
  --show-trace           print out a stack trace in case of evaluation errors
  --verbose              Increase the logging verbosity level.
  --workers              number of evaluate workers
```

## Potential use-cases for the tool

**Faster evaluator in deployment tools.** When evaluating NixOS machines,
evaluation can take several minutes when run on a single core. This limits
scalability for large deployments with deployment tools such as
[NixOps](https://github.com/NixOS/nixops).

**Faster evaluator in CIs.** In addition to evaluation speed for CIs, it is also
useful if evaluation of individual jobs in CIs can fail, as opposed to failing
the entire jobset. For CIs that allow dynamic build steps to be created, one can
also take advantage of the fact that nix-eval-jobs outputs the derivation path
separately. This allows separate logs and success status per job instead of a
single large log file. In the
[wiki](https://github.com/nix-community/nix-eval-jobs/wiki#ci-example-configurations)
we collect example ci configuration for various CIs.

## Organisation of this repository

On the `main` branch we target nixUnstable. When a release of nix happens, we
fork for a release branch i.e. `release-2.8` and change the nix version in
`.nix-version`. Changes and improvements made in `main` also may be backported
to these release branches. At the time of writing we only intent to support the
latest release branch.

## Projects using nix-eval-jobs

- [nix-fast-build](https://github.com/Mic92/nix-fast-build) - Combine the power
  of nix-eval-jobs with nix-output-monitor to speed-up your evaluation and
  building process
- [buildbot-nix](https://github.com/Mic92/buildbot-nix) - A nixos module to make
  buildbot a proper Nix-CI
- [colmena](https://github.com/zhaofengli/colmena) - A simple, stateless NixOS
  deployment tool
- [robotnix](https://github.com/danielfullmer/robotnix) - Build Android (AOSP)
  using Nix, used in their
  [CI](https://github.com/danielfullmer/robotnix/blob/38b80700ee4265c306dcfdcce45056e32ab2973f/.github/workflows/instantiate.yml#L18)

## FAQ

### nix-eval-jobs consumes too much memory / is too slow

By default, nix-eval-jobs spawns as many worker processes as there are hardware
threads in the system and limits the memory usage for each worker to 4GB.

However, keep in mind that each worker process may need to re-evaluate shared
dependencies of the attributes, which can introduce some overhead for each
evaluation or cause workers to exceed their memory limit. If you encounter these
situations, you can tune the following options:

`--workers`: This option allows you to set the number of evaluation workers that
nix-eval-jobs should spawn. You can increase or decrease this number to optimize
the evaluation speed and memory usage. For example, if you have a system with
many CPU cores but limited memory, you may want to reduce the number of workers
to avoid exceeding the memory limit.

`--max-memory-size`: This option allows you to adjust the memory limit for each
worker process. By default, it's set to 4GiB, but you can increase or decrease
this value as needed. For example, if you have a system with a lot of memory and
want to speed up the evaluation, you may want to increase the memory limit to
allow workers to cache more data in memory before getting restarted by
nix-eval-jobs. Note that this is not a hard limit and memory usage may rise
above the limit momentarily before the worker process exits.

Overall, tuning these options can help you optimize the performance and memory
usage of nix-eval-jobs to better fit your system and evaluation needs.
