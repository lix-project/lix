# Release engineering

This directory contains the release engineering scripts for Lix.

## Release process

### Prerequisites

* FIXME: validation via misc tests in nixpkgs, among other things? What other
  validation do we need before we can actually release?
* Have a release post ready to go as a PR on the website repo.
* No [release-blocker bugs][release-blockers].

[release-blockers]: https://git.lix.systems/lix-project/lix/issues?q=&type=all&sort=&labels=145&state=open&milestone=0&project=0&assignee=0&poster=0

### Process

The following process can be done either against the staging environment or the
live environment.

For staging, the buckets are `staging-releases`, `staging-cache`, etc.

FIXME: obtainment of signing key for signing cache paths?

First, we prepare the release. `python -m releng prepare` is used for this.

* Gather everything in `doc/manual/rl-next` and put it in
  `doc/manual/src/release-notes/rl-MAJOR.md`.

Then we tag the release with `python -m releng tag`:

* Git HEAD is detached.
* `officialRelease = true` is set in `flake.nix`, this is committed, and a
  release is tagged.
* The tag is merged back into the last branch (either `main` for new releases
  or `release-MAJOR` for maintenance releases) with `git merge -s ours VERSION`
  creating a history link but ignoring the tree of the release tag.
* Git HEAD is once again detached onto the release tag.

Then, we build the release artifacts with `python -m releng build`:

* Source tarball is generated with `git archive`, then checksummed.
* Manifest for `nix upgrade-nix` is produced and put in `s3://releases` at
  `/manifest.nix` and `/lix/lix-VERSION`.
* Release is built: `hydraJobs.binaryTarball` jobs are built, and joined into a
  derivation that depends on all of them and adds checksum files. This and the
  sources go into `s3://releases/lix/lix-VERSION`.

At this point we have a `release/artifacts` and `release/manual` directory
which are ready to publish, and tags ready for publication. No keys are
required to do this part.

Next, we do the publication with `python -m releng upload`:

* Artifacts for this release are uploaded:
  * s3://releases/manifest.nix, changing the default version of Lix for
    `nix upgrade-nix`.
  * s3://releases/lix/lix-VERSION/ gets the following contents
    * Binary tarballs
    * Docs: `manual/` (FIXME: should we actually do this? what about putting it
      on docs.lix.systems? I think doing both is correct, since the Web site
      should not be an archive of random old manuals)
    * Docs as tarball in addition to web.
    * Source tarball
    * Docker image (FIXME: upload to forgejo registry and github registry [in the future][upload-docker])
  * s3://docs/manual/lix/MAJOR
  * s3://docs/manual/lix/stable

* The tag is uploaded to the remote repo.
* **Manually** build the installer using the scripts in the installer repo and upload.

  FIXME: This currently requires a local Apple Macintosh® aarch64 computer, but
  we could possibly automate doing it from the aarch64-darwin builder.
* **Manually** Push the main/release branch directly to gerrit.
* If this is a new major release, branch-off to `release-MAJOR` and push *that* branch
  directly to gerrit as well (FIXME: special creds for doing this as a service
  account so we don't need to have the gerrit perms to shoot ourselves in the
  foot by default because pushing to main is bad?).

  FIXME: automate branch-off to `release-*` branch.
* **Manually** (FIXME?) switch back to the release branch, which now has the
  correct revision.
* Post!!
  * Merge release blog post to [lix-website].
  * Toot about it! https://chaos.social/@lix_project
  * Tweet about it! https://twitter.com/lixproject

[lix-website]: https://git.lix.systems/lix-project/lix-website

[upload-docker]: https://git.lix.systems/lix-project/lix/issues/252

### Installer

The installer is cross-built to several systems from a Mac using
`build-all.xsh` and `upload-to-lix.xsh` in the installer repo (FIXME: currently
at least; maybe this should be moved here?) .

It installs a binary tarball (FIXME: [it should be taught to substitute from
cache instead][installer-substitute])
from some URL; this is the `hydraJobs.binaryTarball`. The default URLs differ
by architecture and are [configured here][tarball-urls].

[installer-substitute]: https://git.lix.systems/lix-project/lix-installer/issues/13
[tarball-urls]: https://git.lix.systems/lix-project/lix-installer/src/commit/693592ed10d421a885bec0a9dd45e87ab87eb90a/src/settings.rs#L14-L28

## Infrastructure summary

* releases.lix.systems (`s3://releases`):
  * Each release gets a directory: https://releases.lix.systems/lix/lix-2.90-beta.1
    * Binary tarballs: `nix-2.90.0-beta.1-x86_64-linux.tar.xz`, from `hydraJobs.binaryTarball`
    * Manifest: `manifest.nix`, an attrset of the store paths by architecture.
  * Manifest for `nix upgrade-nix` to the latest release at `/manifest.nix`.
* cache.lix.systems (`s3://cache`):
  * Receives all artifacts for released versions of Lix; is a plain HTTP binary cache.
* install.lix.systems (`s3://install`):
  ```
  ~ » aws s3 ls s3://install/lix/
                             PRE lix-2.90-beta.0/
                             PRE lix-2.90-beta.1/
                             PRE lix-2.90.0pre20240411/
                             PRE lix-2.90.0pre20240412/
  2024-05-05 18:59:11    6707344 lix-installer-aarch64-darwin
  2024-05-05 18:59:16    7479768 lix-installer-aarch64-linux
  2024-05-05 18:59:14    7982208 lix-installer-x86_64-darwin
  2024-05-05 18:59:17    8978352 lix-installer-x86_64-linux

  ~ » aws s3 ls s3://install/lix/lix-2.90-beta.1/
  2024-05-05 18:59:01    6707344 lix-installer-aarch64-darwin
  2024-05-05 18:59:06    7479768 lix-installer-aarch64-linux
  2024-05-05 18:59:03    7982208 lix-installer-x86_64-darwin
  2024-05-05 18:59:07    8978352 lix-installer-x86_64-linux
  ```
