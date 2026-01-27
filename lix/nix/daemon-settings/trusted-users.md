---
name: trusted-users
internalName: trustedUsers
type: Strings
default: [root]
---
A list of user names, separated by whitespace.
These users will have additional rights when connecting to the Nix daemon, such as the ability to specify additional [substituters](#conf-substituters), or to import unsigned [NARs](@docroot@/glossary.md#gloss-nar).

You can specify groups by prefixing group names with `@`, like `@wheel` for all
users in the `wheel` group.

Both primary and supplementary groups (when the platform supports it) are
considered when determining membership. Additionally, groups a user belongs to
in the user database (e.g. LDAP if configured) are also taken into account for
access control.

> **Warning**
>
> Adding a user to `trusted-users` is essentially equivalent to giving that user root access to the system.
> For example, the user can access or replace store path contents that are critical for system security.
