# Serving a Nix store via HTTP

<div class="warning">

FIXME(Lix): This section documents outdated practices.

In particular, the Lix developers would *not* recommend using `nix-serve` as it is relatively-unmaintained Perl.
The Lix developers would recommend instead using an s3 based cache (which is what https://cache.nixos.org is), and if it is desired to self-host it, use something like [garage](https://garagehq.deuxfleurs.fr/).

See the following projects:
- [attic](https://github.com/zhaofengli/attic) - multi-tenant cache for larger deployments, using s3 as a backend.
- [harmonia](https://github.com/nix-community/harmonia) - closer to a drop in replacement for use cases served by nix-serve

</div>

You can easily share the Nix store of a machine via HTTP. This allows
other machines to fetch store paths from that machine to speed up
installations. It uses the same *binary cache* mechanism that Lix
usually uses to fetch pre-built binaries from <https://cache.nixos.org>.

The daemon that handles binary cache requests via HTTP, `nix-serve`, is
not part of the Nix distribution, but you can install it from Nixpkgs:

```console
$ nix-env --install --attr nixpkgs.nix-serve
```

You can then start the server, listening for HTTP connections on
whatever port you like:

```console
$ nix-serve -p 8080
```

To check whether it works, try the following on the client:

```console
$ curl http://avalon:8080/nix-cache-info
```

which should print something like:

    StoreDir: /nix/store
    WantMassQuery: 1
    Priority: 30

On the client side, you can tell Lix to use your binary cache using `--substituters` (assuming you are a trusted user, see `trusted-users` in nix.conf), e.g.:

```console
$ nix-env --install --attr nixpkgs.firefox --substituters http://avalon:8080/
```

The option `substituters` tells Lix to use this binary cache in
addition to your default caches, such as <https://cache.nixos.org>.
Thus, for any path in the closure of Firefox, Lix will first check if
the path is available on the server `avalon` or another binary caches.
If not, it will fall back to building from source.

You can also tell Lix to always use your binary cache by adding a line
to the `nix.conf` configuration file like this:

    substituters = http://avalon:8080/ https://cache.nixos.org/
