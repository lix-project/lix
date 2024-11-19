# Clang tidy lints for Lix

This is a skeleton of a clang-tidy lints library for Lix.

Currently there is one check (which is already obsolete as it has served its
goal and is there as an example), `HasPrefixSuffixCheck`.

## Running fixes/checks

One file:

```
ninja -C build && clang-tidy --checks='-*,lix-*' --load=build/liblix-clang-tidy.so -p ../../build -header-filter '\.\./lix/.*\.h' --fix ../../lix/libcmd/installables.cc
```

Several files, in parallel:

```
ninja -C build && run-clang-tidy -checks='-*,lix-*' -load=build/liblix-clang-tidy.so -p ../../build -header-filter '\.\./lix/.*\.h' -fix ../../lix | tee -a clang-tidy-result
```

## Resources

* https://firefox-source-docs.mozilla.org/code-quality/static-analysis/writing-new/clang-query.html
* https://clang.llvm.org/docs/LibASTMatchersReference.html
* https://devblogs.microsoft.com/cppblog/exploring-clang-tooling-part-3-rewriting-code-with-clang-tidy/

## Developing new checks

Put something like so in `myquery.txt`:

```
set traversal     IgnoreUnlessSpelledInSource
# ^ Ignore implicit AST nodes. May need to use AsIs depending on how you are
# working.
set bind-root     true
# ^ true unless you use any .bind("foo") commands
set print-matcher true
enable output     dump
match callExpr(callee(functionDecl(hasName("hasPrefix"))), optionally(hasArgument( 0, cxxConstructExpr(hasDeclaration(functionDecl(hasParameter(0, parmVarDecl(hasType(asString("const char *"))).bind("meow2"))))))))
```

Then run, e.g. `clang-query --preload hasprefix.query -p compile_commands.json lix/libcmd/installables.cc`.

With this you can iterate a query before writing it in C++ and suffering from
C++.

### Tips and tricks for the C++

There is a function `dump()` on many things that will dump to stderr. Also
`llvm::errs()` lets you print to stderr.

When I wrote `HasPrefixSuffixCheck`, I was not really able to figure out how
the structured replacement system was supposed to work. In principle you can
describe the replacement with a nice DSL. Look up the Stencil system in Clang
for details.
