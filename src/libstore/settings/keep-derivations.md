---
name: keep-derivations
internalName: gcKeepDerivations
type: bool
default: true
aliases: [gc-keep-derivations]
---
If `true` (default), the garbage collector will keep the derivations
from which non-garbage store paths were built. If `false`, they will
be deleted unless explicitly registered as a root (or reachable from
other roots).

Keeping derivation around is useful for querying and traceability
(e.g., it allows you to ask with what dependencies or options a
store path was built), so by default this option is on. Turn it off
to save a bit of disk space (or a lot if `keep-outputs` is also
turned on).
