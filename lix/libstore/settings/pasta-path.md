---
name: pasta-path
internalName: pastaPath
type: Path
default: ""
---
If set to an absolute path, enables fully sandboxing fixed-output
derivations, by using `pasta` to pass network traffic between the
private network namespace. This allows for greater levels of isolation
of builds to the host.
