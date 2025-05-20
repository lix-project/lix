# Running tests

## Unit-tests

The unit tests are defined using the [googletest] and [rapidcheck] frameworks.

[googletest]: https://google.github.io/googletest/
[rapidcheck]: https://github.com/emil-e/rapidcheck

### Source and header layout

> An example of some files, demonstrating much of what is described below
>
> ```
> …
> ├── lix
> │   ├── libexpr
> │   │   ├── …
> │   │   ├── value
> │   │   │   ├── context.cc
> │   │   │   └── context.hh
> │   …   …
> ├── tests
> │   …
> │   └── unit
> │       ├── libcmd
> │       │   └── args.cc
> │       ├── libexpr
> │       │   ├── …
> │       │   └── value
> │       │       ├── context.cc
> │       │       └── print.cc
> │       ├── libexpr-support
> │       │   └── tests
> │       │       ├── libexpr.hh
> │       │       └── value
> │       │           ├── context.cc
> │       │           └── context.hh
> │       ├── libstore
> │       │   ├── common-protocol.cc
> │       │   ├── data
> │       │   │   ├── libstore
> │       │   │   │   ├── common-protocol
> │       │   │   │   │   ├── content-address.bin
> │       │   │   │   │   ├── drv-output.bin
> …       …   …   …   …   …
> ```

The unit tests for each Lix library (`liblixexpr`, `liblixstore`, etc..) live inside a directory `lix/${library_shortname}/tests` within the directory for the library (`lix/${library_shortname}`).

The data is in `tests/unit/LIBNAME/data/LIBNAME`, with one subdir per library, with the same name as where the code goes.
For example, `liblixstore` code is in `lix/libstore`, and its test data is in `tests/unit/libstore/data/libstore`.
The path to the unit test data directory is passed to the unit test executable with the environment variable `_NIX_TEST_UNIT_DATA`.

### Running tests

You can run the whole testsuite with `just test` (see justfile for exact invocation of meson), and if you want to run just one test suite, use `just test --suite installcheck functional-init` where `installcheck` is the name of the test suite in this case and `functional-init` is the name of the test.

To get a list of tests, use `meson test -C build --list` (or `just test --list` for short).

