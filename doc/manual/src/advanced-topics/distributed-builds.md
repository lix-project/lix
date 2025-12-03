# Remote Builds

Lix supports remote builds, where a local Lix installation can forward
Nix builds to other machines. This allows multiple builds to be
performed in parallel and allows Lix to perform multi-platform builds in
a semi-transparent way. For instance, if you perform a build for a
`x86_64-darwin` on an `i686-linux` machine, Lix can automatically
forward the build to a `x86_64-darwin` machine, if available.

To forward a build to a remote machine, it’s required that the remote
machine is accessible via SSH and that it has Nix installed. You can
test whether connecting to the remote Nix instance works, e.g.

```console
$ nix store ping --store ssh://mac
```

will try to connect to the machine named `mac`. It is possible to
specify an SSH identity file as part of the remote store URI, e.g.

```console
$ nix store ping --store ssh://mac?ssh-key=/home/alice/my-key
```

Since builds should be non-interactive, the key should not have a
passphrase. Alternatively, you can load identities ahead of time into
`ssh-agent` or `gpg-agent`.

If you get the error

```console
bash: nix-store: command not found
error: cannot connect to 'mac'
```

then you need to ensure that the `PATH` of non-interactive login shells
contains Nix.

> **Warning**
>
> If you are building via the Lix daemon (default on Linux and macOS), it is the Lix daemon user account (that is, `root`) that should have SSH access to a user (not necessarily `root`) on the remote machine.
>
> Furthermore, `root` needs to have the public host keys for the remote system in its `.ssh/known_hosts`.
> To add them to `known_hosts` for root, do `ssh-keyscan HOST | sudo tee -a ~root/.ssh/known_hosts`.
>
> If you can’t or don’t want to configure `root` to be able to access the remote machine, you can use a private Nix store instead by passing e.g. `--store ~/my-nix` when running a Nix command from the local machine.


## Configuration

The list of remote machines can be specified on the command line or in
the Lix configuration file. The former is convenient for testing.
Additionally, there are two supported formats to configure remote builders:
The legacy, "space"-separated format and starting with Lix 2.95.0, a TOML.

Remote builders can also be configured in `nix.conf`, e.g.

    builders = ssh://mac x86_64-darwin ; ssh://beastie x86_64-freebsd

Finally, remote builders can be configured in a separate configuration
file included in `builders` via the syntax `@file`. For example,

    builders = @/etc/nix/machines

causes the list of machines in `/etc/nix/machines` to be included. (This
is the default.)

If you want the builders to use caches, you likely want to set the
option `builders-use-substitutes` in your local `nix.conf`.

To build only on remote builders and disable building on the local
machine, you can use the option `--max-jobs 0`.

---

Each machine specification consists of the following attributes.
How those are combined within the configuration file differs for the formats, and will be explained further down.

1.  `uri` (**required**)
    The URI of the remote store in the format
    `ssh[-ng]://[username@]hostname[?port=<port>]`, e.g. `ssh://nix@mac` or `ssh://mac`.
    If the ssh server is not listening on port 22 (e.g. port 1337 in this case)
    the URI would be `ssh[-ng]://nix@mac?port=1337`. The hostname
    may be an alias defined in your `~/.ssh/config`.

2.  `system-types` (**optional**)
    A list of Nix platform type identifiers, such as
    `x86_64-darwin`. It is possible for a machine to support multiple
    platform types, e.g., `i686-linux` and `x86_64-linux`.

    Defaults to the local platform type

3.  `ssh-key` (**optional**)
    The SSH identity file to be used to log in to the remote machine.

    Defaults to SSHs regular identities.

4.  `jobs` (**optional**)
    The maximum number of builds that Lix will execute in parallel on
    the machine. Typically, this should be equal to the number of CPU
    cores divided by the cores within the target machines configuration, i.e. `jobs * cores ~= cpu cores`

    Defaults to 1; must be a positive integer.

5.  `speed-factor`
    The “speed factor”, indicating the relative speed of the machine. If
    there are multiple machines of the right type, Lix will prefer the
    fastest, taking load into account.

    Defaults to 1; must be a positive float.

