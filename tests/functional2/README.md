
# functional2 test suite

This uncreatively named test suite is a Pytest based replacement for the shell framework used to write traditional Nix integration tests.
Its primary goal is to make tests more concise, more self-contained, easier to write, and to produce better errors.

See [DEVELOPMENT](./DEVELOPMENT.md) for more details about the test infrastructure.

[TOC]

## Structure overview

The test suite is a standalone Python project,
tests are grouped by their general subject into Python packages, e.g. `commands`, `eval`, `flakes`, `lang` etc.
When additional files are required for a test, they are placed within a folder `assets/test_file_name/` inside the test's directory.

The `lang` test package which contains all parser and evaluator tests is somewhat special,
as it automatically discovers file-based tests from within subdirectories (see ["Writing lang tests"](#writing-lang-tests)).

Tests for the test suite itself are located in the `testlib` package.

## Running Tests

```just test functional2``` will run the entire test suite which will call Python from our meson build infrastructure.
Alternatively, ```just test-functional2``` will run the test suite through pytest directly, which allows providing additional arguments to pytest.

A quick primer on useful `pytest` arguments:

- `/path/to/folder/or/module.py`: run tests of the given file or folder
- `-k EXPRESSION`: the expression is any substring, which the test name (or parent) must match against
  - the full internal test name is `/path/to/test_file.py::test_function_name[parametrization_id]`
  - hence providing `-k lang` will test everything within the lang folder and other tests which have `lang` in their name
  - more specifically providing `-k "lang and with"` will only execute tests which have both `lang` and `with` in their test name (i.e. `lang/test_lang.py::test_eval[with-eval-okay]`)
- `--log-cli-level=DEBUG`: for more informative logs (default level is `INFO`)

## Writing Python tests

To write a new test, create a new module in the appropriate package.
For tests to be recognized by pytest, they must have the following naming scheme:

- The module/filename must start with `test_` e.g. `test_file_helper.py`
- The test function itself must start with `test_` e.g. `test_my_feature()`
- If the test function is within a class, the classname must start with `Test` e.g. `class TestEvalThings` (discouraged)

Additional testing resources like a temporary directory, asset files, nix commands or a logger can be imported using [fixtures](#useful-fixtures).

To check if something equals or a condition is true, use the `assert` expression:

```python
from functional2.testlib.fixtures.nix import Nix

# Trivial test
def test_example():
    assert "ell" in "Hello"
    assert "World" in "Hello", "Missing world"

# Simple test that runs `nix-instantiate` with some arguments and checks its output
def test_err_context(nix: Nix):
    result = nix.nix_instantiate(
        ["--show-trace", "--eval", "-E", """'builtins.addErrorContext "Hello" (throw "Foo")'"""]
    ).run()
    assert "Hello" in result.expect(1).stderr_s
```

It is possible to call other test functions from within a test.
This is especially useful for something like the lang test runners, when more setup or complex file structures are required.
Please note that when calling a test function directly, pytest will not set up the fixtures and all arguments have to be provided manually:

```python
import pytest

@pytest.mark.parametrize("a", ["x", "y", "z"])
def test_1(a: int, snapshot):
  assert snapshot("result") == a


def test_2(snapshot):
  x = _do_some_stuff()
  # even though we don't need snapshot ourselves,
  # we still need to have it as a parameter
  # to be able to pass it to test_1
  test_1(x, snapshot)

```

When additional files are required for a test, they are placed within a folder `assets/test_file_name/` inside the test's directory (i.e. next to the test file).
This allows for less clutter and for easier navigation to the actual tests, while allowing for fast navigation to files used by the test.
Conversely, this convention makes it easy to see which test an asset belongs to.

### How to parameterize a test

For a more exhaustive documentation, check the [pytest documentation](https://docs.pytest.org/en/stable/example/parametrize.html).
When parametrizing a test, the test is called once per provided argument.
When a test is parametrized multiple times, every combination of parameters will be run in a matrix.
If that is not intended, one can provide multiple tuple arguments within a single parametrization:

```python
import pytest
@pytest.mark.parametrize("a", [1,2,3])
def test_simple_parameters(a: int):
    # is run 3 times; once using 1, once 2 and once using 3
    # resulting in the following output:
    # 1
    # 2
    # 3
    print(a)
@pytest.mark.parametrize("a", [1,2])
@pytest.mark.parametrize("b", ["A", "B"])
def test_double_parameters(a: int, b: str):
    # is run 4 times, resulting in the following output:
    # 1 A
    # 1 B
    # 2 A
    # 2 B
    print(a, b)
@pytest.mark.parametrize(("a", "b"), [(1, "A"), (2, "B")])
def test_both_params_at_once(a: int, b: str):
    # is run twice resulting in the following output:
    # 1 A
    # 2 B
    print(a, b)
```

#### Indirect parametrization

Injecting parameters into a test that are controlled by [fixtures](#useful-fixtures) requires setting `indirect=True` to the parametrization:

This is currently only used by the `pytest_command` fixture, to test our framework.
```python
import pytest
from functional2.testlib.fixtures.pytest_command import Command
@pytest.mark.parametrize("pytest_command",
    [
        ["-k", "fun"],
        ["-k", "cake", "--accept-tests"],
    ],
    indirect=True,
)
def test_pytest_collection(pytest_command: Command):
    # is called twice, resulting in the following output:
    # ["pytest", "-k", "fun"]
    # ["pytest", "-k", "cake", "--accept-tests"]
    print(pytest_command.argv)
```
Without `indirect=True`, the function would get the raw argument lists and not the configured Command.

### Useful fixtures

A fixture provides a defined, reliable and consistent context for the tests.
This could include environment (for example a database configured with known parameters) or content (such as a dataset).
For a full intro on what exactly fixtures are, see the Pytest documentation ["About fixtures"](https://docs.pytest.org/en/stable/explanation/fixtures.html#about-fixtures).
For an exhaustive documentation on how fixtures work and how to use them check the Pytest documentation ["How-to use fixtures"](https://docs.pytest.org/en/stable/how-to/fixtures.html).
For a complete list of fixtures and their documentation, run ```just test-functional2 --fixtures```.

#### [`env`](./testlib/fixtures/command.py)

Declarative Environment used by `Command` to execute sub commands (often `nix`)
without leaking any global configuration.
Provides rich access to setting and unsetting variables
as well as modifying `PATH`.

#### [`command`](./testlib/fixtures/command.py)

Get a function to create Commands with the given arguments and stdin.

This fixture pre-applies the [declarative environment](#env).
If one is already requesting the `env` fixture,
one can alternatively instantiate the `Command` class directly, passing in `env` to the `_env` argument


#### [`files`](./testlib/fixtures/file_helper.py)

Pass in file resources into the test's temporary runtime directory.

The output type is `Path` and its value is always the same as from the Pytest `tmp_path` fixture.

The input type is [FileDeclaration](./testlib/fixtures/file_helper.py), a dict from file path to `Fileish`. Subclasses of `Fileish` are:

- `File("content")`: Specify the file's content inline in Python. Optional additional argument: `mode`.
- `CopyFile("input/path")`: Copy the specified local file
- `CopyTree("input/path")`: Like `CopyFile`, but recursively
- `CopyTemplate("input/path", { "replace": "with"})`: Like `CopyFile`, but with an additional set of substitutions where each instance of "@key@" in the input file will be replaced by its associated value.
- `Symlink("target/path")`: Create a symlink with the specified target. Therefore, relative paths are relative to the symlink's location.
- `AssetSymlink("source/path)`: Create a symlink pointing to a local asset file within the test suite. Paths must be relative and will be resolved relative to the current Python file. The created symlink will be absolute.

In order to make its usage easier, one can also use this fixture by using our custom decorator.

```python
import pytest
from pathlib import Path
from functional2.testlib.fixtures.file_helper import File, Symlink, with_files

@with_files(
    {
        "test.txt": File("File content"),
        "some-symlink": Symlink("../in.nix"),
    }
)
def test_using_files(files: Path):
    print((files / "test.txt").read_text())
```
To run a function twice with a different set of files, one can pass multiple dictionaries to the mark:

```python
from pathlib import Path
from functional2.testlib.fixtures.file_helper import File, with_files


@with_files(
    {"test.txt": File("fileset 1")},
    {"test.txt": File("fileset 2")},
)
def test_using_files(files: Path):
    print((files / "test.txt").read_text())
```

#### [`snapshot`](./testlib/fixtures/snapshot.py)

Provides a `snapshot` function which facilitates file-based snapshot testing.
`snapshot` is called with a path to a file containing the golden output, which can then be compared to any actual output/result using `==`.
The provided path is relative to the temporary directory provided by [`files`](#files) or `tmp_path`.

To update the golden value, run pytest with `--accept-tests` or set the `_NIX_TEST_ACCEPT` environment variable.

In order for the golden values to actually update within the code base (compared to only in the test's temporary directory), they need to be passed as symlinks.

```python
import pytest
from functional2.testlib.fixtures.file_helper import AssetSymlink, with_files


@with_files(
  # Set up the symlink so that the golden value will update
  { "out": AssetSymlink("assets/test_example/out.exp"), }
)
def test_example(snapshot):
    # snapshot must be on the LHS of ==
    assert snapshot("out") == "Hello World"
```

#### [`nix`](./testlib/fixtures/nix.py)

An object which provides nix executables.

```python
from functional2.testlib.fixtures.nix import Nix

# Simple test that runs `nix-instantiate` with some arguments and checks its output
def test_err_context(nix: Nix):
    result = nix.nix_instantiate(
        ["--show-trace", "--eval", "-E", """'builtins.addErrorContext "Hello" (throw "Foo")'"""]
    ).run()
    assert "Hello" in result.expect(1).stderr_s
```

The base function of is `nix.nix(args)`.
`nix.nix_instantiate`, `nix.nix_build`, `nix.eval` are convenience wrapper which do the expected thing.

`nix` can be configured to run Nix in many additional ways, including environment and Nix settings.

#### [`logger`](./testlib/fixtures/logger.py)

Provides a logger scoped for the current test.
Calls to the logger are captured separately by pytest and results in more beautiful test results and failure prints and don't interfere with stdout or stderr captures from [nix](#nix) or similar sub-processes.

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
