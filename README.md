# nix-eval-jobs

This project evaluates nix attributes sets in parallel with a streamable json output.
This is useful for time and memory-intensive evaluations such as nixos machines i.e. in a CI context.
Evaluation happens with a controlable number of threads that are restarted if
their memory consumption grows beyond a threshold.

For ease of integration nix-eval-jobs creates garbage collection roots for each
evaluated derivation (drv file not the build) inside the supplied attribute.
This prevent race conditions between nix garbage collection service and nix
builds processes started by the user.

## Why using nix-eval-jobs?

- Faster evaluation due the use of threads
- Memory used for evaluation is reclaimed after nix-eval-jobs is finished so that the build can use it.
- Evaluation of jobs can fail individually

## Example

In the following example we evaluate the hydraJobs attribute of the [patchelf](https://github.com/NixOS/patchelf) flake:

```console
$ nix-eval-jobs --gc-roots-dir $(pwd)/gcroot --flake 'github:NixOS/patchelf#hydraJobs'
{"attr":"build-sanitized-clang.aarch64-linux","drvPath":"/nix/store/361mr6bzzwcv65sp0bhbakaa21fj4p1b-patchelf-0.13.20210926.18a389b.drv","name":"patchelf-0.13.20210926.18a389b","system":"x86_64-linux"}
{"attr":"build-sanitized-clang.i686-linux","drvPath":"/nix/store/ial7z46jy8kivmq5dz6f9vqr0b70jqkd-patchelf-0.13.20210926.18a389b.drv","name":"patchelf-0.13.20210926.18a389b","system":"x86_64-linux"}
{"attr":"build-sanitized-clang.x86_64-linux","drvPath":"/nix/store/h2m3k085m21gd3cxc4n1wzhcjv3iap9m-patchelf-0.13.20210926.18a389b.drv","name":"patchelf-0.13.20210926.18a389b","system":"x86_64-linux"}
{"attr":"build-sanitized.aarch64-linux","drvPath":"/nix/store/m9jl25lcwvdk8rz79ibzd55wqfaxhdxx-patchelf-0.13.20210926.18a389b.drv","name":"patchelf-0.13.20210926.18a389b","system":"aarch64-linux"}
{"attr":"build-sanitized.i686-linux","drvPath":"/nix/store/0njjscgha4smzd9qsi4839pbsyqs18zl-patchelf-0.13.20210926.18a389b.drv","name":"patchelf-0.13.20210926.18a389b","system":"i686-linux"}
{"attr":"build-sanitized.x86_64-linux","drvPath":"/nix/store/cp8z7idqzf2cvfj9lzyr3xqll26bbz76-patchelf-0.13.20210926.18a389b.drv","name":"patchelf-0.13.20210926.18a389b","system":"x86_64-linux"}
{"attr":"build.aarch64-linux","drvPath":"/nix/store/rsgwdq3503ibln8hwilbl8ifjhrlb9mv-patchelf-0.13.20210926.18a389b.drv","name":"patchelf-0.13.20210926.18a389b","system":"aarch64-linux"}
{"attr":"build.i686-linux","drvPath":"/nix/store/l5k6ma3lrb2rmbw50s8s8x4c4wvj35s7-patchelf-0.13.20210926.18a389b.drv","name":"patchelf-0.13.20210926.18a389b","system":"i686-linux"}
{"attr":"build.x86_64-linux","drvPath":"/nix/store/lmhpwvj4y9ypz5rgp0y1jbw2vqryc80l-patchelf-0.13.20210926.18a389b.drv","name":"patchelf-0.13.20210926.18a389b","system":"x86_64-linux"}
{"attr":"coverage","drvPath":"/nix/store/hlh7x41c2nnklbnhrc41wm2rir0l3zq3-patchelf-coverage-0.13.20210926.18a389b.drv","name":"patchelf-coverage-0.13.20210926.18a389b","system":"x86_64-linux"}
{"attr":"release","drvPath":"/nix/store/b1jfn3pjdhq1ds4d52sj8k2z33lmb3jk-patchelf-0.13.20210926.18a389b.drv","name":"patchelf-0.13.20210926.18a389b","system":"x86_64-linux"}
{"attr":"tarball","drvPath":"/nix/store/jcharij3ylh36hvszb48j2pzjas9hmx1-patchelf-tarball-0.13.20210926.18a389b.drv","name":"patchelf-tarball-0.13.20210926.18a389b","system":"x86_64-linux"}
```

The output here newline-seperated json according to https://jsonlines.org/

The code is derived from [hydra's](https://github.com/nixos/hydra) eval-jobs executable.

## Further options

``` console
$ nix-eval-jobs --help
USAGE: nix-eval-jobs [options] expr

  --arg                  Pass the value *expr* as the argument *name* to Nix functions.
  --argstr               Pass the string *string* as the argument *name* to Nix functions.
  --debug                Set the logging verbosity level to 'debug'.
  --eval-store           The Nix store to use for evaluations.
  --flake                build a flake
  --gc-roots-dir         garbage collector roots directory
  --help                 show usage information
  --impure               set evaluation mode
  --include              Add *path* to the list of locations used to look up `<...>` file names.
  --log-format           Set the format of log output; one of `raw`, `internal-json`, `bar` or `bar-with-logs`.
  --max-memory-size      maximum evaluation memory size
  --option               Set the Nix configuration setting *name* to *value* (overriding `nix.conf`).
  --override-flake       Override the flake registries, redirecting *original-ref* to *resolved-ref*.
  --quiet                Decrease the logging verbosity level.
  --verbose              Increase the logging verbosity level.
  --workers              number of evaluate workers
```


## Potential use-cases for the tool

**Faster evaluator in deployment tools.** When evaluating nixos machines evaluation can take several minutes when performed on a single core.
This limits the usuability of current deployment tools such as [NixOps](https://github.com/NixOS/nixops).
**Faster evaluator in CI.** In addition to evaluation speed for CIs it is also useful if evaluation of individual jobs can fail in CIs in contrast to failing the whole jobset.
Furthermore for CIs that allow to create dynamic build steps, one can leverage the fact that nix-eval-jobs outputs derivation path seperatly.
This allows to have seperate logs and success status per job rather than one big log file.
