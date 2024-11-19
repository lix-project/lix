---
name: no-url-literals
internalName: NoUrlLiterals
---
Disallow unquoted URLs as part of the Nix language syntax. The Nix
language allows for URL literals, like so:

```
$ nix repl
Welcome to Nix 2.15.0. Type :? for help.

nix-repl> http://foo
"http://foo"
```

But enabling this experimental feature will cause the Nix parser to
throw an error when encountering a URL literal:

```
$ nix repl --extra-experimental-features 'no-url-literals'
Welcome to Nix 2.15.0. Type :? for help.

nix-repl> http://foo
error: URL literals are disabled

at «string»:1:1:

1| http://foo
 | ^

```

While this is currently an experimental feature, unquoted URLs are
being deprecated and their usage is discouraged.

The reason is that, as opposed to path literals, URLs have no
special properties that distinguish them from regular strings, URLs
containing parameters have to be quoted anyway, and unquoted URLs
may confuse external tooling.
