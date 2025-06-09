# DEVELOPMENT
## Goals

- Eliminate implicit dependencies on files in the test directory as well as the requirement to copy the test files to the build directory as is currently hacked in the other functional test suite.
  - You should be able to write a DirectoryTree of files for your test declaratively.
- Reduce the amount of global environment state being thrown around in the test suite.
- Make tests very concise and easy to reuse code for, and hopefully turn more of what is currently code into data.
  - Provide rich ways of calling `nix` with pleasant syntax.

## Structure of the Internals

For general lib code, it is placed directly within the `testlib` folder
For fixtures see "[Writing Fixtures](#writing-fixtures)"

## Testing Internals

Ideally everything should be tested.
This includes internal code.
Create a `test_YOUR_LIB_FILE_NAME.py` file in the same folder as your library code
and refer to "[Writing Custom Tests](./USAGE.md#writing-othercustom-tests)"

## Writing Fixtures

Fixtures reside in the `testlib/fixtures` folder.
For a fixture to be automatically detected and loaded by pytest,
its module path must be added to the `pytest_plugins` attribute of the [`conftest.py`](./conftest.py) file,
similar to the other fixtures present.
For an exhaustive Documentation of Fixtures check out the [pytest documentation](https://docs.pytest.org/en/stable/how-to/fixtures.html)
The short version:
- to declare a function as a fixture, annotate it with `@pytest.fixture`
- there is a list of optional arguments one can provide:
  - `scope=SCOPE`: one of ("function", "class", "module", "package", "session"); how long the provided object will last/within what scope the provided object will be reused
  - `name`: A custom name for the fixture, useful when otherwise the function name would be shadowed (defaults to the function name)
- to use a fixture, add its name as an argument for your `test_`function.
- This also applies for fixtures: if you need a fixture to set up yours, add the name of said fixtures as a parameter for yours
- If your Fixture needs to do clean up after the test(s) have run, use the `yield` statement instead of `return`. After the Test(s) are finished, the code after the `yield` statement will execute
  - For an example, check out the [`nix` fixture](./testlib/fixtures/nix.py)

## TODO: Intended features

- Expect tests ([pytest-expect-test](https://pypi.org/project/pytest-expect-test/)) or snapshot tests ([pytest-insta](https://pypi.org/project/pytest-insta/)) or, likely, both!
  - [x] File-based snapshot tests (with a custom runner instead of `pytest-insta`)
  - [ ] Inline Python tests with `pytest-expect-tests
- [ ] Web server fixture: we don't test our network functionality because background processes are hard and this is simply goofy.
  We could just test it.
- [ ] Nix daemon fixture.
- [x] Parallelism via pytest-xdist.
