# repl characterization tests

> or, literate programming for your REPL tests!

The core idea behind repl-characterization is to make it very easy to write and maintain REPL tests by turning session transcripts directly into tests.

This infrastructure is written in C++ but we want to [rewrite it in functional2][riif2] in the near future.

[riif2]: https://git.lix.systems/lix-project/lix/issues/1196

To write a repl-characterization test, you can open up `nix repl` and then you copy paste the output into the test file, then indent it.
Add your unindented commentary alongside the output text.
Add any necessary args.
Then it should run!

## Syntax

The syntax of repl-characterization contains four types of things:
- Prompts to the repl, indented: `  nix-repl> 2 + 2` for example.
- Blocks of output, indented.
  These are denoted by indented non-empty lines, perhaps with some blank lines between them which are part of the output.
- Directives, unindented.
  These are:
  - `@args some args with "shell quoting"`.
    Adds the args to the invocation of `nix repl`.
    Multiple may be used.

    `${PWD}` will be replaced by the actual working directory.
  - `@should-start false`
    This is used to indicate that the repl is expected to fail to start, giving some output and no prompt to the system.
- Commentary.
  This is any unindented text, including blank lines outside output blocks.

One of the core ideas of the syntax is that it's possible to write an unparser for it such that we can update test session files in-place, retaining your commentary.
This is [not implemented in the C++ version][bug-autoupdate], and I think we will probably only do it in the Python version.

[bug-autoupdate]: https://git.lix.systems/lix-project/lix/issues/36

Here is a simplified version of the syntax in pseudo-[EBNF]:

[EBNF]: https://en.wikipedia.org/wiki/Extended_Backus%E2%80%93Naur_form

```ebnf
indent = "  " (* space times config.indent; 2 in practice *) ;
line-char = ( ? anything but nl ? )+ ;
nl = "\n" ;

prompt = "nix-repl>" ;
command = line-char* ;
prompt-line = indent , prompt , command ;

bool = "true" | "false" ;

args-directive = "args " , line-char+ ;
should-start-directive = "should-start " , bool ;
unknown-directive = ? this is an error ? ; (* there's a bug in the C++ impl where we don't actually error on this *)
directive = args-directive | should-start-directive | unknown-directive ;
directive-line = "@" , directive ;

(* anything unindented is a comment *)
commentary-line = line-char+ ;

output-line = indent , line-char+ ;
output-continuation = output-line? , nl ;
(* in other words: output blocks are considered to contain any *inner* blank
   lines within them, but *not* trailing blank lines. This enables updating
   them automatically while retaining the blank lines surrounding your stanzas
   in your session.

   This is implemented as a postprocessing step so that our parsing doesn't
   require lookahead. *)
output-block = output-line , nl , ((blank-line , nl)+ , output-line , nl)+ ;

input = ((prompt-line | directive-line | commentary-line) , nl) | output-block ;
file = input* ;
```

## Usage

Tests are written in `./data/*.test`, and have a corresponding macro call in `./repl_characterization.cc` (the macro thing is basically a hack because the infra was never quite finished).

You can run the suite with `just install && just test --suite installcheck repl-characterization-tests`.
