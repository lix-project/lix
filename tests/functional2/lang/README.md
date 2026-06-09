# Lang Tests

## Writing lang tests

The `lang` tests are special in that in addition to the usual Python tests, it also has a framework for automatically creating tests purely from resource files.

Each folder in `lang` is a test, although it may contain multiple subtests.
If a folder is a Python module (i.e. has an `__init__.py`), it will be treated as a Python test instead.

There are two different ways of creating Lang tests.
The "simple" way with no `test.toml`, which does more automagic and requires less setup but also is less powerful,
and the "complex" way, which allows full control over the test runners via a `test.toml`.

### Lang test runners

All lang tests focus on testing either the parsing or evaluation of Nix code, which yields for available runners:

- `eval-okay`: run eval, expecting an exit code of 0
- `eval-fail`: run eval, expecting an exit code of 1
- `parse-okay`: run parser, expecting an exit code of 0, stdout will be converted from json to yaml
- `parse-fail`: run parser, expecting an exit code of 1

The runners will test a given input file, and assert that the Nix stdout and stderr match the golden files.
Typical names for these are `eval-fail.err.exp` or `parse-okay.out.exp`, indicating which runner they are referring to and whether they contain stdout or stderr.
If an `*.out.exp` or `*.err.exp` is not present for a test, the Nix output will be *expected* to be empty (i.e. no file is equivalent to empty file).

### Without `test.toml`

A test without `test.toml` simply contains the following files:

- `in.nix`: The input Nix code
- `RUNNER_NAME.out.exp` and/or `RUNNER_NAME.err.exp`: Contain the expected output for stdout and stderr respectively
  - The output of multiple runners may be provided, in which all of them will be run against the input file individually.

When creating a test, simply `touch` the desired `exp` files and use `--accept-tests` to fill them.

It is possible to use different `in`-files within the same folder:
to do so, suffix them with `-number-or-descriptive-string` resulting in `in-number-or-descriptive-string.nix`.
Add the same suffix to the expected output file, e.g. `eval-okay-number-or-descriptive-string.out.exp`
The tests will be discovered by searching the expected output files for the corresponding in files.

### With `test.toml`

A `test.toml` allows for the following additional test configuration features:

- Specifying additional CLI arguments to Nix
- Running the same runner (e.g. `eval-okay`) with different CLI flags
- Providing additional Nix files, e.g. for `import` tests

The `test.toml` has the following structure:

```toml
[[test]]
runner = "RUNNER_NAME"

[[test]]
name = "another-test"
runner = "eval-fail"
flags = ["-some", "-new", "flag"]

[[test]]
name = "third-runner"
runner = "eval-okay"
extra-files = ["./other_nix_file.nix"]
```
- The top-level element is a list called `test`. One can add a new element by labeling a section `[[test]]`
- `runner` must be one of `"eval-okay"`, `"eval-fail"`, `"parse-okay"`, `"parse-fail"`
- `name` defaults to `runner` plus the suffix of the given in file. Must be set manually if multiple test use the same runner and files.
- `matrix` optional boolean, which indicates if the test will be run on a single file or multiple. defaults to `False` (single file)
- `in` optional argument to specify on what files to run.
  - For non-matrix tests, it must be a single file name and defaults to `"in.nix"`
  - For matrix tests, it must be a list of file names and defaults to all available in files.
- `flags` optionally specifies a list of additional CLI arguments to be passed to Nix
- `extra-files` optionally describes a list of (relative) file paths for additional files to be copied into the test directory before execution.
- `global-assets` optionally describes a list of global assets like `config.nix` to be copied into the test directory before execution.

As before, the test folder contains an `in.nix` together with the expected output files.
For these, the naming scheme is
`CUSTOM_NAME.out.exp`/`CUSTOM_NAME.err.exp`, where `CUSTOM_NAME` stands for the name given in the `test.toml`.

**Example:**

```toml
[[test]]
runner = "parse-okay"
flags = ["--no-warning"]

[[test]]
# We must set this name manually to avoid collision
name = "parse-okay-with-warning"
runner = "parse-okay"

[[test]]
runner = "eval-fail"
```

Which implies the following files to exist:

```
in.nix
test.toml
parse-okay.out.exp
parse-okay-with-warning.out.exp
parse-okay-with-warning.err.exp
eval-fail.err.exp
```

When creating a test, simply writing the `in.nix` and `test.toml` is sufficient, all `.exp` files can be automatically generated with `--accept-tests`.


Here too, it is possible to work with multiple input files, though it works slightly differently to without a test toml:
- for non-matrix tests, set the `in` parameter to the name of the according in file, equivalent to tests without a toml.
- for matrix tests, either not set the `in` parameter (this will test the runner on *all* present in files) or set it to a list of in file names.

### Additional Notes
- The file [`lib.nix`](./lang/lib.nix) is a general library file available to all test, and for that copied into each lang test's directory automatically.
  - There is no need to declare and copy it for each individual test that needs it, `import ./lib.nix` will always work out of the box.
- In the `test.toml`, it is currently not supported to pass paths with subdirectories into the `extra-files` attribute. If that functionality is required, use a [pytest tests](#writing-python-tests) instead.
  - It is possible to call the according test runner function directly to avoid boilerplate
- If additional functionalities are required, placing a `.py` file in the directory tells the framework to ignore it. One can then write [pytest tests](#writing-python-tests) as usual
- The test suit will fail, if any files are unused. This is done to avoid unrecognized tests due to bad naming.
