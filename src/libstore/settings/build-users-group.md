---
name: build-users-group
internalName: buildUsersGroup
type: std::string
defaultExpr: '""' # overridden in the code if running as root
defaultText: '*running as root:* `nixbld`, *otherwise:* *empty*'
---
This options specifies the Unix group containing the Lix build user
accounts. In multi-user Lix installations, builds should not be
performed by the Lix account since that would allow users to
arbitrarily modify the Nix store and database by supplying specially
crafted builders; and they cannot be performed by the calling user
since that would allow them to influence the build result.

Therefore, if this option is non-empty and specifies a valid group,
builds will be performed under the user accounts that are a member
of the group specified here (as listed in `/etc/group`). Those user
accounts should not be used for any other purpose\!

Lix will never run two builds under the same user account at the
same time. This is to prevent an obvious security hole: a malicious
user writing a Nix expression that modifies the build result of a
legitimate Nix expression being built by another user. Therefore it
is good to have as many Lix build user accounts as you can spare.
(Remember: uids are cheap.)

The build users should have permission to create files in the Nix
store, but not delete them. Therefore, `/nix/store` should be owned
by the Nix account, its group should be the group specified here,
and its mode should be `1775`.

If the build users group is empty, builds will be performed under
the uid of the Lix process (that is, the uid of the caller if
both `NIX_REMOTE` is either empty or `auto` and the Nix store is
owned by that user, or, alternatively, the uid under which the Nix
daemon runs if `NIX_REMOTE` is `daemon` or if it is `auto` and the
store is not owned by the caller). Obviously, this should not be used
with a nix daemon accessible to untrusted clients.

For the avoidance of doubt, explicitly setting this to *empty* with a
Lix daemon running as root means that builds will be executed as root
with respect to the rest of the system.
We intend to fix this: https://git.lix.systems/lix-project/lix/issues/242
