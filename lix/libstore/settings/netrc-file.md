---
name: netrc-file
internalName: netrcFile
type: std::string
defaultExpr: 'fmt("%s/%s", nixConfDir, "netrc")'
defaultText: '`/etc/nix/netrc`'
---
If set to an absolute path to a `netrc` file, Lix will use the HTTP
authentication credentials in this file when trying to download from
a remote host through HTTP or HTTPS.

The `netrc` file consists of a list of accounts in the following
format:

    machine my-machine
    login my-username
    password my-password

For the exact syntax, see [the `curl`
documentation](https://everything.curl.dev/usingcurl/netrc.html).

> **Note**
>
> This must be an absolute path, and `~` is not resolved. For
> example, `~/.netrc` won't resolve to your home directory's
> `.netrc`.

Defaults to `$NIX_CONF_DIR/netrc`.
The default shown below is only accurate when the value of `NIX_CONF_DIR` has not been overridden at build time or using the environment variable.