For `installcheck` specifically, first run `just install` before running the test suite (this is due to meson limitations that don't let us put a dependency on installing before doing the test).

Finer-grained filtering within a test suite is also possible using the [--gtest_filter](https://google.github.io/googletest/advanced.html#running-a-subset-of-the-tests) command-line option to a test suite executable, or the `GTEST_FILTER` environment variable.

### Unit test support libraries

There are headers and code which are not just used to test the library in question, but also downstream libraries.
For example, we do [property testing] with the [rapidcheck] library.
This requires writing `Arbitrary` "instances", which are used to describe how to generate values of a given type for the sake of running property tests.
Because types contain other types, `Arbitrary` "instances" for some type are not just useful for testing that type, but also any other type that contains it.
Downstream types frequently contain upstream types, so it is very important that we share arbitrary instances so that downstream libraries' property tests can also use them.

It is important that these testing libraries don't contain any actual tests themselves.
On some platforms they would be run as part of every test executable that uses them, which is redundant.
On other platforms they wouldn't be run at all.

### Characterization testing

See [below](#characterization-testing-1) for a broader discussion of characterization testing.

Like with the functional characterization, `_NIX_TEST_ACCEPT=1` is also used.
For example:
```shell-session
$ _NIX_TEST_ACCEPT=1 just test --suite check libstore-unit-tests
...
../tests/unit/libstore/common-protocol.cc:27: Skipped
Cannot read golden master because another test is also updating it

../tests/unit/libstore/common-protocol.cc:62: Skipped
Updating golden master

../tests/unit/libstore/common-protocol.cc:27: Skipped
Cannot read golden master because another test is also updating it

../tests/unit/libstore/common-protocol.cc:62: Skipped
Updating golden master
...
```
will regenerate the "golden master" expected result for the `liblixstore` characterization tests.
The characterization tests will mark themselves "skipped" since they regenerated the expected result instead of actually testing anything.

## Functional tests

The functional tests reside under the `tests/functional` directory and are listed in `tests/functional/meson.build`.
Each test is a bash script.

### Running the whole test suite

<div class="warning">
FIXME(meson): this section is wrong for meson and commented out accordingly. See "Running Tests" above, and ask the Lix team if you need further clarification.
</div>

<!--
The whole test suite can be run with:

```shell-session
$ make install && make installcheck
ran test tests/functional/foo.sh... [PASS]
ran test tests/functional/bar.sh... [PASS]
...
```

### Grouping tests

Sometimes it is useful to group related tests so they can be easily run together without running the entire test suite.
Each test group is in a subdirectory of `tests`.
For example, `tests/functional/ca/local.mk` defines a `ca` test group for content-addressed derivation outputs.

That test group can be run like this:

```shell-session
$ make ca.test-group -j50
ran test tests/functional/ca/nix-run.sh... [PASS]
ran test tests/functional/ca/import-derivation.sh... [PASS]
...
```

The test group is defined in Make like this:
```makefile
$(test-group-name)-tests := \
  $(d)/test0.sh \
  $(d)/test1.sh \
  ...

install-tests-groups += $(test-group-name)
```

### Running individual tests

Individual tests can be run with `make`:

```shell-session
$ make tests/functional/${testName}.sh.test
ran test tests/functional/${testName}.sh... [PASS]
```

or without `make`:

```shell-session
$ ./mk/run-test.sh tests/functional/${testName}.sh
ran test tests/functional/${testName}.sh... [PASS]
```

To see the complete output, one can also run:

```shell-session
$ ./mk/debug-test.sh tests/functional/${testName}.sh
+ foo
output from foo
+ bar
output from bar
...
```

The test script will then be traced with `set -x` and the output displayed as it happens, regardless of whether the test succeeds or fails.

-->

### Debugging failing functional tests

When a functional test fails, it usually does so somewhere in the middle of the script.

To figure out what's wrong, it is convenient to run the test regularly up to the failing `nix` command, and then run that command with a debugger like GDB.

For example, if the script looks like:

```bash
foo
nix blah blub
bar
```
edit it like so:

```diff
 foo
-nix blah blub
+gdb --args nix blah blub
 bar
```

<div class="warning">
FIXME(meson): the command here is incorrect for meson and this whole functionality may need rebuilding.
</div>

Then, running the test with `./mk/debug-test.sh` will drop you into GDB once the script reaches that point:

```shell-session
$ ./mk/debug-test.sh tests/functional/${testName}.sh
...
+ gdb blash blub
GNU gdb (GDB) 12.1
...
(gdb)
```

One can debug the Nix invocation in all the usual ways.
For example, enter `run` to start the Nix invocation.

### Characterization testing

Occasionally, Lix utilizes a technique called [Characterization Testing](https://en.wikipedia.org/wiki/Characterization_test) as part of the functional tests.
This technique is to include the exact output/behavior of a former version of Nix in a test in order to check that Lix continues to produce the same behavior going forward.

For example, this technique is used for the language tests, to check both the printed final value if evaluation was successful, and any errors and warnings encountered.

It is frequently useful to regenerate the expected output.
To do that, rerun the failed test(s) with `_NIX_TEST_ACCEPT=1`.
For example:
```bash
_NIX_TEST_ACCEPT=1 just test --suite installcheck -v functional-lang
```

An interesting situation to document is the case when these tests are "overfitted".
The language tests are, again, an example of this.
The expected successful output of evaluation is supposed to be highly stable – we do not intend to make breaking changes to (the stable parts of) the Nix language.
However, the errors and warnings during evaluation (successful or not) are not stable in this way.
We are free to change how they are displayed at any time.

It may be surprising that we would test non-normative behavior like diagnostic outputs.
Diagnostic outputs are indeed not a stable interface, but they still are important to users.
By recording the expected output, the test suite guards against accidental changes, and ensure the *result* (not just the code that implements it) of the diagnostic code paths are under code review.
Regressions are caught, and improvements always show up in code review.

To ensure that characterization testing doesn't make it harder to intentionally change these interfaces, there always must be an easy way to regenerate the expected output, as we do with `_NIX_TEST_ACCEPT=1`.

## Integration tests

The integration tests are defined in the Nix flake under the `hydraJobs.tests` attribute.
These tests include everything that needs to interact with external services or run Lix in a non-trivial distributed setup.

You can run them manually with `nix build .#hydraJobs.tests.{testName}` or `nix-build -A hydraJobs.tests.{testName}`

<div class="warning">

Installer tests section is outdated and commented out, see https://git.lix.systems/lix-project/lix/issues/33

</div>

<!--
## Installer tests

After a one-time setup, the Lix repository's GitHub Actions continuous integration (CI) workflow can test the installer each time you push to a branch.

Creating a Cachix cache for your installer tests and adding its authorization token to GitHub enables [two installer-specific jobs in the CI workflow](https://github.com/NixOS/nix/blob/88a45d6149c0e304f6eb2efcc2d7a4d0d569f8af/.github/workflows/ci.yml#L50-L91):

- The `installer` job generates installers for the platforms below and uploads them to your Cachix cache:
  - `x86_64-linux`
  - `armv6l-linux`
  - `armv7l-linux`
  - `x86_64-darwin`

- The `installer_test` job (which runs on `ubuntu-latest` and `macos-latest`) will try to install Nix with the cached installer and run a trivial Nix command.

### One-time setup

1. Have a GitHub account with a fork of the [Nix repository](https://github.com/NixOS/nix).
2. At cachix.org:
    - Create or log in to an account.
    - Create a Cachix cache using the format `<github-username>-nix-install-tests`.
    - Navigate to the new cache > Settings > Auth Tokens.
    - Generate a new Cachix auth token and copy the generated value.
3. At github.com:
    - Navigate to your Nix fork > Settings > Secrets > Actions > New repository secret.
    - Name the secret `CACHIX_AUTH_TOKEN`.
    - Paste the copied value of the Cachix cache auth token.

## Working on documentation

### Using the CI-generated installer for manual testing

After the CI run completes, you can check the output to extract the installer URL:
1. Click into the detailed view of the CI run.
2. Click into any `installer_test` run (the URL you're here to extract will be the same in all of them).
3. Click into the `Run cachix/install-nix-action@v...` step and click the detail triangle next to the first log line (it will also be `Run cachix/install-nix-action@v...`)
4. Copy the value of `install_url`
5. To generate an install command, plug this `install_url` and your GitHub username into this template:

    ```console
    curl -L <install_url> | sh -s -- --tarball-url-prefix https://<github-username>-nix-install-tests.cachix.org/serve
    ```

<!~~ #### Manually generating test installers

There's obviously a manual way to do this, and it's still the only way for
platforms that lack GA runners.

I did do this back in Fall 2020 (before the GA approach encouraged here). I'll
sketch what I recall in case it encourages someone to fill in detail, but: I
didn't know what I was doing at the time and had to fumble/ask around a lot--
so I don't want to uphold any of it as "right". It may have been dumb or
the _hard_ way from the getgo. Fundamentals may have changed since.

Here's the build command I used to do this on and for x86_64-darwin:
nix build --out-link /tmp/foo ".#checks.x86_64-darwin.binaryTarball"

I used the stable out-link to make it easier to script the next steps:
link=$(readlink /tmp/foo)
cp $link/*-darwin.tar.xz ~/somewheres

I've lost the last steps and am just going from memory:

From here, I think I had to extract and modify the `install` script to point
it at this tarball (which I scped to my own site, but it might make more sense
to just share them locally). I extracted this script once and then just
search/replaced in it for each new build.

The installer now supports a `--tarball-url-prefix` flag which _may_ have
solved this need?
~~>

-->

## Magic environment variables

FIXME: maybe this section should be moved elsewhere or turned partially into user docs, but I just need a complete index for now.
I actually want to ban people calling getenv without writing documentation, and produce a comprehensive list of env-vars used by Lix and enforce it.

This is a non-exhaustive list of almost all environment variables, magic or not, accepted or used by various parts of the test suite as well as Lix itself.
Please add more if you find them.

I looked for these in the testsuite with the following bad regexes:

```
rg '(?:[^A-Za-z]|^)(_[A-Z][^-\[ }/:");$(]+)' -r '$1' --no-filename --only-matching tests | sort -u > vars.txt
rg '\$\{?([A-Z][^-\[ }/:");]+)' -r '$1' --no-filename --only-matching tests | sort -u > vars.txt
```

I grepped `lix/` for `get[eE]nv\("` to find the mentions in Lix code.

### Used by Lix testing support code

- `_NIX_TEST_ACCEPT` (optional) - Writes out the result of a characterization test as the new expected value.
  **Expected value**: 1

- `_NIX_TEST_UNIT_DATA` - The path to the directory for the data for a given unit test suite.

  **Expected value**: `tests/unit/libstore/data/libstore` or similar


### Used by Lix

- `_NIX_FORCE_HTTP` - Forces file URIs to be treated as remote ones.

  Used by `lix/libfetchers/git.cc`, `lix/libstore/http-binary-cache-store.cc`,
  `lix/libstore/local-binary-cache-store.cc`. Seems to be for forcing Git
  clones of `git+file://` URLs, making the HTTP binary
  cache store accept `file://` URLs (presumably passing them to curl?), and
  unknown reasons for the local binary cache.

  FIXME(jade): is this obscuring a bug in https://git.lix.systems/lix-project/lix/issues/200?

  **Expected value**: 1
- `NIX_ATTRS_SH_FILE`, `NIX_ATTRS_JSON_FILE` (output) - Set by Lix builders; see
  `structuredAttrs` documentation.
- `NIX_BIN_DIR`, `NIX_STORE_DIR` (or its inconsistently-used old alias `NIX_STORE`), `NIX_DATA_DIR`,
  `NIX_LOG_DIR`, `NIX_LOG_DIR`, `NIX_STATE_DIR`, `NIX_CONF_DIR` -
  Overrides compile-time configuration of various locations used by Lix. See `lix/libstore/globals.cc`.

  **Expected value**: a directory
- `NIX_DAEMON_SOCKET_PATH` (optional) - Overrides the daemon socket path from `$NIX_STATE_DIR/daemon-socket/socket`.

  **Expected value**: path to a socket
- `NIX_LOG_FD` (output) - An FD number for logs in `internal-json` format to be sent to.
  Used for, mostly, "setPhase" in nixpkgs setup.sh, but can also be creatively used to print verbose log messages from derivations.

  **Provided value**: number corresponding to an FD in the builder
- `NIX_PATH` - Search path for `<whatever>`. Documented elsewhere in the manual.

  **Expected value**: `:` separated list of things that are not necessarily pointing to filesystem paths
- `NIX_REMOTE` - The default value of the Lix setting `store`.

  **Expected value**: "daemon", usually. Could be "auto" or any other value acceptable in `store`.
- `NIX_BUILD_SHELL` - Documented elsewhere; the shell to invoke with `nix-shell` but not `nix develop`/`nix shell`.
  The latter ignoring it altogether seems like a bug.

  **Expected value**: the path to an executable shell
- `PRINT_PATH` - Undocumented. Used by `nix-prefetch-url` as an alternative form of `--print-path`. Why???
- `_NIX_IN_TEST` - If present with any value, makes `fetchClosure` accept file URLs in addition to HTTP ones. Why is this not `_NIX_FORCE_HTTP`??

  Not used anywhere else.
- `NIX_ALLOW_EVAL` - Used by eval-cache tests to block evaluation if set to `0`.

  **Expected value**: 1 or 0
- `EDITOR` - Used by `editorFor()`, which has some extremely sketchy editor-detection code for jumping to line numbers.
- `LISTEN_FDS` and `LISTEN_PID` - Used for systemd socket activation using the systemd socket activation protocol.
- `NIX_PAGER` (alternatively, `PAGER`) - Used to select a pager for Lix output. Why does this not use libutil `getEnv()`?
- `LESS` (output) - Sets the pager settings for `less` when invoked by Lix.
- `NIX_IGNORE_SYMLINK_STORE` - When set, Lix allows the store to be a symlink. Why do we support this?

  Apparently [someone was using it enough to fix it](https://github.com/NixOS/nix/pull/4038).
- `NIX_SSL_CERT_FILE` (alternatively, `SSL_CERT_FILE`) - Used to set CA certificates for libcurl.

  **Expected value**: "/etc/ssl/certs/ca-certificates.crt" or similar
- `NIX_REMOTE_SYSTEMS` - Used to set `builders`. Can we please deprecate this?
- `NIX_USER_CONF_FILES` - `:` separated list of config files to load before
  `/nix/nix.conf` under each of `XDG_CONFIG_DIRS`.
- `NIX_CONFIG` - Newline separated configuration to load into Lix.
- `NIX_GET_COMPLETIONS` - Returns completions.
  Unsure of the exact format, someone should document it; either way my shell never had any completions.

  **Expected value**: number of completions to return.
- `IN_SYSTEMD` - Used to switch the logging format so that systemd gets the correct log levels. I think.
- `NIX_HELD_LOCKS` - Not used, what is this for?? We should surely remove it right after searching github?
- `GC_INITIAL_HEAP_SIZE` - Used to set the initial heap size, processed by boehmgc.
- `NIX_COUNT_CALLS` - Documented elsewhere; prints call counts for profiling purposes.
- `NIX_SHOW_STATS` - Documented elsewhere; prints various evaluation statistics like function calls, gc info, and similar.
- `NIX_SHOW_STATS_PATH` - Writes those statistics into a file at the given path instead of stdout. Undocumented.
- `NIX_SHOW_SYMBOLS` - Dumps the symbol table into the show-stats json output.
- `TERM` - If `dumb` or unset, disables ANSI colour output.
- `FORCE_COLOR`, `CLICOLOR_FORCE` - Enables ANSI colour output if `NO_COLOR`/`NOCOLOR` not set.
- `NO_COLOR`, `NOCOLOR` - Disables ANSI colour output.
- `_NIX_DEVELOPER_SHOW_UNKNOWN_LOCATIONS` - Highlights unknown locations in errors.
- `NIX_PROFILE` - Selects which profile `nix-env` will operate on. Documented elsewhere.
- `NIX_SSHOPTS` - Options passed to `ssh(1)` when using a ssh remote store.
  Incorrectly documented on `nix-copy-closure` which is *surely* not the only place they are used??
- `_NIX_TEST_GC_SYNC_1` - Path to a pipe that is used to block the GC briefly to validate invariants from the test suite.
- `_NIX_TEST_GC_SYNC_2` - Path to a pipe that is used to block the GC briefly to validate invariants from the test suite.
- `_NIX_TEST_FREE_SPACE_FILE` - Path to a file containing a decimal number with the free space that the GC is to believe it has.
- Various XDG vars
- `NIX_DEBUG_SQLITE_TRACES` - Dump all sqlite queries to the log at `notice` level.
- `_NIX_TEST_NO_SANDBOX` - Disables actually setting up the sandbox on macOS while leaving other logic the same. Unused on other platforms.
- `_NIX_TRACE_BUILT_OUTPUTS` - Dumps all the derivation paths alongside their outputs as lines into a file of the given name.

### Used by the functional test framework

- `NIX_DAEMON_PACKAGE` - Runs the test suite against an alternate Nix daemon with the current client.

  **Expected value**: something like `/nix/store/...-nix-2.18.2`
- `NIX_CLIENT_PACKAGE` - Runs the test suite against an alternate Nix client with the current daemon.

  **Expected value**: something like `/nix/store/...-nix-2.18.2`
- `TEST_DATA` - Not an environment variable! This is used in repl characterization tests to refer to `tests/functional/repl_characterization/data`.
  More specifically, that path is replaced with the string `$TEST_DATA` in output for reproducibility.
- `TEST_HOME` (output) - Set to the temporary directory that is set as `$HOME` inside the tests, underneath `$TEST_ROOT`.
- `TEST_ROOT` (output) - Set to the temporary directory that is created for each test to mess with.
- `_NIX_TEST_DAEMON_PID` (output) - Used to track the daemon pid to be able to kill it.

  **Provided value**: Daemon pid as a base-10 integer, e.g. 2345
