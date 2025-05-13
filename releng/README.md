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

* Switch to the release branch you'd like to base your release off of.
  * For a major release:
    ```
    git switch -c releng/2.93.0 origin/main
    ```
  * For a patch release:
    ```
    git switch -c releng/2.93.1 origin/release-2.93
    ```

* Set the version and release name you are going to release in `version.json`.

  Note: Release names are only for major releases (one of the first two
  components changes).

  See: <https://wiki.lix.systems/books/lix-contributors/page/release-names>

* Commit the new `version.json`.

* Next, prepare the release. `python -m releng prepare` is used for this.

* Then we tag the release with `python -m releng tag`, which does:

  * Git HEAD is detached.
  * `"official_release": true` is set in `version.json`, this is committed, and a
    release is tagged.
  * The tag is merged back into the last branch (either `main` for new releases
    or `release-MAJOR` for maintenance releases) with `git merge -s ours VERSION`
    creating a history link but ignoring the tree of the release tag.
  * Git HEAD is once again detached onto the release tag.

* Then, we build the release artifacts with `python -m releng build`:

  * Source tarball is generated with `git archive`, then checksummed.
  * Manifest for `nix upgrade-nix` is produced and put in `s3://releases` at
    `/manifest.nix` and `/lix/lix-VERSION`.
  * Release is built: `hydraJobs.binaryTarball` jobs are built, and joined into a
    derivation that depends on all of them and adds checksum files. This and the
    sources go into `s3://releases/lix/lix-VERSION`.

* At this point we have a `release/artifacts` and `release/manual` directory
  which are ready to publish, and tags ready for publication. No keys are
  required to do this part.

* Next, we do the publication with `python -m releng upload --environment production`:
  * Running this command will require access tokens for a couple systems:
    * Create an access token for `git.lix.systems` with `read:user` and
      `write:package` permissions.
      See: <https://git.lix.systems/user/settings/applications>

    * Create an access token for GitHub. The easiest way to this is with the `gh` CLI:

      ```
      gh auth refresh --scopes write:packages && gh auth token
      ```
      See: <https://cli.github.com/>

      Check your token's permissions:
      ```
      $ gh api -i / | grep '^X-Oauth-Scopes'
      X-Oauth-Scopes: gist, read:org, repo, workflow, write:packages
      ```

      You can probably also do this with a traditional PAT, but GitHub has like
      three different permissions systems and they're all poorly documented.
      See: <https://github.com/settings/tokens>

    * You'll also need `prod.key`; ask @jade where to find it.

  * s3://releases/manifest.nix, changing the default version of Lix for
    `nix upgrade-nix`.
  * s3://releases/lix/lix-VERSION/ gets the following contents
    * Binary tarballs
    * Docs: `manual/`, primarily as an archive of old manuals
    * Docs as tarball in addition to web.
    * Source tarball
    * Docker image
  * s3://docs/manual/lix/MAJOR
  * s3://docs/manual/lix/stable
  * The tag is uploaded to the remote repo.

* **Manually** build the installer using the scripts in the installer repo and upload.

  FIXME: This currently requires a local Apple Macintosh® aarch64 computer, but
  we could possibly automate doing it from the aarch64-darwin builder.

* **Manually** Push the main/release branch directly to Gerrit:
  ```
  git push HEAD:refs/for/main
  ```

* **Manually** Push the tag to Gerrit:
  ```
  git push origin 2.93.0:refs/tags/2.93.0
  ```

  TODO: `./create_release.xsh` has code that looks like it does this already,
  but @rbt had to do it manually when running the release for 2.93.0.

  From @jade: "This is because the code is broken; it's because Gerrit doesn't
  like tags that haven't yet had their contents submitted for review."

* If this is a new major release, branch-off to `release-MAJOR` and push *that* branch
  directly to gerrit as well (FIXME: special creds for doing this as a service
  account so we don't need to have the gerrit perms to shoot ourselves in the
  foot by default because pushing to main is bad?).

  FIXME: automate branch-off to `release-*` branch.
* **Manually** (FIXME?) switch back to the release branch, which now has the
  correct revision.
* Deal with the external systems (see sections below).
* Post!!
  * Merge release blog post to [lix-website].
  * Toot about it! https://chaos.social/@lix_project
  * Tweet about it! https://twitter.com/lixproject

[lix-website]: https://git.lix.systems/lix-project/lix-website

### Post-Validation

After the release is created, we do some post-release validation for the Lix
packaging in Nixpkgs. Most of these should probably be pre-release validation checks!

- [ ] Build on 4 platforms (`{x86_64,aarch64}-{linux,darwin}`)
- [ ] Build with debuginfo: build the `lix.debug` attribute and check it is not an empty directory.

  FIXME(jade): this applies only to Linux right?
  I think separate debuginfo on macOS is just broken so we have no debuginfo on there.
- [ ] Verify that manual is present in the `doc` output at `share/doc/nix/manual/`
- [ ] Verify that `:doc` in the `nix repl` finds documentation for `builtins` and Nixpkgs `lib.fix`
- [ ] Cross from `x86_64` → `aarch64` (`pkgsCross.aarch64-multiplatform.lixVersions.lix_2_91`)
- [ ] Cross from `x86_64` → `riscv64` (`pkgsCross.riscv64.lixVersions.lix_2_91`)
- [ ] Cross from `x86_64` → `armv7l` (`pkgsCross.armv7l-hf-multiplatform.lixVersions.lix_2_91`)
- [ ] Cross from `aarch64` → `x86_64` (`pkgsCross.gnu64.lixVersions.lix_2_91` from `aarch64-linux`)
- [ ] Static builds on `x86_64-linux`, `aarch64-linux`, `aarch64-darwin` (`pkgs.pkgsStatic.lixVersions.lix_2_91`)
- [ ] Perform closure checks and verify that no unnecessary dependencies are included (compare to previous versions: `du -hs result/` and `nix path-info -rsh result/`)
- [ ] Ensure that previous versions did not explode in size neither in functionality: use the same methodology as above for `lixVersions.lix_2_91`, etc, after the new release is packaged.

### Installer

The installer is cross-built to several systems from a Mac using `build-all.xsh` and `upload-to-lix.xsh` in the installer repo (FIXME: currently at least; maybe this should be moved here?).

It installs a binary tarball (FIXME: [it should be taught to substitute from cache instead][installer-substitute]) from some URL; this is the `hydraJobs.binaryTarball`.
The default URLs differ by architecture and are [configured here][tarball-urls].

To automatically do the file changes for a new version, run `python3 set_version.py NEW_VERSION`, and submit the result for review.

[installer-substitute]: https://git.lix.systems/lix-project/lix-installer/issues/13
[tarball-urls]: https://git.lix.systems/lix-project/lix-installer/src/commit/693592ed10d421a885bec0a9dd45e87ab87eb90a/src/settings.rs#L14-L28

### Web site

The website has various release-version dependent pieces.
You can update them with `python3 update_version.py NEW_VERSION`, which will regenerate the affected page sources.

These need the release to have been done first as they need hashes for tarballs and such.

### NixOS module

The NixOS module has underdeveloped releng in it.
Currently you have to do the whole branch-off dance manually to a `release-VERSION` branch and update the tarball URLs to point to the release versions manually.

FIXME: this should be unified with the `set_version.py` work in `lix-installer` and probably all the releng kept in here, or kept elsewhere.
Related: https://git.lix.systems/lix-project/lix/issues/439

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
