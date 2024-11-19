---
name: tail
args: [list]
---
Return the second to last elements of a list; abort evaluation if
the argument isn’t a list or is an empty list.

> **Warning**
>
> This function should generally be avoided since it's inefficient:
> unlike Haskell's `tail`, it takes O(n) time, so recursing over a
> list by repeatedly calling `tail` takes O(n^2) time.
