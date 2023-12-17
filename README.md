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
{"attr":"coverage","attrPath":["coverage"],"drvPath":"/nix/store/fmbqzaq8mim1423879lhn9whs6imx5w4-patchelf-coverage-0.18.0.drv","inputDrvs":{"/nix/store/23632hx2c98lbbjld279dx0w08lxn6kp-hook.drv":["out"],"/nix/store/6z1jfnqqgyqr221zgbpm30v91yfj3r45-bash-5.1-p16.drv":["out"],"/nix/store/ap9g09fxbicj836zm88d56dn3ff4clxl-stdenv-linux.drv":["out"],"/nix/store/c0gg7lj101xhd8v2b3cjl5dwwkpxfc0q-patchelf-tarball-0.18.0.drv":["out"],"/nix/store/vslywm6kbazi37q1vbq8y7bi884yc6yx-lcov-1.16.drv":["out"],"/nix/store/y964yq4vz1gsn7azd44vyg65gnr4gpvi-hook.drv":["out"]},"name":"patchelf-coverage-0.18.0","outputs":{"out":"/nix/store/gfni9sbhhwhxxfqziq1fs3n82bvw962l-patchelf-coverage-0.18.0"},"system":"x86_64-linux"}
{"attr":"patchelf-win32","attrPath":["patchelf-win32"],"drvPath":"/nix/store/s38l0fg5ja6j8qpws7slw2ws0c6v0qcf-patchelf-i686-w64-mingw32-0.18.0.drv","inputDrvs":{"/nix/store/6z1jfnqqgyqr221zgbpm30v91yfj3r45-bash-5.1-p16.drv":["out"],"/nix/store/b2p151ilwqpd47fbmzz50a5cmj12ixbf-hook.drv":["out"],"/nix/store/fbnhh18m4jh6cwa92am2sv3aqzjnzpdd-stdenv-linux.drv":["out"]},"name":"patchelf-i686-w64-mingw32-0.18.0","outputs":{"out":"/nix/store/w8r4h1xk71fryb99df8aszp83kfhw3bc-patchelf-i686-w64-mingw32-0.18.0"},"system":"x86_64-linux"}
{"attr":"patchelf-win64","attrPath":["patchelf-win64"],"drvPath":"/nix/store/wxpym6d3dxr1w9syhinp7f058gwxfmd3-patchelf-x86_64-w64-mingw32-0.18.0.drv","inputDrvs":{"/nix/store/6z1jfnqqgyqr221zgbpm30v91yfj3r45-bash-5.1-p16.drv":["out"],"/nix/store/71lv5lsr1y59bv1b91jc9gg0n85kf1sq-stdenv-linux.drv":["out"],"/nix/store/b2p151ilwqpd47fbmzz50a5cmj12ixbf-hook.drv":["out"]},"name":"patchelf-x86_64-w64-mingw32-0.18.0","outputs":{"out":"/nix/store/fkq5428l2xsb84yj0cc6q1lkvsrga7sv-patchelf-x86_64-w64-mingw32-0.18.0"},"system":"x86_64-linux"}
{"attr":"release","attrPath":["release"],"drvPath":"/nix/store/3xpwg8f623dpkh6cblv2fzcq5n99xl0j-patchelf-0.18.0.drv","inputDrvs":{"/nix/store/6z1jfnqqgyqr221zgbpm30v91yfj3r45-bash-5.1-p16.drv":["out"],"/nix/store/9rmihrl9ys0sap6827xyns0y73vqafjx-patchelf-0.18.0.drv":["out"],"/nix/store/am2zqx3pyc1i14f888jna785h0f841sg-patchelf-0.18.0.drv":["out"],"/nix/store/c0gg7lj101xhd8v2b3cjl5dwwkpxfc0q-patchelf-tarball-0.18.0.drv":["out"],"/nix/store/csjiccxbwpfv55m8kqs2xwrkkha14dnq-patchelf-0.18.0.drv":["out"],"/nix/store/jsrnpxdx5vmpnakd9bkb3sk3lgh0k8hm-patchelf-0.18.0.drv":["out"],"/nix/store/k8a51ax83554c67g98xf3y751vjgjs7m-patchelf-0.18.0.drv":["out"],"/nix/store/wq3ncl207isqqkqmsa5ql4fg19jbrhxg-stdenv-linux.drv":["out"]},"name":"patchelf-0.18.0","outputs":{"out":"/nix/store/d0mzprvv3vhasj23r1a6qn8qip0srbc4-patchelf-0.18.0"},"system":"x86_64-linux"}
{"attr":"tarball","attrPath":["tarball"],"drvPath":"/nix/store/c0gg7lj101xhd8v2b3cjl5dwwkpxfc0q-patchelf-tarball-0.18.0.drv","inputDrvs":{"/nix/store/6z1jfnqqgyqr221zgbpm30v91yfj3r45-bash-5.1-p16.drv":["out"],"/nix/store/9d754glmsvpjm5kxvgsjslvgv356kbmn-libtool-2.4.7.drv":["out"],"/nix/store/ap9g09fxbicj836zm88d56dn3ff4clxl-stdenv-linux.drv":["out"],"/nix/store/f1ksgsyplvb0sli4pls6k6vsfvmv519d-autoconf-2.71.drv":["out"],"/nix/store/jf58lcnch1bmpbi2188c59w5zr1cqrx2-automake-1.16.5.drv":["out"]},"name":"patchelf-tarball-0.18.0","outputs":{"out":"/nix/store/72pz5awc7gpwdqxrdsy8j0bvg2n7z78q-patchelf-tarball-0.18.0"},"system":"x86_64-linux"}
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
  --eval-store
            The [URL of the Nix store](@docroot@/command-ref/new-cli/nix3-help-stores.md#store-url-format)
            to use for evaluation, i.e. to store derivations (`.drv` files) and inputs referenced by them.

  --expr                 treat the argument as a Nix expression
  --flake                build a flake
  --force-recurse        force recursion (don't respect recurseIntoAttrs)
  --gc-roots-dir         garbage collector roots directory
  --help                 show usage information
  --impure               allow impure expressions

  --log-format           Set the format of log output; one of `raw`, `internal-json`, `bar` or `bar-with-logs`.
  --max-memory-size      maximum evaluation memory size in megabyte (4GiB per worker by default)
  --meta                 include derivation meta field in output
  --option               Set the Nix configuration setting *name* to *value* (overriding `nix.conf`).
  --override-flake       Override the flake registries, redirecting *original-ref* to *resolved-ref*.
  --override-input       Override a specific flake input (e.g. `dwarffs/nixpkgs`).
  --quiet                Decrease the logging verbosity level.
  --repair               During evaluation, rewrite missing or corrupted files in the Nix store. During building, rebuild missing or corrupted store paths.
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

### How can I check if my package already have been uploaded in the binary cache?

If you provide the `--check-cache-status`, the json will contain a `"isCached"`
key in its json, that is true or false based on the status.

### How can I evaluate nixpkgs?

If you want to evaluate nixpkgs in the same way
[hydra](https://hydra.nixos.org/) does it, use this snippet:

```console
$ nix-eval-jobs --force-recurse pkgs/top-level/release.nix
```

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
