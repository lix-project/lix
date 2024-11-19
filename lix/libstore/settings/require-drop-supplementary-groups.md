---
name: require-drop-supplementary-groups
internalName: requireDropSupplementaryGroups
platforms: [linux]
type: bool
defaultExpr: 'getuid() == 0'
defaultText: '*running as root:* `true`, *otherwise:* `false`'
---
Following the principle of least privilege,
Lix will attempt to drop supplementary groups when building with sandboxing.

However this can fail under some circumstances.
For example, if the user lacks the `CAP_SETGID` capability.
Search `setgroups(2)` for `EPERM` to find more detailed information on this.

If you encounter such a failure, setting this option to `false` will let you ignore it and continue.
But before doing so, you should consider the security implications carefully.
Not dropping supplementary groups means the build sandbox will be less restricted than intended.
