---
synopsis: "`disallowedRequisites` now reports chains of disallowed requisites"
issues: [fj#334,fj#626,gh#10877]
category: Improvements
credits: [ma27,roberth]
---

When a build fails because of [`disallowedRequisites`](@docroot@/language/advanced-attributes.md#adv-attr-disallowedRequisites), the error message now includes the chain of references that led to the failure. This makes it easier to see in which derivations the chain can be broken, to resolve the problem.

Example:

```
$ nix-build -A hello
error: output '/nix/store/0b7k85gg5r28gb54px9nq7iv5986mns9-hello-2.12.2' is not allowed to refer to the following paths:
       /nix/store/eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee-glibc-2.40-66
       Shown below are chains that lead to the forbidden path(s).
       /nix/store/0b7k85gg5r28gb54px9nq7iv5986mns9-hello-2.12.2
       └───/nix/store/eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee-glibc-2.40-66
```
