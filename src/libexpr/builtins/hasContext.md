---
name: hasContext
args: [s]
---
Return `true` if string *s* has a non-empty context.
The context can be obtained with
[`getContext`](#builtins-getContext).

> **Example**
>
> Many operations require a string context to be empty because they are intended only to work with "regular" strings, and also to help users avoid unintentionally losing track of string context elements.
> `builtins.hasContext` can help create better domain-specific errors in those case.
>
> ```nix
> name: meta:
>
> if builtins.hasContext name
> then throw "package name cannot contain string context"
> else { ${name} = meta; }
> ```
