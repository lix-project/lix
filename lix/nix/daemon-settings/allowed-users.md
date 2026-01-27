---
name: allowed-users
internalName: allowedUsers
type: Strings
default: ['*']
---
A list user names, separated by whitespace.
These users are allowed to connect to the Nix daemon.

You can specify groups by prefixing group names with `@`, like `@wheel` for all
users in the `wheel` group. To allow all users, use `*`.

Both primary and supplementary groups (when the platform supports it) are
considered when determining membership. Additionally, groups a user belongs to
in the user database (e.g. LDAP if configured) are also taken into account for
access control.

> **Note**
>
> Trusted users (set in [`trusted-users`](#conf-trusted-users)) can always connect to the Nix daemon.
