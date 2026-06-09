
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
as it automatically discovers file-based tests from within subdirectories (see ["Writing lang tests"](./lang/README.md#writing-lang-tests)).

The `repl_characterization` test package which contains interactive repl tests and is, similar to `lang`, somewhat special.
see ["Writing repl tests"](./repl_characterization/README.md#writing-repl-tests) for further information.

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
from testlib.fixtures.nix import Nix

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
from testlib.fixtures.pytest_command import Command
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

#### [`env`](./testlib/fixtures/env.py)

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
from testlib.fixtures.file_helper import File, Symlink, with_files

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
from testlib.fixtures.file_helper import File, with_files


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
from testlib.fixtures.file_helper import AssetSymlink, with_files


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
from testlib.fixtures.nix import Nix

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
