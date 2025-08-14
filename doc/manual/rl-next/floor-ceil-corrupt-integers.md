---
synopsis: builtins.floor/builtins.ceil handle out-of-range inputs correctly
issues: [nix#12899]
cls: [3923]
prs: [nix#13013]
category: "Breaking Changes"
credits: [jade, nan-git, rootile]
---
Previously, `builtins.floor` and `builtins.ceil` always cast the input into a floating point value before running the operation and casting the floating point result back into an integer.
No checks were made for precision loss in either coercing integer inputs or converting the output to an integer (and in fact in the latter case, invoked undefined behaviour).

Now, Lix checks for precision loss on integer input (to avoid a silent eval semantics change if we were to simply pass it through as-is) and on integer output.
If your code fails to evaluate after this change, use `--extra-deprecated-features floor-ceil-corrupt-integers`.
