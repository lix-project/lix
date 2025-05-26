# Garbage Collection

`nix-env` operations such as upgrades (`-u`) and uninstall (`-e`) never
actually delete packages from the system. All they do (as shown above)
is to create a new user environment that no longer contains symlinks to
the “deleted” packages.

Of course, since disk space is not infinite, unused packages should be
removed at some point. You can do this by running the Lix garbage
collector. It will remove from the Nix store any package not used
(directly or indirectly) by any generation of any profile.

Note however that as long as old generations reference a package, it
will not be deleted. After all, we wouldn’t be able to do a rollback
otherwise. So in order for garbage collection to be effective, you
should also delete (some) old generations. Of course, this should only
be done if you are certain that you will not need to roll back.

To delete all old (non-current) generations of your current profile:

```console
$ nix-env --delete-generations old
```

Instead of `old` you can also specify a list of generations, e.g.,

```console
$ nix-env --delete-generations 10 11 14
```

To delete all generations older than a specified number of days (except
the current generation), use the `d` suffix. For example,

```console
$ nix-env --delete-generations 14d
```

deletes all generations older than two weeks.

After removing appropriate old generations you can run the garbage
collector as follows:

```console
$ nix-store --gc
```

The behaviour of the garbage collector is affected by the
`keep-derivations` (default: true) and `keep-outputs` (default: false)
options in the Nix configuration file. The defaults will ensure that all
derivations that are build-time dependencies of garbage collector roots
will be kept and that all output paths that are runtime dependencies
will be kept as well. All other derivations or paths will be collected.
(This is usually what you want, but while you are developing it may make
sense to keep outputs to ensure that rebuild times are quick.) If you
are feeling uncertain, you can also first view what files would be
deleted:

```console
$ nix-store --gc --print-dead
```

Likewise, the option `--print-live` will show the paths that *won’t* be
deleted.

There is also a convenient little utility `nix-collect-garbage`, which
when invoked with the `-d` (`--delete-old`) switch deletes all old
generations of all profiles in `/nix/var/nix/profiles`. So

```console
$ nix-collect-garbage -d
```

is a quick and easy way to clean up your system.

## Garbage Collector Roots

### Explicit roots

All store paths to which there are symlinks in the directory
`prefix/nix/var/nix/gcroots` will be used as roots by the garbage
collector. For instance, the following command makes the path
`/nix/store/d718ef...-foo` a root of the collector:

```console
$ ln -s /nix/store/d718ef...-foo /nix/var/nix/gcroots/bar
```

That is, after this command, the garbage collector will not remove
`/nix/store/d718ef...-foo` or any of its dependencies.

Subdirectories of `prefix/nix/var/nix/gcroots` are also searched for
symlinks.

Symlinks may also point to paths outside the nix store. If the
destination of the symlink is itself a symlink to a store path, it
is also considered a root. This style of GC root is called an
"indirect root", and is created by tools like `nix-build` to avoid
garbage-collecting paths that are being used on-the-fly rather than
installed in profiles.


### In-use roots

Lix will also perform a best-effort detection of paths that are in use
by running processes when scanning for garbage collection roots, to
avoid removing paths that are still needed by running processes.

Exact details vary between platforms, but the following will generally
be taken into account:

- Executables in the store that are currently running;
- Other files in the store that are mapped into a process's address space (e.g. shared libraries);
- Files in the store to which processes have open handles;
- Store paths found in processes' environment variables.

Note that this detection is susceptible to missing paths that may still be in use for multiple reasons:

- Time-of-check-to-time-of-use (TOCTTOU): new processes may appear
  after Lix has enumerated the currently running processes, and will
  not be taken into account;
- Access privileges: if the garbage collection is not running as the
  root user (this is typically the case for single-user
  installations), it will not be able to scan processes belonging to
  other users;
- Other types of references: store paths may be stored in parts of the
  filesystem (e.g. databases) or process memory (e.g. environment
  variables changed since the start of the process) that Lix does not
  scan.

For this reason, it is recommended to create explicit roots whenever
using store paths that aren't obtained from some existing explicit GC
root.
