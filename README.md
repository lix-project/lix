# nix-eval-jobs

This project evaluates nix attributes sets in parallel with streamable json
output.  This is useful for time and memory intensive evaluations such as NixOS
machines, i.e. in a CI context.  The evaluation is done with a controllable
number of threads that are restarted when their memory consumption exceeds a
certain threshold.

To facilitate integration, nix-eval-jobs creates garbage collection roots for
each evaluated derivation (drv file, not the build) within the provided
attribute.  This prevents race conditions between the nix garbage collection
service and user-started nix builds processes.

## Why using nix-eval-jobs?

- Faster evaluation by using threads
- Memory used for evaluation is reclaimed after nix-eval-jobs finish, so that the build can use it.
- Evaluation of jobs can fail individually

## Example

In the following example we evaluate the hydraJobs attribute of the [patchelf](https://github.com/NixOS/patchelf) flake:

```console
$ nix-eval-jobs --gc-roots-dir $(pwd)/gcroot --flake 'github:NixOS/patchelf#hydraJobs'
{"attr":"build-sanitized-clang.x86_64-linux","drvPath":"/nix/store/igmkq61cwys8nj34yqvnpdg921h0i0mp-patchelf-0.14.3.drv","name":"patchelf-0.14.3","outputs":{"out":"/nix/store/nwwgff1fwkws4wxv7k7cfvvin8ab9gbh-patchelf-0.14.3"},"system":"x86_64-linux"}
{"attr":"build-sanitized.aarch64-linux","drvPath":"/nix/store/d8ma8d7gjwx6ix4ibs910z9fkm3hwdvz-patchelf-0.14.3.drv","name":"patchelf-0.14.3","outputs":{"out":"/nix/store/6j26m4sznwdyfk4sbmnls3sk0lxm38ih-patchelf-0.14.3"},"system":"aarch64-linux"}
{"attr":"build-sanitized.i686-linux","drvPath":"/nix/store/87rwijvfqqs7dw9lbmckmz4nbryvjaq3-patchelf-0.14.3.drv","name":"patchelf-0.14.3","outputs":{"out":"/nix/store/za5w0gzf97na44fza9sdys15qnjqayd7-patchelf-0.14.3"},"system":"i686-linux"}
{"attr":"build-sanitized.x86_64-linux","drvPath":"/nix/store/nmx50wly2qvd00svx0vqsjfh0jv7q3kl-patchelf-0.14.3.drv","name":"patchelf-0.14.3","outputs":{"out":"/nix/store/38d6bhz3a5jq48gm1diji0rjfcm5vi9n-patchelf-0.14.3"},"system":"x86_64-linux"}
{"attr":"build.aarch64-linux","drvPath":"/nix/store/yjz9msbr6pl8mj7im5kiyhk7wwkvxywa-patchelf-0.14.3.drv","name":"patchelf-0.14.3","outputs":{"out":"/nix/store/as9xhcfwnhfy5x30kxh7lfgla1qrk182-patchelf-0.14.3"},"system":"aarch64-linux"}
{"attr":"build.i686-linux","drvPath":"/nix/store/nwcmdcimnaci0knri5ga019lgbvc4am4-patchelf-0.14.3.drv","name":"patchelf-0.14.3","outputs":{"out":"/nix/store/64x12dmbscnnl42r4y2av52y55ksphhk-patchelf-0.14.3"},"system":"i686-linux"}
{"attr":"build.x86_64-linux","drvPath":"/nix/store/k6p4qnjryr2l1lz31pf085ay9bd7j8gj-patchelf-0.14.3.drv","name":"patchelf-0.14.3","outputs":{"out":"/nix/store/h9a779ghpibfqkkdchx6s08bb3v3i8vy-patchelf-0.14.3"},"system":"x86_64-linux"}
{"attr":"coverage","drvPath":"/nix/store/lsrg05dx3hyi5b6ak99pn9g1rn8xwx39-patchelf-coverage-0.14.3.drv","name":"patchelf-coverage-0.14.3","outputs":{"out":"/nix/store/6h4l5axy5lvxzq662yw47y9r60mxw3zz-patchelf-coverage-0.14.3"},"system":"x86_64-linux"}
{"attr":"release","drvPath":"/nix/store/dgn5gy64pjskfnv7vqh0s86nb998f8sq-patchelf-0.14.3.drv","name":"patchelf-0.14.3","outputs":{"out":"/nix/store/nn05yaznr5af8g8mpgd82yx16pvfzjcy-patchelf-0.14.3"},"system":"x86_64-linux"}
{"attr":"tarball","drvPath":"/nix/store/5ajrgfd5nx29ykgg942k154mcaqfbhxd-patchelf-tarball-0.14.3.drv","name":"patchelf-tarball-0.14.3","outputs":{"out":"/nix/store/5cli6rh0h32yhfcgjkgbplcc73cqvplv-patchelf-tarball-0.14.3"},"system":"x86_64-linux"}

```

The output here is newline-seperated json according to https://jsonlines.org.

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
  --meta                 include derivation meta field in output
  --option               Set the Nix configuration setting *name* to *value* (overriding `nix.conf`).
  --override-flake       Override the flake registries, redirecting *original-ref* to *resolved-ref*.
  --quiet                Decrease the logging verbosity level.
  --verbose              Increase the logging verbosity level.
  --workers              number of evaluate workers
```


## Potential use-cases for the tool

**Faster evaluator in deployment tools.** When evaluating NixOS machines,
evaluation can take several minutes when run on a single core.  This limits
scalability for large deployments with deployment tools such as
[NixOps](https://github.com/NixOS/nixops).

**Faster evaluator in CIs.** In addition to evaluation speed for CIs, it is also
useful if evaluation of individual jobs in CIs can fail, as opposed to failing
the entire jobset.  For CIs that allow dynamic build steps to be created, one
can also take advantage of the fact that nix-eval-jobs outputs the derivation
path separately.  This allows separate logs and success status per job instead
of a single large log file.


## Organisation of this repository

On the `main` branch we target nixUnstable. When a release of nix happens, we
fork for a release branch i.e. `release-2.8` and change the nix version in
`.nix-version`. Changes and improvements made in `main` also may be backported
to these release branches. At the time of writing we only intent to support the
latest release branch.


## Projects using nix-eval-jobs

- [colmena](https://github.com/zhaofengli/colmena) -  A simple, stateless NixOS deployment tool
- [robotnix](https://github.com/danielfullmer/robotnix) -  Build Android (AOSP) using Nix, used in their [CI](https://github.com/danielfullmer/robotnix/blob/38b80700ee4265c306dcfdcce45056e32ab2973f/.github/workflows/instantiate.yml#L18)
