# Repl Tests

## Writing Repl test
The `repl_characterization` tests work similar to the `lang` tests in the sense that tests aren't written directly in python, but in `.md` files instead and are auto-discovered by its framework.

Similar to `lang`, each folder represents a test-group and can contain one or more files, which are separate tests. The files and folders can be named arbitrarily, as long as the files' extensions are `.md`

Each `.md` file represents a single repl session. If you need to test things in multiple sessions, create multiple files.

Expected outputs can be updated by running the tests with the `--accept-tests` flag enabled.

### Input and output blocks
In order to pass input to a repl session, create a (fenced) codeblock using `nix` as its language. Paste in whatever commands or nix code you want to send to the repl.
Optionally, you may create another codeblock beneath, using `output` as its language to indicate its expected output. This block will otherwise be auto-generated upon running the test-suite with `--accept-tests` immediately below the input block.

> Warning:
> Due to how the REPL is implemented, multiline code is **not** supported. The real command-line repl implemented it in a very sketcy way which is not replicatable with automation.

For example:
`repl_characterization/my_test/example.md`
``````md
# This is an example for writing a repl test:

Here we have some documenation.

Below this we can see the input
```nix
1 + 1
```
The output will go below here:
```output
2
```

# Another section, this is irrelevant for the test itself, just provides more doc
you can also use `~` for codeblocks BTW
~~~nix
f = a: a + "";
~~~
Mix and match all you want (input style = output style for auto-generation of output blocks)
````output
Added f.
````
``````

### Repl options
If needed, one can change the following options using frontmatter:
- args: a list of strings added as cli arguments when the session is initialized. `{PWD}` will be replaced with the working directory.
- should_fail: boolean, if True indicates that the repl session should fail to initialize. When True, only a single `output` block is expected in the file.
- files: a list of relative paths for files to be accessible to the session

These options are defined in the `ReplTestMetadata` class of `repl_util.py`

For example:
````md
---
args: ['--repl-overlays', '{PWD}/repl-overlay-fail.nix']
should_fail: True
files: ['repl-overlay-fail.nix']
---

```output
[error output omitted here]
```

## Additional notes:
Check `repl_basics/repl_basics.md` and `repl_overlay_errors/repl_overlay_errors.md` for more examples.
Everything which isn't frontmatter or a code-block with either `nix` or `output` as its language, will be considered a comment and ignored.
The current lix version will be replaced with `VERSION` auto-magically to ensure compatibility with newer versions/commits.
