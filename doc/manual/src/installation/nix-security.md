# Security

Lix has two basic security models. First, it can be used in “single-user
mode”, which is similar to what most other package management tools do:
there is a single user (typically root) who performs all package
management operations. All other users can then use the installed
packages, but they cannot perform package management operations
themselves.

Alternatively, you can configure Lix in “multi-user mode”. In this model, all users can perform package management operations — for instance, every user can install software for themselves without requiring root privileges.
Lix does its best to ensure that this is secure.
For instance, it would be considered a serious security bug for one untrusted user to be able to overwrite a package used by another user with a Trojan horse.

Nevertheless, the Lix team does not consider multi-user mode a strong security boundary, and does not recommend running untrusted user-supplied Nix language code on privileged machines, even if it is secure to the best of our knowledge at any moment in time.
