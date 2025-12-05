---
name: extra-local-jobs
internalName: extraLocalJobs
type: std::optional<uint32_t>
defaultText: "1 if `max-jobs = 0` otherwise 0"
---

This option defines an additional amount of slots for local jobs.
`preferLocalBuild` derivations will prioritize acquiring from the local job pool first,
then fall back to a `max-jobs` pool.

By default, `extra-local-jobs` is empty except if `max-jobs = 0`, in which
case, Lix will automatically set this value to `1` to allow local jobs to
progress as they are usually trivial.

This option can be overridden using the `--extra-local-jobs` flag.
