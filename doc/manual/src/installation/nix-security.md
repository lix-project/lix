# Security

Lix has two basic security models. First, it can be used in “single-user
mode”, which is similar to what most other package management tools do:
there is a single user (typically root) who performs all package
management operations. All other users can then use the installed
packages, but they cannot perform package management operations
themselves.

Alternatively, you can configure Lix in “multi-user mode”. In this
model, all users can perform package management operations — for
instance, every user can install software without requiring root
privileges. Lix ensures that this is secure. For instance, it’s not
possible for one user to overwrite a package used by another user with a
Trojan horse.
