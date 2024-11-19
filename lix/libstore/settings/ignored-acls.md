---
name: ignored-acls
internalName: ignoredAcls
platforms: [linux]
type: StringSet
default: [security.csm, security.selinux, system.nfs4_acl]
---
A list of ACLs that should be ignored, normally Lix attempts to
remove all ACLs from files and directories in the Nix store, but
some ACLs like `security.selinux` or `system.nfs4_acl` can't be
removed even by root. Therefore it's best to just ignore them.
