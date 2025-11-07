# Limitations of non-isolated builds

## What are non-isolated builds?

In Lix, only builds done on Linux with `sandbox = true` and a functioning
`pasta-path` are isolated from the rest of the system, all other builds are
considered non-isolated to some degree.

For example, running Lix with [Pasta](@docroot@/advanced-topics/pasta.md)
disabled makes the host network visible to fixed-output derivations, reducing
isolation somewhat.

## Clean termination of non-isolated builds

Non-isolated builds may not terminate cleanly in all cases due to limitations in Lix's process management.

This occurs when a build keeps the build log file descriptor open past the end of the actual build. A common cause of this are background tasks that aren't properly terminated before the main build process exits, for example: HTTP servers run as part of a test suite.

See [issue #1018](https://git.lix.systems/lix-project/lix/issues/1018) for an example.

The only solution is to manually terminate leftover processes in your derivation, including during failure scenarios.
