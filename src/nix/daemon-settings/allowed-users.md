---
name: allowed-users
internalName: allowedUsers
type: Strings
default: ['*']
---
A list user names, separated by whitespace.
These users are allowed to connect to the Nix daemon.

You can specify groups by prefixing names with `@`.
For instance, `@wheel` means all users in the `wheel` group.
Also, you can allow all users by specifying `*`.

> **Note**
>
> Trusted users (set in [`trusted-users`](#conf-trusted-users)) can always connect to the Nix daemon.
