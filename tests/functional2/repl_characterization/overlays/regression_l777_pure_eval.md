---
args: ['--pure-eval', '--repl-overlays', '{PWD}/repl-overlay-trivial.nix']
should_fail: True
---

```output
Lix VERSION
Type :? for help.
Loading 'repl-overlays'...
error: access to absolute path '/pwd/repl-overlay-trivial.nix' is forbidden in pure eval mode (use '--impure' to override)
```
> FIXME(rootile, 2026-06-09): This actually fails and the issue has been reopened.
> Big yaaaaaaay for the old stupid test suit which just didn't fail
>
> Remove the `should_fail` once this works again

Check that repl-overlays work correctly in pure eval mode

```nix
foo
:quit
```
```output

```
