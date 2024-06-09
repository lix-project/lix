# Using Lix within Docker

Lix is available on the following two container registries:
- [ghcr.io/lix-project/lix](https://ghcr.io/lix-project/lix)
- [git.lix.systems/lix-project/lix](https://git.lix.systems/lix-project/-/packages/container/lix)

To run the latest stable release of Lix with Docker run the following command:

```console
~ » sudo podman run -it ghcr.io/lix-project/lix:latest
Trying to pull ghcr.io/lix-project/lix:latest...

bash-5.2# nix --version
nix (Lix, like Nix) 2.90.0
```

# What is included in Lix's Docker image?

The official Docker image is created using [nix2container]
(and not with `Dockerfile` as it is usual with Docker images). You can still
base your custom Docker image on it as you would do with any other Docker
image.

[nix2container]: https://github.com/nlewo/nix2container

The Docker image is also not based on any other image and includes the nixpkgs
that Lix was built with along with a minimal set of tools in the system profile:

- bashInteractive
- cacert.out
- coreutils-full
- curl
- findutils
- gitMinimal
- gnugrep
- gnutar
- gzip
- iana-etc
- less
- libxml2
- lix
- man
- openssh
- sqlite
- wget
- which

# Docker image with the latest development version of Lix

FIXME: There are not currently images of development versions of Lix. Tracking issue: https://git.lix.systems/lix-project/lix/issues/381

You can build a Docker image from source yourself and copy it to either:

Podman: `nix run '.#dockerImage.copyTo' containers-storage:lix`

Docker: `nix run '.#dockerImage.copyToDockerDaemon'`

Then:

```console
$ docker run -ti lix
```
