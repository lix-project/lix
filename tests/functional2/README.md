# functional2 tests

This uncreatively named test suite is a Pytest based replacement for the shell framework used to write traditional Nix integration tests.
Its primary goal is to make tests more concise, more self-contained, easier to write, and to produce better errors.

## Goals

- Eliminate implicit dependencies on files in the test directory as well as the requirement to copy the test files to the build directory as is currently hacked in the other functional test suite.
  - You should be able to write a DirectoryTree of files for your test declaratively.
- Reduce the amount of global environment state being thrown around in the test suite.
- Make tests very concise and easy to reuse code for, and hopefully turn more of what is currently code into data.
  - Provide rich ways of calling `nix` with pleasant syntax.

## TODO: Intended features

- [ ] Expect tests ([pytest-expect-test]) or snapshot tests ([pytest-insta]) or, likely, both!
  We::jade prefer to have short output written in-line as it makes it greatly easier to read the tests, but pytest-expect doesn't allow for putting larger stuff in external files, so something else is necessary for those.
- [ ] Web server fixture: we don't test our network functionality because background processes are hard and this is simply goofy.
  We could just test it.
- [ ] Nix daemon fixture.
- [ ] Parallelism via pytest-xdist.

[pytest-expect-test]: https://pypi.org/project/pytest-expect-test/
[pytest-insta]: https://pypi.org/project/pytest-insta/