6.  `supported-features` (**optional**)
    A list of *supported features*. If a derivation has
    the `requiredSystemFeatures` attribute, then Lix will only schedule
    the derivation on a machine that has the specified features. For
    example, the attribute

    ```nix
    requiredSystemFeatures = [ "kvm" ];
    ```

    will cause the build to be performed on a machine that has the `kvm`
    feature.

    Defaults to an empty list.

7.  `mandatory-features` (**optional**)
    A list of *mandatory features*. A machine will only
    be used to build a derivation if all the machine’s mandatory
    features appear in the derivation’s `requiredSystemFeatures`
    attribute.

    Defaults to an empty list.

8.  `ssh-public-host-key` (**optional**)
    The public host key of the remote machine.

    Defaults to basic ssh behavior (checking contests of the known-hosts file)


### Using a TOML configuration

Each machine is configured as an attribute within the map called `machines`.
The attributes name is the machines name.
Attributes can be in any order.

For example:

```toml
version = 1

[machines.andesite]
uri = "ssh://lix@andesite.lix.systems" # toml also allows for comments
system-types = ["i686-linux"]
jobs = 8
speed-factor = 1.0
supported-features = ["kvm"]
ssh-key = "/home/deepslate/.ssh/id_ed25519"

[machines.diorite]
uri = "ssh://lix@diorite.lix.systems"
system-types = ["i686-linux"]
jobs = 8
speed-factor = 2.0
ssh-key = "/home/deepslate/.ssh/id_ed25519"

[machines.granite]
uri = "ssh://lix@granite.lix.systems"
system-types = ["i686-linux"]
jobs = 1
speed-factor = 2.0
supported-features = ["kvm", "benchmark"]
ssh-key = "/home/deepslate/.ssh/id_ed25519"

[machines.legacy]
uri = "ssh://nix@nix-15-11.nixos.org"
enable = false

```

> **Note**
>
> If the version tag is omitted (e.g. in the CLI), it defaults to the latest version.
> It is strongly recommended to always provide a version tag for configuration within files to avoid breakage.

For testing purposes, one can also define a builder ad hoc on the CLI as follows:
`--builders 'machines.andesite = {uri = "ssh://lix@andesite.lix.systems", jobs = 8}'`


#### Special handling of fields
 - `enable` (**optional**)
    If set to false, the declared machine will not be loaded.
    This allows one to statically disable machines.

    Defaults to true

### Using the legacy format
> **Warning**
>
> This format is frozen and new features / configuration options will not be backported to this format.

It is possible to specify multiple builders separated by a semicolon or
a newline, e.g.

```console
  --builders 'ssh://mac x86_64-darwin ; ssh://beastie x86_64-freebsd'
```

Every machine specification consists of the elements listed in the section above, seperated by any amount of spaces or tabs.
The Attributes need to be provided **in order** and without names.
To leave a field at its default, set it to `-`.
Lists are colon seperated, without additional spaces.

```
lix@andesite.lix.systems    i686-linux      /home/deepslate/.ssh/id_ed25519        8 1 kvm
lix@diorite.lix.systems     i686-linux      /home/deepslate/.ssh/id_ed25519        8 2
lix@granite.lix.systems     i686-linux      /home/deepslate/.ssh/id_ed25519        1 2 kvm benchmark
```

#### Special handling of fields
- `uri`: Due to backward compatibility, the `ssh://` may be omitted for the store-uri.
- `ssh-public-host-key`: The key must be provided encoded in base64. Specifically calculated via `base64 -w0 /etc/ssh/ssh_host_ed25519_key.pub`


### Format detection

At first, the given configuration is being parsed syntactically as a toml.
If parsing fails and the given configuration contains a `"` the error is presented to the user, as those characters are necessary for TOML, but disallowed for the legacy format.
Otherwise, parsing is retried using the legacy format.
If non-syntactic errors are detected within the toml, the exception will always be shown to the user directly.


## Builder selection
The configuration(s) above specify several machines that can perform `i686-linux` builds.
However, `granite` will only do builds that have the attribute

```nix
requiredSystemFeatures = [ "benchmark" ];
```

or

```nix
requiredSystemFeatures = [ "benchmark" "kvm" ];
```

`diorite` cannot do builds that require `kvm`, but `andesite` does support
such builds. For regular builds, `diorite` will be preferred over
`andesite` because it has a higher speed factor.
