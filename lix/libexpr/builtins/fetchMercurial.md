---
name: fetchMercurial
args: [args]
renameInGlobalScope: false
---

Fetch a Mercurial repository. *args* can be a URL, in which case the default
branch of the repo at that URL is fetched. Otherwise, it can be an
attribute with the following attributes (all except `url` optional):

- `url`

  The URL of the repo.

- `name` (default: `"source"`)

  The name of the directory the repo should be exported to in the store.

- `rev`

  The revision to fetch.
  This is typically a commit hash.

  > **Note**
  >
  > Currently, `rev` can either contain a revision or a branch/tag name.

The return value is an attrset containing the following keys:

- `outPath` (`string`)

  Resulting store path of the fetch process.

- `branch` (`string`)

  The branch of the fetch repository.

- `rev` (`string`)

  The revision that was fetched.

- `revCount` (`int`)

  The number of revsets for this branch.

- `shortRev` (`string`)

  The first *12* characters of `rev`.
