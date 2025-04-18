---
name: fetchGit
args: [args]
renameInGlobalScope: false
---
Fetch a path from git. *args* can be a URL, in which case the HEAD
of the repo at that URL is fetched. Otherwise, it can be an
attribute with the following attributes (all except `url` optional):

- `url`

  The URL of the repo.

- `name` (default: *basename of the URL*)

  The name of the directory the repo should be exported to in the store.

- `rev` (default: *the tip of `ref`*)

  The [Git revision] to fetch.
  This is typically a commit hash.

  [Git revision]: https://git-scm.com/docs/git-rev-parse#_specifying_revisions

- `ref` (default: `HEAD`)

  The [Git reference] under which to look for the requested revision.
  This is often a branch or tag name.

  [Git reference]: https://git-scm.com/book/en/v2/Git-Internals-Git-References

  By default, the `ref` value is prefixed with `refs/heads/`.
  As of 2.3.0, Nix will not prefix `refs/heads/` if `ref` starts with `refs/` or
  if `ref` looks like a commit hash for backwards compatibility with CppNix 2.3.

- `submodules` (default: `false`)

  A Boolean parameter that specifies whether submodules should be checked out.

- `shallow` (default: `false`)

  A Boolean parameter that specifies whether fetching from a shallow remote repository is allowed.
  This still performs a full clone of what is available on the remote.

- `allRefs`

  Whether to fetch all references of the repository.
  With this argument being true, it's possible to load a `rev` from *any* `ref`
  (by default only `rev`s from the specified `ref` are supported).

- `narHash`

  If given, the source is first looked-up in the Nix store and the [substituters](@docroot@/command-ref/conf-file.md#conf-substituters), and only fetched if not available.

The return value is an attrset containing the following keys:

- `lastModified` (`integer`)

  Unix timestamp of the last update.
  This corresponds to the timestamp of the "committer" timestamp embedded in the fetched commit.

- `lastModifiedDate` (`string`)

  Textual representation of the `lastModified` timestamp in UTC (the timezone embedded in the git commit is discarded).

- `outPath` (`string`)

  Resulting store path of the fetch process.

- `narHash` (`string`)

  SRI representation of the hash of the `outPath`.

- `rev` (`string`)

  The full-length revision fetched from the remote.
  For further information see the `rev` input parameter.
  This will usually be the output of `git rev-parse <rev>` (or `ref` when no `rev` is provided as an input parameter).

- `revCount` (`integer`)

  Number of revisions in the history of the revision fetched.
  For a repository with a single commit (the root) this number equals 1.
  Fetches of shallow repositories report a value of 0.

- `shortRev` (`string`)

  A short representation of the `rev`.
  This string is a *truncated* version of the `rev`.
  It is of fixed length and therefore not guaranteed to be unique (unlike the output of `git rev-parse --short`).
  Future versions of Lix may change the length of this string only as part of a breaking change.
  For maximum reproducibility and interoperability it is recommended to not rely on this value and to truncate the returned `rev` to an appropriate value instead.

- `submodules` (`boolean`)

  Indicates whether submodules have been fetched.
  If this value is set to `true`, any submodules are already checked out in the resulting `outPath`.

A full example of the output:

```nix
{
  lastModified = 1746827286;
  lastModifiedDate = "20250509214806";
  narHash = "sha256-qCRBy8Bbh5XhPalPkhonxNgfsbw3lP0UIXBLSrhxAvI=";
  outPath = "/nix/store/2qdnzhzccspwm70mni7jkvrfkpwcb3jn-source";
  rev = "dcb0a97000d50b2868ed4f8d9fd465c5a5b8eb3a";
  revCount = 17845;
  shortRev = "dcb0a97";
  submodules = false;
}
```

Here are some examples of how to use `fetchGit`.

  - To fetch a private repository over SSH:

    ```nix
    builtins.fetchGit {
      url = "git@github.com:my-secret/repository.git";
      ref = "master";
      rev = "adab8b916a45068c044658c4158d81878f9ed1c3";
    }
    ```

  - To fetch an arbitrary reference:

    ```nix
    builtins.fetchGit {
      url = "https://github.com/NixOS/nix.git";
      ref = "refs/heads/0.5-release";
    }
    ```

  - If the revision you're looking for is in the default branch of
    the git repository you don't strictly need to specify the branch
    name in the `ref` attribute.

    However, if the revision you're looking for is in a future
    branch for the non-default branch you will need to specify
    the `ref` attribute as well.

    ```nix
    builtins.fetchGit {
      url = "https://github.com/nixos/nix.git";
      rev = "841fcbd04755c7a2865c51c1e2d3b045976b7452";
      ref = "1.11-maintenance";
    }
    ```

    > **Note**
    >
    > It is nice to always specify the branch which a revision
    > belongs to. Without the branch being specified, the fetcher
    > might fail if the default branch changes. Additionally, it can
    > be confusing to try a commit from a non-default branch and see
    > the fetch fail. If the branch is specified the fault is much
    > more obvious.

  - If the revision you're looking for is in the default branch of
    the git repository you may omit the `ref` attribute.

    ```nix
    builtins.fetchGit {
      url = "https://github.com/nixos/nix.git";
      rev = "841fcbd04755c7a2865c51c1e2d3b045976b7452";
    }
    ```

  - To fetch a specific tag:

    ```nix
    builtins.fetchGit {
      url = "https://github.com/nixos/nix.git";
      ref = "refs/tags/1.9";
    }
    ```

  - To fetch the latest version of a remote branch:

    ```nix
    builtins.fetchGit {
      url = "ssh://git@github.com/nixos/nix.git";
      ref = "master";
    }
    ```

    Nix will refetch the branch according to the [`tarball-ttl`](@docroot@/command-ref/conf-file.md#conf-tarball-ttl) setting.

    This behavior is disabled in [pure evaluation mode](@docroot@/command-ref/conf-file.md#conf-pure-eval).

  - To fetch the content of a checked-out work directory:

    ```nix
    builtins.fetchGit ./work-dir
    ```

If the URL points to a local directory, and no `ref` or `rev` is
given, `fetchGit` will use the current content of the checked-out
files, even if they are not committed or added to Git's index. It will
only consider files added to the Git repository, as listed by `git ls-files`.
